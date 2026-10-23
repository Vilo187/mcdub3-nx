/* windows_host.c -- mcdub3.exe started on its own (double-click, shortcut).
 *
 * Windows' counterpart of switch_nx.c's set-up: what run_mcdub3.bat did, so
 * the exe works without it (it did nothing when double-clicked: no disc
 * found, a console that flashed and closed, 2026-10-23).
 *
 *   settings  mcdub3_defaults.txt (the build's SCALE / UI_SCALE / SETTINGS)
 *             then mcdub3_env.txt beside the exe, NAME=value per line, #
 *             comments; the later file wins, the real environment (make run
 *             RECOMP_GL_SCALE=2, a launcher) over both
 *   the disc  MC3_GAME_DIR, else game\ beside the exe, else the repo's game\
 *             (dist\ sits in work\build-windows-<arch>\)
 *   saves     MC3_SAVE_DIR, else the repo's work\save\ if there is one
 *   the log   started without a console of its own to report to (a
 *             double-click gives it a fresh one): stdout and stderr go to
 *             work\logs\windows_run_<time>.log (else mcdub3_log.txt beside
 *             the exe) and the console closes, leaving the game's window
 *   errors    fatal() (a missing disc, another edition) in a message box
 */
#if defined(_WIN32)
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char s_exe_dir[MAX_PATH * 3];

static int is_dir(const char *p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static int is_file(const char *p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

/* Names the real environment had before the files were read. */
#define MAX_KEPT 256
static char *s_kept[MAX_KEPT];
static int s_nkept;

static int was_set_outside(const char *name)
{
    int i;
    for (i = 0; i < s_nkept; i++)
        if (!strcmp(s_kept[i], name))
            return 1;
    return 0;
}

static void remember_environment(void)
{
    LPCH env = GetEnvironmentStringsA(), p;
    if (!env)
        return;
    for (p = env; *p && s_nkept < MAX_KEPT; p += strlen(p) + 1) {
        const char *eq = strchr(p + 1, '=');    /* "=C:=C:\..." entries start with '=' */
        if (eq && (!strncmp(p, "RECOMP_", 7) || !strncmp(p, "MC3_", 4))) {
            char *name = _strdup(p);
            if (!name)
                break;
            name[eq - p] = 0;
            s_kept[s_nkept++] = name;
        }
    }
    FreeEnvironmentStringsA(env);
}

static void load_settings(const char *file)
{
    char path[sizeof s_exe_dir + 64], line[1024];
    FILE *f;
    snprintf(path, sizeof path, "%s\\%s", s_exe_dir, file);
    if (!(f = fopen(path, "r")))
        return;
    while (fgets(line, sizeof line, f)) {
        char *s = line, *eq, *e;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '#' || !(eq = strchr(s, '=')))
            continue;
        for (e = s + strlen(s); e > eq && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '); e--) { }
        *e = 0;
        *eq = 0;
        if (*s && !was_set_outside(s))
            _putenv_s(s, eq + 1);
    }
    fclose(f);
}

static void find_dir(const char *var, const char *const *cands)
{
    char p[sizeof s_exe_dir + 64];
    const char *e = getenv(var);
    if (e && *e)
        return;
    for (; *cands; cands++) {
        snprintf(p, sizeof p, "%s\\%s", s_exe_dir, *cands);
        if (is_dir(p)) {
            _putenv_s(var, p);
            return;
        }
    }
}

/* A double-click gives the exe a console of its own (only this process in
 * it): send the log to a file and close that console. Started from a
 * terminal or with its output redirected (run_mcdub3.bat, make run), it
 * leaves the output where it is. */
static void log_to_file_if_alone(void)
{
    DWORD procs[2];
    char logs[sizeof s_exe_dir + 64], path[sizeof s_exe_dir + 96], ts[32];
    time_t t = time(NULL);
    if (GetConsoleProcessList(procs, 2) != 1)
        return;
    strftime(ts, sizeof ts, "%Y%m%d_%H%M%S", localtime(&t));
    snprintf(logs, sizeof logs, "%s\\..\\..\\logs", s_exe_dir);
    if (is_dir(logs))
        snprintf(path, sizeof path, "%s\\windows_run_%s.log", logs, ts);
    else
        snprintf(path, sizeof path, "%s\\mcdub3_log.txt", s_exe_dir);
    if (freopen(path, "w", stdout) && freopen(path, "a", stderr)) {
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
        FreeConsole();
    }
}

void windows_boot(void)
{
    static const char *const game[] = { "game", "..\\..\\..\\game", NULL };
    static const char *const save[] = { "..\\..\\save", NULL };
    char *slash;
    GetModuleFileNameA(NULL, s_exe_dir, sizeof s_exe_dir);
    if ((slash = strrchr(s_exe_dir, '\\')) != NULL)
        *slash = 0;
    log_to_file_if_alone();
    remember_environment();
    load_settings("mcdub3_defaults.txt");
    load_settings("mcdub3_env.txt");
    find_dir("MC3_GAME_DIR", game);
    find_dir("MC3_SAVE_DIR", save);
}

/* fatal() in main.c: the log is not where a player looks. */
void windows_error_dialog(const char *msg)
{
    MessageBoxA(NULL, msg, "Midnight Club 3", MB_OK | MB_ICONERROR);
}
#endif
