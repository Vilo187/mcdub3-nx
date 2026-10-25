/*
 * mc3_font.c -- the TrueType font of the port's own screens (the Extras
 * menu), for the renderer's overlay (nv2a_gl_host_font).
 *
 * In order: MC3_UI_FONT (a .ttf/.otf path), the mods folder's
 * ui/font.ttf (mc3_mods.c: sdmc:/switch/mcdub3-nx/mods/ on the Switch), the
 * Switch's own system font, then common fonts on a computer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mc3_mods.h"

#ifdef __SWITCH__
const void *switch_system_font(size_t *len);   /* switch_nx.c */
#endif

static void *s_font;
static size_t s_len;

static int load(const char *path)
{
    FILE *f;
    long n;
    if (!path || !*path || !(f = fopen(path, "rb")))
        return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0 && n < (32L << 20) && (s_font = malloc((size_t)n)) != NULL
            && fread(s_font, 1, (size_t)n, f) == (size_t)n) {
        s_len = (size_t)n;
        fprintf(stderr, "[EXTRAS] menu font %s\n", path);
    } else {
        free(s_font);
        s_font = NULL;
    }
    fclose(f);
    return s_font != NULL;
}

const void *nv2a_gl_host_font(size_t *len)
{
    static const char *const desktop[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "C:\\Windows\\Fonts\\arialbd.ttf",
    };
    char mod[512];
    size_t i;

    if (!s_font) {
        if (!load(getenv("MC3_UI_FONT"))
                && !(mc3_mods_path("ui/font.ttf", mod, sizeof mod) && load(mod))
                && !(mc3_mods_path("ui/font.otf", mod, sizeof mod) && load(mod))) {
#ifdef __SWITCH__
            const void *sys = switch_system_font(&s_len);
            if (sys) {
                *len = s_len;
                return sys;
            }
#endif
            for (i = 0; i < sizeof desktop / sizeof desktop[0] && !s_font; i++)
                load(desktop[i]);
        }
    }
    *len = s_len;
    return s_font;
}
