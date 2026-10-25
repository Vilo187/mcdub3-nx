/*
 * mc3_mods.c -- replacing the game's files, textures and settings from a
 * mods folder, without touching the disc or the code.
 *
 *   sdmc:/switch/mcdub3-nx/mods/   (beside game/; MC3_MODS_DIR elsewhere)
 *   ├── files/               replaces a file of the disc at the same path:
 *   │                        files/video/rockstar.bik, files/flash/pause_x.xbck
 *   ├── packs/<name>/files/  the same, one folder per mod; packs are layered
 *   │                        by name (a later name wins), files/ above all
 *   ├── textures/<hash>.png  replacement textures, as an HD pack's (the
 *   │                        renderer's hd_pack.c; RECOMP_GL_TEX_DUMP=1
 *   │                        writes the game's own, named by that hash)
 *   ├── ui/font.ttf          the Extras menu's font (mc3_font.c)
 *   └── settings.txt         KEY=VALUE lines applied after mcdub3_env.txt
 *
 * Files are matched without regard to case, as the Xbox's file system does.
 * The folder is read once at start (a restart picks up changes): the index
 * keeps every open of the disc from touching the SD card twice. Every
 * directory handle is closed again -- leaked ones crash Atmosphere's
 * ams.mitm (CLAUDE.md).
 */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "mc3_mods.h"

typedef struct { char *key; char *host; } ModFile;   /* key: lower-case disc path */

static char     s_root[512];
static int      s_have_root;
static ModFile *s_files;
static size_t   s_n, s_cap;
static size_t   s_added;                /* files seen, for the per-layer count */

const char *mc3_mods_dir(void)
{
    return s_have_root ? s_root : NULL;
}

static int is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

int mc3_mods_path(const char *name, char *out, size_t size)
{
    struct stat st;
    if (!s_have_root)
        return 0;
    snprintf(out, size, "%s/%s", s_root, name);
    return stat(out, &st) == 0 && S_ISREG(st.st_mode);
}

static void lower(char *s)
{
    for (; *s; s++)
        *s = (char)tolower((unsigned char)*s);
}

/* Add or replace (a later layer wins). */
static void add(const char *rel, const char *host)
{
    char key[512];
    size_t i;
    snprintf(key, sizeof key, "%s", rel);
    lower(key);
    s_added++;
    for (i = 0; i < s_n; i++)
        if (!strcmp(s_files[i].key, key)) {
            free(s_files[i].host);
            s_files[i].host = strdup(host);
            return;
        }
    if (s_n == s_cap) {
        size_t cap = s_cap ? s_cap * 2 : 64;
        ModFile *n = (ModFile *)realloc(s_files, cap * sizeof *n);
        if (!n)
            return;
        s_files = n;
        s_cap = cap;
    }
    s_files[s_n].key = strdup(key);
    s_files[s_n].host = strdup(host);
    s_n++;
}

/* Every file under dir, as paths relative to base_len. Depth-limited. */
static void walk(const char *dir, size_t base_len, int depth)
{
    DIR *d;
    struct dirent *e;
    char **subs = NULL;
    size_t nsub = 0, i;

    if (depth > 8 || !(d = opendir(dir)))
        return;
    while ((e = readdir(d)) != NULL) {
        char path[768];
        if (e->d_name[0] == '.')
            continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (is_dir(path)) {
            char **n = (char **)realloc(subs, (nsub + 1) * sizeof *n);
            if (n) { subs = n; subs[nsub++] = strdup(path); }
        } else {
            add(path + base_len + 1, path);
        }
    }
    closedir(d);                        /* before descending: one handle at a time */
    for (i = 0; i < nsub; i++) {
        walk(subs[i], base_len, depth + 1);
        free(subs[i]);
    }
    free(subs);
}

static int cmp_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int cmp_file(const void *a, const void *b)
{
    return strcmp(((const ModFile *)a)->key, ((const ModFile *)b)->key);
}

static void layer(const char *files_dir)
{
    size_t before = s_added;
    if (!is_dir(files_dir))
        return;
    walk(files_dir, strlen(files_dir), 0);
    fprintf(stderr, "[MODS] %s: %zu file(s)\n", files_dir, s_added - before);
}

static void apply_settings(void)
{
    char path[600], line[512];
    FILE *f;
    if (!mc3_mods_path("settings.txt", path, sizeof path) || !(f = fopen(path, "r")))
        return;
    while (fgets(line, sizeof line, f)) {
        char *k = line, *eq;
        while (*k == ' ' || *k == '\t') k++;
        if (*k == '#' || !(eq = strchr(k, '=')))
            continue;
        *eq = 0;
        eq[1 + strcspn(eq + 1, "\r\n")] = 0;
        setenv(k, eq + 1, 1);
        fprintf(stderr, "[MODS] setting %s=%s\n", k, eq + 1);
    }
    fclose(f);
}

void mc3_mods_init(const char *game_dir)
{
    const char *e = getenv("MC3_MODS_DIR");
    char path[600];

    if (e && *e) {
        snprintf(s_root, sizeof s_root, "%s", e);
    } else {
        /* Beside the disc folder: <game_dir>/../mods. */
        const char *slash = strrchr(game_dir, '/');
        size_t n = slash ? (size_t)(slash - game_dir) : 0;
        snprintf(s_root, sizeof s_root, "%.*s%smods", (int)n, game_dir, n ? "/" : "");
    }
    if (!is_dir(s_root))
        return;
    s_have_root = 1;
    fprintf(stderr, "[MODS] folder %s\n", s_root);
    apply_settings();

    /* Layers, lowest first: packs by name, then files/. */
    snprintf(path, sizeof path, "%s/packs", s_root);
    {
        DIR *d = opendir(path);
        char **names = NULL;
        size_t n = 0, i;
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                char **nn;
                if (de->d_name[0] == '.')
                    continue;
                nn = (char **)realloc(names, (n + 1) * sizeof *nn);
                if (nn) { names = nn; names[n++] = strdup(de->d_name); }
            }
            closedir(d);
        }
        qsort(names, n, sizeof *names, cmp_name);
        for (i = 0; i < n; i++) {
            snprintf(path, sizeof path, "%s/packs/%s/files", s_root, names[i]);
            layer(path);
            free(names[i]);
        }
        free(names);
    }
    snprintf(path, sizeof path, "%s/files", s_root);
    layer(path);
    qsort(s_files, s_n, sizeof *s_files, cmp_file);

    /* Textures: the renderer's replacement folder, unless set already. */
    snprintf(path, sizeof path, "%s/textures", s_root);
    if (is_dir(path) && !(getenv("RECOMP_GL_HD_DIR") && *getenv("RECOMP_GL_HD_DIR")))
        setenv("RECOMP_GL_HD_DIR", s_root, 1);   /* hd_pack.c reads <dir>/textures */
}

/* The toolkit's path hook (kernel_path.c): a disc file the mods replace. */
int xbox_path_override(const char *disc_rel, char *host_path, size_t size)
{
    char key[512];
    ModFile probe, *hit;
    static unsigned reported;

    if (!s_n || !disc_rel)
        return 0;
    while (*disc_rel == '/')
        disc_rel++;
    snprintf(key, sizeof key, "%s", disc_rel);
    lower(key);
    probe.key = key;
    hit = (ModFile *)bsearch(&probe, s_files, s_n, sizeof *s_files, cmp_file);
    if (!hit)
        return 0;
    snprintf(host_path, size, "%s", hit->host);
    if (reported++ < 200)
        fprintf(stderr, "[MODS] %s -> %s\n", disc_rel, hit->host);
    return 1;
}
