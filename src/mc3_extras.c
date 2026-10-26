/*
 * mc3_extras.c -- the port's Extras menu: free camera, frame-rate counter,
 * picture settings and (in career mode) cheats, opened from the game's
 * pause menu.
 *
 * Drawn by the renderer's host overlay (xboxrecomp nv2a_gl/gl_overlay.c)
 * in the look of MC3's own menus: dark panels with a light rim, bold italic
 * capitals, the highlighted line orange behind a double chevron, the
 * button legend at the bottom right. Driven by the pad through the
 * toolkit's input filter (xinput_device.c): while the menu is open the game
 * sees an idle pad.
 *
 *   open    R3 (right stick click) while the pause menu is up; L3+R3
 *           anywhere (MC3_EXTRAS_COMBO=0 turns the combination off)
 *   move    d-pad or left stick; A selects, B goes back, left/right change
 *
 * What it reads and writes in the game (found 2026-10-24, NTSC-U = PAL code):
 *   0x00427B68  mcMenuPause's vtable; an object with it is the pause menu,
 *               found by a scan of guest RAM. +0x44 bit 0: the menu is
 *               active (mcMenuPause's input and update methods test it)
 *   0x00507B14  game mode (2 = career; 1 arcade, 3 replay/editor, 5 net)
 *   0x0051893C  profile manager -> +0x0C current profile -> +0x838 money
 *               (the save's "Money" field, sub_002598B0)
 *   0x00507B2C  unlock flags, one byte each: races, cities, vehicles,
 *               customisation, abilities, difficulty, performance parts
 *               (0x507B32). Set at boot by the developer parameter
 *               -unlockall (sub_00241670) and by the game's own cheat code
 *               screen (sub_001794F0); the unlocking rules read them live.
 *
 * Settings persist in mcdub3_env.txt (switch_nx.c reads it at start), so a
 * choice made here is also an ordinary documented setting.
 */
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <xbox/xboxrecomp.h>
#include "../xboxrecomp/src/input/xinput_xbox.h"
#include "../xboxrecomp/src/nv2a_gl/gl_overlay.h"
#include "mc3_extras.h"

extern ptrdiff_t g_xbox_mem_offset;
extern uint32_t nv2a_gl_frame_count(void);
void nv2a_gl_set_fxaa(int on);
void nv2a_gl_set_sharpen(float k);

/* ------------------------------------------------------------------ */
/* Guest memory                                                        */
/* ------------------------------------------------------------------ */

#define GUEST_RAM_END 0x04000000u           /* 64 MB, the retail console */

/* The game's heap is mostly in the contiguous window at 0x80000000 (the
 * runtime backs it separately: kernel.h XBOX_CONTIG_BASE, 64 MB), the rest
 * in the 64 MB from 0. Both are mapped at g_xbox_mem_offset + VA, as the
 * lifted code reads them. */
#define CONTIG_BASE 0x80000000u
#define CONTIG_END  0x84000000u

static int ram_ok(uint32_t va, uint32_t n)
{
    if (!g_xbox_mem_offset || va + n < va)
        return 0;
    return (va >= 0x00010000u && va + n <= GUEST_RAM_END)
        || (va >= CONTIG_BASE && va + n <= CONTIG_END);
}

static void *host_of(uint32_t va)
{
    return (void *)((uintptr_t)va + g_xbox_mem_offset);
}

uint32_t mc3_rd32(uint32_t va)
{
    uint32_t v = 0;
    if (ram_ok(va, 4))
        memcpy(&v, host_of(va), 4);
    return v;
}

void mc3_wr32(uint32_t va, uint32_t v)
{
    if (ram_ok(va, 4))
        memcpy(host_of(va), &v, 4);
}

static uint8_t rd8(uint32_t va)
{
    return ram_ok(va, 1) ? *(volatile uint8_t *)host_of(va) : 0;
}

static void wr8(uint32_t va, uint8_t v)
{
    if (ram_ok(va, 1))
        *(volatile uint8_t *)host_of(va) = v;
}

/* ------------------------------------------------------------------ */
/* The game's state                                                    */
/* ------------------------------------------------------------------ */

#define MC3_VT_MENU_PAUSE   0x00427B68u
#define MC3_MENU_FLAGS      0x44
#define MC3_GAME_MODE       0x00507B14u
#define MC3_MODE_CAREER     2
#define MC3_MODE_CAREER_GARAGE 5         /* set right after 2 when a career starts */
#define MC3_PROFILES        0x0051893Cu
#define MC3_PROFILE_MONEY   0x838
#define MC3_UNLOCK_FIRST    0x00507B2Cu
#define MC3_UNLOCK_COUNT    7
#define MC3_MONEY_MAX       9999999u

/* The pause menu object, found once by its vtable (the object is made at
 * boot with the other menus and lives as long as the game). */
static volatile uint32_t s_pause_obj;

static uint32_t scan_range(uint32_t from, uint32_t to)
{
    const uint32_t *p = (const uint32_t *)host_of(from);
    uint32_t n = (to - from) / 4, i;
    static int told;
    for (i = 0; i < n; i++)
        if (p[i] == MC3_VT_MENU_PAUSE) {
            uint32_t va = from + i * 4;
            /* The constructor clears +0x1E0 (a bool); the flags word at +0x44
             * is small. A stray copy of the pointer elsewhere fails these. */
            if (rd8(va + 0x1E0) <= 1 && mc3_rd32(va + MC3_MENU_FLAGS) < 0x10000u)
                return va;
            if (told++ < 8)
                fprintf(stderr, "[EXTRAS] pause menu candidate %08X refused: +0x44 %08X +0x1E0 %02X\n",
                        va, mc3_rd32(va + MC3_MENU_FLAGS), rd8(va + 0x1E0));
        }
    return 0;
}

static uint32_t find_pause_menu(void)
{
    /* The contiguous window first (where the game's heap lives), then the
     * low RAM past the XBE image (base 0x10000, 5.3 MB). */
    uint32_t o = scan_range(CONTIG_BASE, CONTIG_END);
    return o ? o : scan_range(0x00560000u, GUEST_RAM_END);
}

int mc3_game_paused(void)
{
    uint32_t o = s_pause_obj;
    if (!o || mc3_rd32(o) != MC3_VT_MENU_PAUSE)
        return 0;
    return (mc3_rd32(o + MC3_MENU_FLAGS) & 1u) != 0;
}

int mc3_in_career(void)
{
    uint32_t m = mc3_rd32(MC3_GAME_MODE);
    return m == MC3_MODE_CAREER || m == MC3_MODE_CAREER_GARAGE;
}

/* The current profile's money field, or 0 when there is no profile. */
static uint32_t money_va(void)
{
    uint32_t mgr = mc3_rd32(MC3_PROFILES), prof;
    if (!ram_ok(mgr, 0x10))
        return 0;
    prof = mc3_rd32(mgr + 0x0C);
    if (!ram_ok(prof, MC3_PROFILE_MONEY + 4))
        return 0;
    return prof + MC3_PROFILE_MONEY;
}

/* ------------------------------------------------------------------ */
/* Settings                                                            */
/* ------------------------------------------------------------------ */

static struct {
    int   fps;              /* MC3_FPS_COUNTER */
    int   fxaa;             /* RECOMP_GL_FXAA */
    float sharpen;          /* RECOMP_GL_SHARPEN */
    float scale;            /* RECOMP_GL_SCALE (restart) */
    int   aniso;            /* RECOMP_GL_ANISO (restart) */
    int   unlock;           /* MC3_CHEAT_UNLOCK */
    int   money;            /* MC3_CHEAT_MONEY */
    int   traffic;          /* MC3_TRAFFIC: city traffic */
    int   peds;             /* MC3_PEDS: pedestrians */
    int   props;            /* MC3_PROPS: street props */
    int   weather;          /* MC3_WEATHER: 0 game's, 1 clear, 2 cloudy, 3 rainy */
    int   slowmo;           /* MC3_SLOWMO: game speed in percent, 100 = normal */
    int   devmenus;         /* developer parameters, through MC3_ARGS (restart) */
    int   cops;
    int   skipintro;
    int   fps60;            /* MC3_FPS60: 0 the game's 30, 1 60 while driving, 2 60 everywhere */
} S;

static float env_f(const char *k, float def)
{
    const char *e = getenv(k);
    return e && *e ? (float)atof(e) : def;
}

/* ------------------------------------------------------------------ */
/* Vehicle switcher                                                    */
/* ------------------------------------------------------------------ */

/* Every player vehicle of the game (the "_base" models in assets_x.dat),
 * the police cars and bike included, which career never sells. */
static const char *const VEHICLES[] = {
    "vp_350z_04", "vp_aprilia_mille_04", "vp_belair_57", "vp_cad16_04", "vp_charger_69",
    "vp_charger_99", "vp_chrysler300c_05", "vp_cien_02", "vp_cl500_04", "vp_cl55_amg_04",
    "vp_clk_gtr_03", "vp_corvette_68", "vp_corvettez06_03", "vp_ctsv_04", "vp_cuevito_99",
    "vp_delegance_03", "vp_ducati_999r_04", "vp_ducati_ss1000_04", "vp_eclipse_04",
    "vp_elcamino_70", "vp_elise_04", "vp_escalade_03", "vp_esprit_04", "vp_ext_03",
    "vp_fleetline_49", "vp_g500_03", "vp_g55_amg_03", "vp_gallardo_04", "vp_gemballa_04",
    "vp_golfr32_04", "vp_gs430_04", "vp_gto_68", "vp_gto_70", "vp_h2_03", "vp_hummer_02",
    "vp_impala_64", "vp_impalass_96", "vp_impalass_cop_a_96", "vp_impalass_cop_d_96",
    "vp_impalass_cop_s_96", "vp_is300_04", "vp_jetta_03", "vp_kwz_cop_98", "vp_lancer_04",
    "vp_mclaren_f1_95", "vp_me412_04", "vp_monster_sr4_04", "vp_montecarlo_78",
    "vp_murcielago_04", "vp_ninja_03", "vp_phaeton_04", "vp_ram_04", "vp_saleens7_04",
    "vp_saleen_sr_00", "vp_sc430_04", "vp_silverado_ss_04", "vp_skully_01", "vp_skyline_02",
    "vp_sl500_04", "vp_sl55_amg_04", "vp_slr_mclaren_04", "vp_srt4_04", "vp_supra_98",
    "vp_viper_03", "vp_wcc_cfl_03", "vp_wcc_eldiablo_r_03", "vp_wcc_eldiablo_st_00",
    "vp_z28_69", "vp_z28_81",
};
#define NVEHICLES ((int)(sizeof VEHICLES / sizeof VEHICLES[0]))

static int s_vehicle;                       /* 0 = the game's, else VEHICLES[i - 1] */

/* MC3_VEHICLE=<vp_name>: the switcher's choice, kept across starts. */
static void vehicle_from_env(void)
{
    const char *v = getenv("MC3_VEHICLE");
    int k;
    s_vehicle = 0;
    for (k = 0; v && *v && k < NVEHICLES; k++)
        if (!strcmp(v, VEHICLES[k])) s_vehicle = k + 1;
}

/* "vp_slr_mclaren_04" -> "SLR MCLAREN '04". */
static void vehicle_label(int v, char *out, size_t n)
{
    const char *p = VEHICLES[v - 1] + 3;
    size_t o = 0, len = strlen(p);
    const char *year = len > 3 && p[len - 3] == '_' ? p + len - 2 : NULL;
    for (; *p && o + 2 < n && (!year || p < year - 1); p++)
        out[o++] = *p == '_' ? ' ' : *p;
    if (year && o + 5 < n)
        o += (size_t)snprintf(out + o, n - o, " '%s", year);
    out[o] = 0;
}

/* recomp_manual.c, before a player set-up builds its car (which: 0 the
 * set-up that marks itself +0xE0 = 1, 1 the one that clears it, 2 and 3
 * the virtual ones). The local player's name is replaced while a vehicle is
 * chosen; the original comes back when the choice is undone. */
#define VEH_NAME_MAX 32
static struct { uint32_t va; char orig[VEH_NAME_MAX]; } s_veh_seen[8];

void mc3_vehicle_hook(int which, uint32_t name_va)
{
    static int trace = -1, mask = -1;
    char name[VEH_NAME_MAX];
    int k, slot = -1;
    if (trace < 0) {
        trace = (int)env_f("MC3_VEHICLE_TRACE", 0);
        mask = (int)env_f("MC3_VEHICLE_HOOKS", 3);     /* set-ups that build the local player (0 and 1; 1 in a career race) */
    }
    if (!ram_ok(name_va, VEH_NAME_MAX))
        return;
    memcpy(name, host_of(name_va), VEH_NAME_MAX);
    name[VEH_NAME_MAX - 1] = 0;
    if (trace)
        fprintf(stderr, "[VEHICLE] set-up %d at %08X: %s (+0xE0 = %u)\n", which, name_va - 4,
                name, rd8(name_va - 4 + 0xE0));
    if (!(mask & (1 << which)))
        return;
    for (k = 0; k < 8; k++)
        if (s_veh_seen[k].va == name_va) { slot = k; break; }
    if (slot < 0) {
        for (k = 0; k < 8 && s_veh_seen[k].va; k++) { }
        slot = k < 8 ? k : 0;
        s_veh_seen[slot].va = name_va;
        memcpy(s_veh_seen[slot].orig, name, VEH_NAME_MAX);
    } else if (!strncmp(name, "vp_", 3)
               && !(s_vehicle && !strcmp(name, VEHICLES[s_vehicle - 1]))) {
        /* A name we did not put there: the game chose a new car. */
        memcpy(s_veh_seen[slot].orig, name, VEH_NAME_MAX);
    }
    if (s_vehicle > 0 && s_vehicle <= NVEHICLES) {
        snprintf((char *)host_of(name_va), VEH_NAME_MAX, "%s", VEHICLES[s_vehicle - 1]);
        if (trace)
            fprintf(stderr, "[VEHICLE]   -> %s\n", VEHICLES[s_vehicle - 1]);
    } else if (strcmp(name, s_veh_seen[slot].orig)) {
        snprintf((char *)host_of(name_va), VEH_NAME_MAX, "%s", s_veh_seen[slot].orig);
    }
}

/* Is "-name" among MC3_ARGS? */
static int arg_on(const char *name)
{
    const char *a = getenv("MC3_ARGS"), *p;
    size_t n = strlen(name);
    for (p = a; p && (p = strstr(p, name)) != NULL; p += n)
        if (p > a && p[-1] == '-' && (p[n] == 0 || p[n] == ' ' || p[n] == '='))
            return 1;
    return 0;
}

static void settings_load(void)
{
    S.fps = (int)env_f("MC3_FPS_COUNTER", 0);
    S.fxaa = (int)env_f("RECOMP_GL_FXAA", 1);
    S.sharpen = env_f("RECOMP_GL_SHARPEN", 0.5f);
    S.scale = env_f("RECOMP_GL_SCALE", 1.0f);
    S.aniso = (int)env_f("RECOMP_GL_ANISO", 16);
    S.unlock = (int)env_f("MC3_CHEAT_UNLOCK", 0);
    S.money = (int)env_f("MC3_CHEAT_MONEY", 0);
    S.traffic = (int)env_f("MC3_TRAFFIC", 1);
    S.peds = (int)env_f("MC3_PEDS", 1);
    S.props = (int)env_f("MC3_PROPS", 1);
    S.weather = (int)env_f("MC3_WEATHER", 0);
    S.slowmo = (int)env_f("MC3_SLOWMO", 100);
    S.devmenus = arg_on("devmenus");
    S.cops = arg_on("forcecops");
    S.skipintro = arg_on("skipintro");
    S.fps60 = (int)env_f("MC3_FPS60", 0);
    vehicle_from_env();
}

/* The developer parameters the menu keeps in MC3_ARGS (others there stay). */
static void args_compose(char *out, size_t n)
{
    static const char *const ours[] = { "devmenus", "forcecops", "skipintro" };
    const char *a = getenv("MC3_ARGS");
    char buf[512], *tok, *save;
    size_t o = 0, k;
    out[0] = 0;
    if (a) {
        snprintf(buf, sizeof buf, "%s", a);
        for (tok = strtok_r(buf, " \t", &save); tok; tok = strtok_r(NULL, " \t", &save)) {
            for (k = 0; k < 3; k++)
                if (tok[0] == '-' && !strcmp(tok + 1, ours[k]))
                    break;
            if (k == 3 && o + strlen(tok) + 2 < n)
                o += (size_t)snprintf(out + o, n - o, "%s%s", o ? " " : "", tok);
        }
    }
    if (S.devmenus) o += (size_t)snprintf(out + o, n - o, "%s-devmenus", o ? " " : "");
    if (S.cops) o += (size_t)snprintf(out + o, n - o, "%s-forcecops", o ? " " : "");
    if (S.skipintro) o += (size_t)snprintf(out + o, n - o, "%s-skipintro", o ? " " : "");
}

/* Where settings are kept: the Switch's settings file, or MC3_SETTINGS_FILE
 * on a computer (none by default: there the environment is the setting). */
static const char *settings_path(void)
{
    const char *e = getenv("MC3_SETTINGS_FILE");
    if (e && *e)
        return e;
#ifdef __SWITCH__
    return "sdmc:/switch/" MC3_SD_DIR "/" MC3_APP "_env.txt";
#else
    return NULL;
#endif
}

/* Rewrite our keys in the settings file: an existing KEY= line (commented
 * or not) is replaced, the rest appended; every other line stays. */
#define NKEYS 15
static int settings_save(void)
{
    static const char *keys[NKEYS] = {
        "MC3_FPS_COUNTER", "RECOMP_GL_FXAA", "RECOMP_GL_SHARPEN", "RECOMP_GL_SCALE",
        "RECOMP_GL_ANISO", "MC3_CHEAT_UNLOCK", "MC3_CHEAT_MONEY", "MC3_TRAFFIC", "MC3_PEDS",
        "MC3_PROPS", "MC3_WEATHER", "MC3_SLOWMO", "MC3_ARGS", "MC3_VEHICLE", "MC3_FPS60" };
    char vals[NKEYS][512];
    int done[NKEYS] = { 0 }, k;
    const char *path = settings_path();
    char *buf = NULL, *line, *save;
    size_t len = 0;
    FILE *f;

    snprintf(vals[0], 32, "%d", S.fps);
    snprintf(vals[1], 32, "%d", S.fxaa);
    snprintf(vals[2], 32, "%.1f", S.sharpen);
    snprintf(vals[3], 32, "%.2f", S.scale);
    snprintf(vals[4], 32, "%d", S.aniso);
    snprintf(vals[5], 32, "%d", S.unlock);
    snprintf(vals[6], 32, "%d", S.money);
    snprintf(vals[7], 32, "%d", S.traffic);
    snprintf(vals[8], 32, "%d", S.peds);
    snprintf(vals[9], 32, "%d", S.props);
    snprintf(vals[10], 32, "%d", S.weather);
    snprintf(vals[11], 32, "%d", S.slowmo);
    args_compose(vals[12], sizeof vals[12]);
    snprintf(vals[13], 64, "%s", s_vehicle > 0 ? VEHICLES[s_vehicle - 1] : "");
    snprintf(vals[14], 32, "%d", S.fps60);
    for (k = 0; k < NKEYS; k++)
        setenv(keys[k], vals[k], 1);
    if (!path)
        return 0;
    if ((f = fopen(path, "rb")) != NULL) {
        fseek(f, 0, SEEK_END);
        len = (size_t)ftell(f);
        fseek(f, 0, SEEK_SET);
        buf = (char *)calloc(1, len + 1);
        if (buf && fread(buf, 1, len, f) != len)
            len = 0;
        fclose(f);
    }
    if (!(f = fopen(path, "wb"))) {
        free(buf);
        return 0;
    }
    for (line = buf ? strtok_r(buf, "\n", &save) : NULL; line; line = strtok_r(NULL, "\n", &save)) {
        const char *p = line;
        size_t ll = strlen(line);
        if (ll && line[ll - 1] == '\r')
            line[--ll] = 0;
        while (*p == '#' || *p == ' ')
            p++;
        for (k = 0; k < NKEYS; k++) {
            size_t kl = strlen(keys[k]);
            if (!strncmp(p, keys[k], kl) && p[kl] == '=')
                break;
        }
        if (k < NKEYS) {
            if (!done[k]++)
                fprintf(f, "%s=%s\n", keys[k], vals[k]);
        } else {
            fprintf(f, "%s\n", line);
        }
    }
    for (k = 0; k < NKEYS; k++)
        if (!done[k])
            fprintf(f, "%s=%s\n", keys[k], vals[k]);
    fclose(f);
    free(buf);
    return 1;
}

/* ------------------------------------------------------------------ */
/* The game's world and clock                                          */
/* ------------------------------------------------------------------ */

/* World switches: what -noprops, -notraffic/-noambients, -nopeds and -noai
 * clear at boot (sub_00241670), copied into the game state (0x00507B24) when
 * a session starts (sub_00241120: +0x106 props, +0x107 traffic, +0x108
 * pedestrians). Set in both: the state now, the switch for every later
 * session. Fewer cars and people is less CPU too. */
#define MC3_SW_PROPS    0x0047CD40u
#define MC3_SW_TRAFFIC  0x0047CD41u
#define MC3_SW_PEDS     0x0047CD42u
#define MC3_STATE       0x00507B24u
/* Weather by index into {clear, cloudy, rainy} (0x0047CDB0), kept at
 * state +8 (sub_00242FF0, the -weather= parameter); used by the next race. */
#define MC3_STATE_WEATHER 0x08

/* Off is held (each poll, against a session start copying the switch);
 * back on is written once (once_on), then the game decides again. */
static void world_apply(int once_on)
{
    uint32_t st = mc3_rd32(MC3_STATE);
    const uint32_t sw[3] = { MC3_SW_PROPS, MC3_SW_TRAFFIC, MC3_SW_PEDS };
    const int on[3] = { S.props, S.traffic, S.peds };
    int k, st_ok = st && ram_ok(st, 0x110);
    for (k = 0; k < 3; k++) {
        if (on[k] && !once_on)
            continue;
        if (rd8(sw[k]) != (uint8_t)on[k])
            wr8(sw[k], (uint8_t)on[k]);
        if (st_ok && rd8(st + 0x106 + k) != (uint8_t)on[k])
            wr8(st + 0x106 + k, (uint8_t)on[k]);
    }
    if (S.weather && st && ram_ok(st, 0x10)
            && mc3_rd32(st + MC3_STATE_WEATHER) != (uint32_t)(S.weather - 1))
        mc3_wr32(st + MC3_STATE_WEATHER, (uint32_t)(S.weather - 1));
}

/* The game steps the world by its frame time times a time factor
 * (0x0047C400, 1.0; sub_001EDBE0 -> step 0x0047C3DC), in races by a fixed
 * step (recomp_manual.c, sub_001EDBE0, which also does the 60 fps mode).
 * The free camera holds the world still with a factor of 1e-5, slow motion
 * scales it; the game's own factor comes back when neither is on. */
#define TIME_SCALE 0x0047C400u
static uint32_t s_scale0;
static int      s_scale_saved, s_dt_freeze;
extern volatile int g_mc3_fps60;            /* recomp_manual.c: flips at interval 1 */
extern volatile int g_nv2a_flip_ahead;      /* nv2a_pb_exec.c */

void mc3_time_freeze(int on)
{
    s_dt_freeze = on;
}

static void time_apply(void)
{
    float k;
    uint32_t a;
    g_mc3_fps60 = S.fps60 == 2 || (S.fps60 == 1 && mc3_freecam_ready() && !mc3_game_paused());
    {
        /* Triple buffering at 60 fps (nv2a_pb_exec.c): a frame a little
         * over 16.7 ms then costs that, not a whole second vblank. At 30 fps
         * the game's own pacing stays as on the console. MC3_FLIP_AHEAD=0:
         * double buffering at 60 too; -1: the executor's old protocol. */
        static int ahead = -2;
        if (ahead == -2) {
            const char *e = getenv("MC3_FLIP_AHEAD");
            ahead = e && *e ? atoi(e) : 1;
            if (ahead > 1)
                ahead = 1;                  /* D3D's flip queue holds two */
        }
        g_nv2a_flip_ahead = ahead < 0 ? -1 : g_mc3_fps60 ? ahead : 0;
    }
    {
        /* With the 60 fps setting on, a 120 Hz virtual vblank: flips retire
         * on an 8.3 ms grid (intervals are doubled to match, recomp_manual.c
         * sub_003E52D0), so the game and the executor wait half as long for
         * a frame that just missed one. MC3_VBLANK_120=0: 60 Hz. */
        extern volatile int g_vblank_mult;          /* kernel_bridge.c */
        static int on = -1;
        if (on < 0) {
            const char *e = getenv("MC3_VBLANK_120");
            on = !(e && *e == '0');
        }
        g_vblank_mult = on && S.fps60 ? 2 : 1;
    }
    if (s_dt_freeze)
        k = 0.00001f;
    else if (S.slowmo > 0 && S.slowmo < 100)
        k = (float)S.slowmo / 100.0f;
    else {
        if (s_scale_saved) {
            mc3_wr32(TIME_SCALE, s_scale0);
            s_scale_saved = 0;
        }
        return;
    }
    if (!s_scale_saved) {
        s_scale0 = mc3_rd32(TIME_SCALE);
        s_scale_saved = 1;
    }
    memcpy(&a, &k, 4);
    if (mc3_rd32(TIME_SCALE) != a)
        mc3_wr32(TIME_SCALE, a);
}

/* ------------------------------------------------------------------ */
/* Text (English, German with RECOMP_LANGUAGE=de)                      */
/* ------------------------------------------------------------------ */

enum {
    T_EXTRAS, T_PAUSE, T_FREECAM, T_FPS, T_GRAPHICS, T_CHEATS, T_CLOSE,
    T_RES, T_FXAA, T_SHARPEN, T_ANISO, T_SAVE, T_RESTART, T_SAVED, T_NOTSAVED,
    T_UNLOCK, T_MONEY, T_ADDMONEY, T_ON, T_OFF, T_ACCEPT, T_BACK, T_HINT,
    T_CAREER_ONLY, T_FREECAM_HELP, T_NA,
    T_WORLD, T_TRAFFIC, T_PEDS, T_PROPS, T_WEATHER, T_SPEED, T_DEV, T_DEVMENUS, T_COPS,
    T_SKIPINTRO, T_W_GAME, T_W_CLEAR, T_W_CLOUDY, T_W_RAINY, T_NEXTLOAD, T_VEHICLE,
    T_FRAMERATE, T_FR_30, T_FR_60DRIVE, T_FR_60ALL, T_APPLYVEH, T_GARAGE_FAIL, T_COUNT
};

static const char *const TXT_EN[T_COUNT] = {
    "Extras", "Pause", "Free Camera", "Frame Rate Counter", "Graphics", "Cheats", "Resume",
    "Resolution", "Anti-Aliasing", "Sharpening", "Texture Filtering", "Save Settings",
    "Takes effect after a restart", "Settings saved", "Saved for this session",
    "Unlock All", "Infinite Money", "Add $100,000", "On", "Off", "Accept", "Back",
    "Extras", "Cheats are available in Career mode",
    "Left stick / D-pad: move   Right stick: look   LT/RT: down/up   A: faster   B: exit", "N/A",
    "World", "Traffic", "Pedestrians", "Street Props", "Weather", "Game Speed", "Developer",
    "Developer Menus", "Cops Everywhere", "Skip Intro Movies", "Game's Choice", "Clear",
    "Cloudy", "Rain", "From the next race or city load", "Vehicle",
    "Frame Rate", "30 FPS", "60 FPS Driving", "60 FPS Always",
    "Apply Vehicle Now (via Garage)", "The pause menu did not answer",
};

static const char *const TXT_DE[T_COUNT] = {
    "Extras", "Pause", "Freie Kamera", "Bildratenanzeige", "Grafik", "Cheats", "Fortsetzen",
    "Auflösung", "Kantenglättung", "Schärfe", "Texturfilterung", "Einstellungen speichern",
    "Wird nach einem Neustart aktiv", "Einstellungen gespeichert", "Für diese Sitzung gespeichert",
    "Alles freischalten", "Unendlich Geld", "$100.000 hinzufügen", "An", "Aus", "OK", "Zurück",
    "Extras", "Cheats gibt es im Karrieremodus",
    "Linker Stick / Steuerkreuz: fliegen   Rechter Stick: schauen   LT/RT: runter/hoch   A: schneller   B: Ende", "--",
    "Welt", "Verkehr", "Fußgänger", "Straßenobjekte", "Wetter", "Spieltempo", "Entwickler",
    "Entwicklermenüs", "Überall Polizei", "Intro-Videos überspringen", "Wie im Spiel", "Klar",
    "Bewölkt", "Regen", "Ab dem nächsten Rennen oder Stadtladen", "Fahrzeug",
    "Bildrate", "30 FPS", "60 FPS beim Fahren", "60 FPS immer",
    "Fahrzeug jetzt laden (über Garage)", "Das Pausenmenü hat nicht reagiert",
};

static const char *const *s_txt = TXT_EN;
#define T(id) (s_txt[id])

/* ------------------------------------------------------------------ */
/* Menu model                                                          */
/* ------------------------------------------------------------------ */

enum { PAGE_MAIN, PAGE_GRAPHICS, PAGE_CHEATS, PAGE_WORLD, PAGE_DEV };
enum {
    IT_FREECAM, IT_FPS, IT_GRAPHICS, IT_CHEATS, IT_CLOSE,
    IT_RES, IT_FXAA, IT_SHARPEN, IT_ANISO, IT_SAVE,
    IT_UNLOCK, IT_MONEY, IT_ADDMONEY,
    IT_WORLD, IT_DEV, IT_TRAFFIC, IT_PEDS, IT_PROPS, IT_WEATHER, IT_SPEED,
    IT_DEVMENUS, IT_COPS, IT_SKIPINTRO, IT_VEHICLE, IT_FRAMERATE, IT_APPLYVEH,
};

static const int SPEEDS[] = { 100, 75, 50, 25 };

static const float SCALES[] = { 0.75f, 1.0f, 1.25f, 1.5f };
static const int   ANISOS[] = { 1, 2, 4, 8, 16 };

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static int  s_open;                 /* the menu is up */
static int  s_page, s_sel;
static char s_toast[96];
static double s_toast_until;
static int  s_freecam;              /* mc3_freecam.c owns the camera */
static int  s_macro = -1;           /* the garage round trip's step, -1 when idle */


static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void toast(const char *msg)
{
    snprintf(s_toast, sizeof s_toast, "%s", msg);
    s_toast_until = now_s() + 2.5;
}

/* The items of a page, in order. Cheats only in career mode; the free
 * camera only where there is a world to fly through (mc3_freecam_ready). */
static int page_items(int page, int *out)
{
    int n = 0;
    switch (page) {
    case PAGE_MAIN:
        out[n++] = IT_FREECAM;
        out[n++] = IT_FPS;
        out[n++] = IT_VEHICLE;
        if (mc3_in_career() && mc3_game_paused())
            out[n++] = IT_APPLYVEH;
        out[n++] = IT_WORLD;
        out[n++] = IT_GRAPHICS;
        out[n++] = IT_DEV;
        if (mc3_in_career())
            out[n++] = IT_CHEATS;
        out[n++] = IT_CLOSE;
        break;
    case PAGE_WORLD:
        out[n++] = IT_TRAFFIC; out[n++] = IT_PEDS; out[n++] = IT_PROPS;
        out[n++] = IT_WEATHER; out[n++] = IT_SPEED; out[n++] = IT_SAVE;
        break;
    case PAGE_DEV:
        out[n++] = IT_DEVMENUS; out[n++] = IT_COPS; out[n++] = IT_SKIPINTRO; out[n++] = IT_SAVE;
        break;
    case PAGE_GRAPHICS:
        out[n++] = IT_FRAMERATE;
        out[n++] = IT_RES; out[n++] = IT_FXAA; out[n++] = IT_SHARPEN;
        out[n++] = IT_ANISO; out[n++] = IT_SAVE;
        break;
    case PAGE_CHEATS:
        out[n++] = IT_UNLOCK; out[n++] = IT_MONEY; out[n++] = IT_ADDMONEY;
        break;
    }
    return n;
}

static const char *item_label(int it)
{
    switch (it) {
    case IT_FREECAM: return T(T_FREECAM);
    case IT_FPS: return T(T_FPS);
    case IT_GRAPHICS: return T(T_GRAPHICS);
    case IT_CHEATS: return T(T_CHEATS);
    case IT_CLOSE: return T(T_CLOSE);
    case IT_RES: return T(T_RES);
    case IT_FXAA: return T(T_FXAA);
    case IT_SHARPEN: return T(T_SHARPEN);
    case IT_ANISO: return T(T_ANISO);
    case IT_SAVE: return T(T_SAVE);
    case IT_UNLOCK: return T(T_UNLOCK);
    case IT_MONEY: return T(T_MONEY);
    case IT_ADDMONEY: return T(T_ADDMONEY);
    case IT_WORLD: return T(T_WORLD);
    case IT_DEV: return T(T_DEV);
    case IT_TRAFFIC: return T(T_TRAFFIC);
    case IT_PEDS: return T(T_PEDS);
    case IT_PROPS: return T(T_PROPS);
    case IT_WEATHER: return T(T_WEATHER);
    case IT_SPEED: return T(T_SPEED);
    case IT_DEVMENUS: return T(T_DEVMENUS);
    case IT_COPS: return T(T_COPS);
    case IT_SKIPINTRO: return T(T_SKIPINTRO);
    case IT_VEHICLE: return T(T_VEHICLE);
    case IT_FRAMERATE: return T(T_FRAMERATE);
    case IT_APPLYVEH: return T(T_APPLYVEH);
    }
    return "";
}

/* The value shown at the right of an item ("" for none). */
static void item_value(int it, char *buf, size_t n)
{
    buf[0] = 0;
    switch (it) {
    case IT_FREECAM:
        snprintf(buf, n, "%s", mc3_freecam_ready() ? (s_freecam ? T(T_ON) : T(T_OFF)) : T(T_NA));
        break;
    case IT_FPS: snprintf(buf, n, "%s", S.fps ? T(T_ON) : T(T_OFF)); break;
    case IT_RES:
        snprintf(buf, n, "%dx%d", (int)lroundf(720.0f * S.scale), (int)lroundf(480.0f * S.scale));
        break;
    case IT_FXAA: snprintf(buf, n, "%s", S.fxaa ? "FXAA" : T(T_OFF)); break;
    case IT_SHARPEN: snprintf(buf, n, "%d%%", (int)lroundf(S.sharpen * 100.0f)); break;
    case IT_ANISO:
        if (S.aniso <= 1) snprintf(buf, n, "%s", T(T_OFF));
        else snprintf(buf, n, "%dx", S.aniso);
        break;
    case IT_UNLOCK: snprintf(buf, n, "%s", S.unlock ? T(T_ON) : T(T_OFF)); break;
    case IT_MONEY: snprintf(buf, n, "%s", S.money ? T(T_ON) : T(T_OFF)); break;
    case IT_TRAFFIC: snprintf(buf, n, "%s", S.traffic ? T(T_ON) : T(T_OFF)); break;
    case IT_PEDS: snprintf(buf, n, "%s", S.peds ? T(T_ON) : T(T_OFF)); break;
    case IT_PROPS: snprintf(buf, n, "%s", S.props ? T(T_ON) : T(T_OFF)); break;
    case IT_WEATHER: {
        static const int w[4] = { T_W_GAME, T_W_CLEAR, T_W_CLOUDY, T_W_RAINY };
        snprintf(buf, n, "%s", T(w[S.weather & 3]));
        break;
    }
    case IT_SPEED: snprintf(buf, n, "%d%%", S.slowmo > 0 && S.slowmo <= 100 ? S.slowmo : 100); break;
    case IT_DEVMENUS: snprintf(buf, n, "%s", S.devmenus ? T(T_ON) : T(T_OFF)); break;
    case IT_COPS: snprintf(buf, n, "%s", S.cops ? T(T_ON) : T(T_OFF)); break;
    case IT_SKIPINTRO: snprintf(buf, n, "%s", S.skipintro ? T(T_ON) : T(T_OFF)); break;
    case IT_FRAMERATE: {
        static const int fr[3] = { T_FR_30, T_FR_60DRIVE, T_FR_60ALL };
        snprintf(buf, n, "%s", T(fr[S.fps60 >= 0 && S.fps60 <= 2 ? S.fps60 : 0]));
        break;
    }
    case IT_VEHICLE:
        if (s_vehicle > 0) vehicle_label(s_vehicle, buf, n);
        else snprintf(buf, n, "%s", T(T_W_GAME));
        break;
    default: break;
    }
}

static int idx_of_f(const float *a, int n, float v)
{
    int i, best = 0;
    for (i = 0; i < n; i++)
        if (fabsf(a[i] - v) < fabsf(a[best] - v)) best = i;
    return best;
}

static int idx_of_i(const int *a, int n, int v)
{
    int i, best = 0;
    for (i = 0; i < n; i++)
        if (abs(a[i] - v) < abs(a[best] - v)) best = i;
    return best;
}

static void apply_cheats(void);

/* Left (-1) / right (+1) / select (0) on an item. */
static void item_act(int it, int dir)
{
    switch (it) {
    case IT_FREECAM:
        if (!mc3_freecam_ready())
            break;
        s_freecam = !s_freecam;
        mc3_freecam_enable(s_freecam);
        if (s_freecam)
            s_open = 0;                         /* straight into the camera */
        break;
    case IT_FPS: S.fps = !S.fps; break;
    case IT_GRAPHICS: if (!dir) { s_page = PAGE_GRAPHICS; s_sel = 0; } break;
    case IT_CHEATS: if (!dir) { s_page = PAGE_CHEATS; s_sel = 0; } break;
    case IT_CLOSE: if (!dir) s_open = 0; break;
    case IT_RES: {
        int i = idx_of_f(SCALES, 4, S.scale) + (dir ? dir : 1);
        S.scale = SCALES[(i + 4) % 4];
        toast(T(T_RESTART));
        break;
    }
    case IT_FXAA: S.fxaa = !S.fxaa; nv2a_gl_set_fxaa(S.fxaa); break;
    case IT_SHARPEN: {
        float k = S.sharpen + (dir < 0 ? -0.1f : 0.1f);
        if (k > 1.05f) k = 0.0f;
        if (k < -0.05f) k = 1.0f;
        S.sharpen = roundf(k * 10.0f) / 10.0f;
        nv2a_gl_set_sharpen(S.sharpen);
        break;
    }
    case IT_ANISO: {
        int i = idx_of_i(ANISOS, 5, S.aniso) + (dir ? dir : 1);
        S.aniso = ANISOS[(i + 5) % 5];
        toast(T(T_RESTART));
        break;
    }
    case IT_SAVE:
        if (!dir)
            toast(settings_save() ? T(T_SAVED) : T(T_NOTSAVED));
        break;
    case IT_WORLD: if (!dir) { s_page = PAGE_WORLD; s_sel = 0; } break;
    case IT_DEV: if (!dir) { s_page = PAGE_DEV; s_sel = 0; } break;
    case IT_TRAFFIC: S.traffic = !S.traffic; world_apply(1); toast(T(T_NEXTLOAD)); break;
    case IT_PEDS: S.peds = !S.peds; world_apply(1); toast(T(T_NEXTLOAD)); break;
    case IT_PROPS: S.props = !S.props; world_apply(1); toast(T(T_NEXTLOAD)); break;
    case IT_WEATHER:
        S.weather = (S.weather + (dir < 0 ? 3 : 1)) & 3;
        world_apply(0);
        toast(T(T_NEXTLOAD));
        break;
    case IT_SPEED: {
        int i = idx_of_i(SPEEDS, 4, S.slowmo) + (dir < 0 ? 3 : 1);
        S.slowmo = SPEEDS[i % 4];
        break;
    }
    case IT_DEVMENUS: S.devmenus = !S.devmenus; toast(T(T_RESTART)); break;
    case IT_COPS: S.cops = !S.cops; toast(T(T_RESTART)); break;
    case IT_SKIPINTRO: S.skipintro = !S.skipintro; toast(T(T_RESTART)); break;
    case IT_FRAMERATE: S.fps60 = (S.fps60 + (dir < 0 ? 2 : 1)) % 3; break;
    case IT_VEHICLE:
        s_vehicle = (s_vehicle + (dir < 0 ? NVEHICLES : 1)) % (NVEHICLES + 1);
        toast(T(T_NEXTLOAD));
        break;
    case IT_APPLYVEH:
        if (!dir && mc3_in_career() && mc3_game_paused()) {
            s_open = 0;
            s_macro = 0;
        }
        break;
    case IT_UNLOCK: S.unlock = !S.unlock; apply_cheats(); break;
    case IT_MONEY: S.money = !S.money; apply_cheats(); break;
    case IT_ADDMONEY:
        if (!dir && mc3_in_career()) {
            uint32_t va = money_va();
            if (va) {
                uint32_t m = mc3_rd32(va) + 100000u;
                mc3_wr32(va, m > MC3_MONEY_MAX ? MC3_MONEY_MAX : m);
            }
        }
        break;
    }
}

/* Cheats hold while they are on and the game is in career mode: the
 * unlock flags (re-set: a career load may clear them) and the money. */
static void apply_cheats(void)
{
    int i;
    if (!mc3_in_career())
        return;
    if (S.unlock)
        for (i = 0; i < MC3_UNLOCK_COUNT; i++)
            if (!rd8(MC3_UNLOCK_FIRST + i))
                wr8(MC3_UNLOCK_FIRST + i, 1);
    if (S.money) {
        uint32_t va = money_va();
        if (va && mc3_rd32(va) != MC3_MONEY_MAX)
            mc3_wr32(va, MC3_MONEY_MAX);
    }
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

#define BTN_A(s)  ((s)->Gamepad.bAnalogButtons[0] > XBOX_ANALOG_BUTTON_THRESHOLD)
#define BTN_B(s)  ((s)->Gamepad.bAnalogButtons[1] > XBOX_ANALOG_BUTTON_THRESHOLD)

/* Logical buttons for edge detection. */
enum { K_UP = 1, K_DOWN = 2, K_LEFT = 4, K_RIGHT = 8, K_A = 16, K_B = 32,
       K_R3 = 64, K_L3R3 = 128, K_START = 256 };

static unsigned keys_of(const XBOX_INPUT_STATE *s)
{
    unsigned k = 0;
    WORD w = s->Gamepad.wButtons;
    if ((w & XBOX_GAMEPAD_DPAD_UP) || s->Gamepad.sThumbLY > 20000) k |= K_UP;
    if ((w & XBOX_GAMEPAD_DPAD_DOWN) || s->Gamepad.sThumbLY < -20000) k |= K_DOWN;
    if ((w & XBOX_GAMEPAD_DPAD_LEFT) || s->Gamepad.sThumbLX < -20000) k |= K_LEFT;
    if ((w & XBOX_GAMEPAD_DPAD_RIGHT) || s->Gamepad.sThumbLX > 20000) k |= K_RIGHT;
    if (BTN_A(s)) k |= K_A;
    if (BTN_B(s)) k |= K_B;
    if (w & XBOX_GAMEPAD_START) k |= K_START;
    if ((w & XBOX_GAMEPAD_RIGHT_THUMB) && !(w & XBOX_GAMEPAD_LEFT_THUMB)) k |= K_R3;
    if ((w & XBOX_GAMEPAD_RIGHT_THUMB) && (w & XBOX_GAMEPAD_LEFT_THUMB)) k |= K_L3R3;
    return k;
}

static void pad_clear(XBOX_INPUT_STATE *s)
{
    memset(&s->Gamepad, 0, sizeof s->Gamepad);
}

static void menu_input(unsigned press, unsigned held, double t)
{
    static double repeat_at;
    static unsigned last_dir;
    int items[16], n, it;
    unsigned dir = held & (K_UP | K_DOWN | K_LEFT | K_RIGHT);

    /* Held directions repeat after 0.35 s, every 0.1 s. */
    if (dir && dir == last_dir && t >= repeat_at) {
        press |= dir;
        repeat_at = t + 0.10;
    } else if (dir != last_dir) {
        repeat_at = t + 0.35;
    }
    last_dir = dir;

    n = page_items(s_page, items);
    if (s_sel >= n) s_sel = n - 1;
    if (s_sel < 0) s_sel = 0;
    if (press & K_UP) s_sel = (s_sel + n - 1) % n;
    if (press & K_DOWN) s_sel = (s_sel + 1) % n;
    it = items[s_sel];
    if (press & K_LEFT) item_act(it, -1);
    if (press & K_RIGHT) item_act(it, +1);
    if (press & K_A) item_act(it, 0);
    if (press & (K_B | K_START)) {
        if (s_page != PAGE_MAIN) {
            /* Back onto the entry that led here. */
            int back = s_page == PAGE_GRAPHICS ? IT_GRAPHICS : s_page == PAGE_CHEATS ? IT_CHEATS
                     : s_page == PAGE_WORLD ? IT_WORLD : IT_DEV, items2[16], m, j;
            s_page = PAGE_MAIN;
            m = page_items(PAGE_MAIN, items2);
            for (j = 0, s_sel = 0; j < m; j++)
                if (items2[j] == back) s_sel = j;
        } else {
            s_open = 0;
        }
    }
}

/* "Apply Vehicle Now": a car is only built when the game loads the city,
 * so the pad goes the way a player would: the pause menu closed and opened
 * again (which puts it on its first entry), up twice onto "Go to Garage"
 * (the second-last entry; the menu wraps and has 5 or 6 entries) and Yes;
 * in the garage (game mode 5) Back, Yes, and the city loads with the chosen
 * car. A press lost in the garage leaves its question on No: harmless.
 * Each step waits (-1: until the garage has been up for 10 s, past its intro),
 * presses its buttons for 0.25 s and, where it has one, waits for the pause
 * menu to be shown (1) or gone (0), pressing again every 2.5 s (the game
 * drops a Start soon after the last one). */
static const struct { float wait; unsigned keys; int paused; } MACRO[] = {
    { 0.3f, K_START, 0 },                               /* resume */
    { 1.0f, K_START, 1 },                               /* pause: on "Continue" */
    { 1.5f, K_UP, -1 }, { 1.0f, K_UP, -1 },             /* "Go to Garage" */
    { 1.0f, K_A, 0 },                                   /* its question replaces the menu */
    { 1.5f, K_UP, -1 }, { 1.0f, K_A, -1 },              /* Yes (the answer starts on No) */
    { -1.0f, K_B, -1 },                                 /* "Exit the garage?" */
    { 2.0f, K_UP, -1 }, { 1.5f, K_A, -1 },              /* Yes */
};
#define NMACRO ((int)(sizeof MACRO / sizeof MACRO[0]))
#define MACRO_RETRY 2.5
#define MACRO_TRIES 4
#define MACRO_GARAGE_TIMEOUT 120.0

static void macro_input(XBOX_INPUT_STATE *st, double t)
{
    static double at, pressed_at, garage_since;
    static int pressed, tries;
    pad_clear(st);
    if (!at && !pressed)
        at = t + (MACRO[s_macro].wait < 0 ? 0 : MACRO[s_macro].wait);
    if (!pressed && MACRO[s_macro].wait < 0) {
        if (mc3_rd32(MC3_GAME_MODE) != MC3_MODE_CAREER_GARAGE) {
            garage_since = 0;
            if (t - at > MACRO_GARAGE_TIMEOUT) {
                fprintf(stderr, "[EXTRAS] garage round trip: no garage\n");
                s_macro = -1;
                at = 0;
            }
            return;
        }
        if (!garage_since)
            garage_since = t;
        if (t - garage_since < 10.0)
            return;
        garage_since = 0;
        pressed = 1;
        pressed_at = t;
        return;
    }
    if (pressed) {
        int want = MACRO[s_macro].paused;
        if (t < pressed_at + 0.25) {
            unsigned k = MACRO[s_macro].keys;
            if (k & K_START) st->Gamepad.wButtons |= XBOX_GAMEPAD_START;
            if (k & K_UP) st->Gamepad.wButtons |= XBOX_GAMEPAD_DPAD_UP;
            if (k & K_A) st->Gamepad.bAnalogButtons[0] = 255;
            if (k & K_B) st->Gamepad.bAnalogButtons[1] = 255;
            return;
        }
        if (want >= 0 && mc3_game_paused() != want) {
            if (t < pressed_at + MACRO_RETRY)
                return;
            if (++tries >= MACRO_TRIES) {
                fprintf(stderr, "[EXTRAS] garage round trip: step %d got no answer\n", s_macro);
                toast(T(T_GARAGE_FAIL));
                s_macro = -1;
                pressed = 0;
                at = 0;
                return;
            }
            pressed_at = t;             /* press again */
            return;
        }
        pressed = 0;
        tries = 0;
        if (++s_macro >= NMACRO) {
            s_macro = -1;
            at = 0;
            return;
        }
        at = t + (MACRO[s_macro].wait < 0 ? 0 : MACRO[s_macro].wait);
        return;
    }
    if (t >= at) {
        pressed = 1;
        pressed_at = t;
    }
}

/* The toolkit's input filter: every pad report before the game reads it. */
void xbox_input_filter(DWORD port, XBOX_INPUT_STATE *st)
{
    static unsigned prev;
    static int combo = -1, swallow;
    unsigned k, press;
    double t;

    if (port != 0)
        return;
    if (combo < 0)
        combo = (int)env_f("MC3_EXTRAS_COMBO", 1);
    t = now_s();
    k = keys_of(st);
    press = k & ~prev;
    prev = k;

    pthread_mutex_lock(&s_lock);
    apply_cheats();
    world_apply(0);
    time_apply();
    if (s_macro >= 0) {
        macro_input(st, t);
    } else if (s_freecam && !s_open) {
        /* The camera takes the pad; B (or Start) ends it, back in the pause
         * menu it was started from. */
        if (press & (K_B | K_START)) {
            s_freecam = 0;
            mc3_freecam_enable(0);
            swallow = 1;
        } else {
            /* The d-pad as well: up/down move, left/right turn. */
            WORD w = st->Gamepad.wButtons;
            int ly = st->Gamepad.sThumbLY, rx = st->Gamepad.sThumbRX;
            if (w & XBOX_GAMEPAD_DPAD_UP) ly = 32767;
            if (w & XBOX_GAMEPAD_DPAD_DOWN) ly = -32767;
            if (w & XBOX_GAMEPAD_DPAD_LEFT) rx = -32767;
            if (w & XBOX_GAMEPAD_DPAD_RIGHT) rx = 32767;
            mc3_freecam_input(st->Gamepad.sThumbLX, ly, rx, st->Gamepad.sThumbRY,
                              st->Gamepad.bAnalogButtons[6], st->Gamepad.bAnalogButtons[7],
                              BTN_A(st), (st->Gamepad.wButtons & XBOX_GAMEPAD_LEFT_THUMB) != 0);
        }
        pad_clear(st);
    } else if (!s_open) {
        if (((press & K_R3) && mc3_game_paused()) || (combo && (press & K_L3R3))) {
            s_open = 1;
            s_page = PAGE_MAIN;
            s_sel = 0;
            pad_clear(st);
        }
    } else {
        menu_input(press, k, t);
        pad_clear(st);
        if (!s_open)
            swallow = 1;
    }
    /* The button that closed the menu stays away from the game until it is
     * let go: B would otherwise also close the pause menu below. */
    if (swallow && s_macro < 0) {
        if (k & (K_A | K_B | K_START))
            pad_clear(st);
        else
            swallow = 0;
    }
    pthread_mutex_unlock(&s_lock);
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

/* MC3's palette, read off its menus. */
#define C_DIM      0x99000000u
#define C_PANEL_T  0xE6181C24u
#define C_PANEL_B  0xE60A0C10u
#define C_RIM      0xFFAEB4BEu
#define C_RIM_DARK 0xFF4A505Au
#define C_TEXT     0xFFF2F2F2u
#define C_GREY     0xFF9AA0A8u
#define C_ORANGE   0xFFF5A11Cu
#define C_ORANGE_D 0xFFC46A0Au
#define C_BLUE     0xFF2F6FB8u

#define F_ITEM  (NV2A_OVL_ITALIC | NV2A_OVL_UPPER | NV2A_OVL_SHADOW)

static void panel(float x, float y, float w, float h)
{
    /* A rim of 2 px, light at the top, darker at the bottom, around a dark
     * gradient: the look of the game's dialog boxes. Corners cut by 6 px. */
    const float c = 6.0f;
    nv2a_ovl_grad(x + c, y, w - 2 * c, 2, C_RIM, C_RIM);
    nv2a_ovl_grad(x + c, y + h - 2, w - 2 * c, 2, C_RIM_DARK, C_RIM_DARK);
    nv2a_ovl_grad(x, y + c, 2, h - 2 * c, C_RIM, C_RIM_DARK);
    nv2a_ovl_grad(x + w - 2, y + c, 2, h - 2 * c, C_RIM, C_RIM_DARK);
    nv2a_ovl_rect(x + 2, y + 2, c - 2, c - 2, C_RIM);
    nv2a_ovl_rect(x + w - c, y + 2, c - 2, c - 2, C_RIM);
    nv2a_ovl_rect(x + 2, y + h - c, c - 2, c - 2, C_RIM_DARK);
    nv2a_ovl_rect(x + w - c, y + h - c, c - 2, c - 2, C_RIM_DARK);
    nv2a_ovl_grad(x + 2, y + 2, w - 4, h - 4, C_PANEL_T, C_PANEL_B);
}

static void chevrons(float x, float y, float s, uint32_t col)
{
    /* Two slanted strokes per chevron, like the game's "»" marker. */
    int k;
    for (k = 0; k < 2; k++) {
        float cx = x + k * s * 0.55f;
        nv2a_ovl_slant(cx, y, s * 0.22f, s * 0.5f, col, col, -s * 0.28f);
        nv2a_ovl_slant(cx - s * 0.28f, y + s * 0.5f, s * 0.22f, s * 0.5f, col, col, s * 0.28f);
    }
}

static void button_icon(float x, float y, const char *glyph, uint32_t col)
{
    nv2a_ovl_rect(x + 3, y, 20, 26, col);
    nv2a_ovl_rect(x, y + 3, 26, 20, col);
    nv2a_ovl_text(x + 13, y + 1, 22, 0xFF000000u, NV2A_OVL_CENTER, glyph);
}

static void draw_menu(void)
{
    int items[16], n, i;
    const float pw = 640, x = (1280 - pw) / 2, row = 46;
    float ph, y;
    char val[48];
    const char *title = s_page == PAGE_GRAPHICS ? T(T_GRAPHICS)
                      : s_page == PAGE_CHEATS ? T(T_CHEATS)
                      : s_page == PAGE_WORLD ? T(T_WORLD)
                      : s_page == PAGE_DEV ? T(T_DEV) : NULL;

    n = page_items(s_page, items);
    if (s_sel >= n) s_sel = n - 1;
    ph = 120 + n * row + 70;
    y = (720 - ph) / 2;

    nv2a_ovl_rect(0, 0, 1280, 720, C_DIM);
    /* Title: the game's slanted blue band behind italic capitals. */
    nv2a_ovl_slant(x - 20, y - 54, 360, 44, C_BLUE, 0xFF173A66u, 14);
    nv2a_ovl_text(x, y - 50, 38, C_TEXT, F_ITEM, T(T_EXTRAS));
    panel(x, y, pw, ph);
    {
        char crumb[96];
        snprintf(crumb, sizeof crumb, "%s \\ %s%s%s", T(T_PAUSE), T(T_EXTRAS),
                 title ? " \\ " : "", title ? title : "");
        nv2a_ovl_text(x + 22, y + 14, 20, C_GREY, NV2A_OVL_ITALIC | NV2A_OVL_UPPER, crumb);
        nv2a_ovl_rect(x + 20, y + 42, pw - 40, 1, C_RIM_DARK);
    }
    for (i = 0; i < n; i++) {
        float iy = y + 64 + i * row;
        int sel = i == s_sel;
        int grey = (items[i] == IT_FREECAM && !mc3_freecam_ready());
        uint32_t col = sel ? C_ORANGE : grey ? C_GREY : C_TEXT;
        if (sel) {
            nv2a_ovl_grad(x + 14, iy - 4, pw - 28, row - 6, 0x40F5A11Cu, 0x10F5A11Cu);
            chevrons(x + 34, iy + 6, 24, C_ORANGE);
        }
        nv2a_ovl_text(x + 76, iy, 30, col, F_ITEM, item_label(items[i]));
        item_value(items[i], val, sizeof val);
        if (val[0]) {
            float w = nv2a_ovl_text_width(28, F_ITEM, val);
            float vx = x + pw - 40;
            if (sel && items[i] != IT_FREECAM) {
                nv2a_ovl_text(vx - w - 22, iy, 28, C_ORANGE, F_ITEM, "<");
                nv2a_ovl_text(vx + 4, iy, 28, C_ORANGE, F_ITEM, ">");
            }
            nv2a_ovl_text(vx, iy, 28, sel ? C_ORANGE : C_TEXT, F_ITEM | NV2A_OVL_RIGHT, val);
        }
    }
    if (s_page == PAGE_MAIN && !mc3_in_career())
        nv2a_ovl_text(x + 76, y + 64 + n * row + 2, 20, C_GREY, NV2A_OVL_ITALIC, T(T_CAREER_ONLY));
    {
        /* Legend: (A) Accept  (B) Back, as at the bottom of every MC3 menu. */
        float ly = y + ph - 44, lx = x + pw - 30;
        float wb = nv2a_ovl_text_width(22, F_ITEM, T(T_BACK));
        float wa = nv2a_ovl_text_width(22, F_ITEM, T(T_ACCEPT));
        nv2a_ovl_text(lx, ly + 2, 22, C_TEXT, F_ITEM | NV2A_OVL_RIGHT, T(T_BACK));
        button_icon(lx - wb - 34, ly, "B", 0xFFD8352Au);
        lx -= wb + 60;
        nv2a_ovl_text(lx, ly + 2, 22, C_TEXT, F_ITEM | NV2A_OVL_RIGHT, T(T_ACCEPT));
        button_icon(lx - wa - 34, ly, "A", 0xFF3FB83Fu);
    }
}

static void draw_fps(void)
{
    static uint32_t last_frames;
    static double last_t;
    static float fps, ms;
    double t = now_s();
    uint32_t fr = nv2a_gl_frame_count();
    char buf[48];
    if (t - last_t >= 0.5) {
        if (last_t > 0 && fr >= last_frames) {
            fps = (float)((fr - last_frames) / (t - last_t));
            ms = fps > 0 ? 1000.0f / fps : 0;
        }
        last_t = t;
        last_frames = fr;
    }
    snprintf(buf, sizeof buf, "%.0f FPS", fps);
    nv2a_ovl_slant(16, 14, 150, 40, 0xB0101418u, 0xB0101418u, 8);
    nv2a_ovl_text(30, 16, 28, fps >= 29.0f ? 0xFF7CE07Cu : fps >= 24.0f ? C_ORANGE : 0xFFF05A4Au,
                  F_ITEM, buf);
    snprintf(buf, sizeof buf, "%.1f ms", ms);
    nv2a_ovl_text(170, 22, 18, C_GREY, NV2A_OVL_ITALIC, buf);
}

/* The renderer's overlay call: once a frame, on its thread. */
void nv2a_gl_host_overlay(void)
{
    double t = now_s();
    pthread_mutex_lock(&s_lock);
    if (S.fps)
        draw_fps();
    if (s_open) {
        draw_menu();
    } else if (s_freecam) {
        nv2a_ovl_slant(240, 660, 800, 34, 0xB0101418u, 0xB0101418u, 8);
        nv2a_ovl_text(640, 666, 18, C_TEXT, NV2A_OVL_ITALIC | NV2A_OVL_CENTER, T(T_FREECAM_HELP));
    } else if (mc3_game_paused()) {
        /* The way in, at the bottom right under the pause menu's own legend. */
        float w = nv2a_ovl_text_width(22, F_ITEM, T(T_HINT));
        nv2a_ovl_text(1240, 676, 22, C_TEXT, F_ITEM | NV2A_OVL_RIGHT, T(T_HINT));
        button_icon(1240 - w - 34, 674, "R", C_BLUE);
    }
    if (t < s_toast_until && s_toast[0]) {
        float w = nv2a_ovl_text_width(24, F_ITEM, s_toast);
        nv2a_ovl_slant(640 - w / 2 - 30, 600, w + 60, 40, 0xE0101418u, 0xE0101418u, 8);
        nv2a_ovl_text(640, 606, 24, C_ORANGE, F_ITEM | NV2A_OVL_CENTER, s_toast);
    }
    pthread_mutex_unlock(&s_lock);
}

/* ------------------------------------------------------------------ */
/* Start-up                                                            */
/* ------------------------------------------------------------------ */

/* MC3_MEMWATCH=<va>[:len],...: those guest words every second, as [MW]
 * lines (finding addresses for this file; off by default). */
static void memwatch(void)
{
    const char *spec = getenv("MC3_MEMWATCH");
    char line[512];
    const char *p;
    size_t o;
    if (!spec || !*spec)
        return;
    o = (size_t)snprintf(line, sizeof line, "[MW] pause %08X/%d mode %u money %u |",
                         s_pause_obj, mc3_game_paused(), mc3_rd32(MC3_GAME_MODE),
                         money_va() ? mc3_rd32(money_va()) : 0);
    for (p = spec; *p && o < sizeof line - 24; ) {
        char *end;
        uint32_t va = (uint32_t)strtoul(p, &end, 16), n = 1, k;
        if (*end == ':') n = (uint32_t)strtoul(end + 1, &end, 10);
        for (k = 0; k < n && o < sizeof line - 12; k++)
            o += (size_t)snprintf(line + o, sizeof line - o, " %08X", mc3_rd32(va + 4 * k));
        p = *end == ',' ? end + 1 : end;
        if (p == end && *p) break;
    }
    fprintf(stderr, "%s\n", line);
}

/* MC3_FIND_VEHICLES=1: once every 10 s, the vehicle names ("vp_...") in the
 * career profile and in the settings object 0x00507BC0, with their offsets
 * (finding where the selected car is kept). */
static void find_vehicles(void)
{
    static int on = -1;
    static unsigned tick;
    uint32_t objs[2], sizes[2] = { 0x18000, 0x12000 }, k;
    if (on < 0)
        on = (int)env_f("MC3_FIND_VEHICLES", 0);
    if (!on || (tick++ % 10))
        return;
    objs[0] = money_va() ? money_va() - MC3_PROFILE_MONEY : 0;
    objs[1] = 0x00507BC0u;
    for (k = 0; k < 2; k++) {
        uint32_t o = objs[k], i;
        if (!o || !ram_ok(o, sizes[k]))
            continue;
        for (i = 0; i + 4 < sizes[k]; i++) {
            const char *c = (const char *)host_of(o + i);
            if (c[0] == 'v' && c[1] == 'p' && c[2] == '_' && (i == 0 || c[-1] == 0)) {
                char name[40];
                snprintf(name, sizeof name, "%.39s", c);
                fprintf(stderr, "[VEH] %s %08X +0x%05X %s\n", k ? "settings" : "profile", o, i, name);
                i += (uint32_t)strlen(name);
            }
        }
    }
}

/* MC3_FIND_STR=<text>: once every 10 s, every place in guest RAM (low 64 MB
 * and the contiguous window) that holds that NUL-terminated text. */
static void find_str(void)
{
    static const char *want = (const char *)-1;
    static unsigned tick;
    const uint32_t lo[2] = { 0x00010000u, CONTIG_BASE }, hi[2] = { GUEST_RAM_END, CONTIG_END };
    size_t wl;
    int r, shown = 0;
    if (want == (const char *)-1)
        want = getenv("MC3_FIND_STR");
    if (!want || !*want || (tick++ % 10))
        return;
    wl = strlen(want) + 1;
    for (r = 0; r < 2; r++) {
        const char *base = (const char *)host_of(lo[r]);
        const char *p = base, *end = base + (hi[r] - lo[r]) - wl;
        while (p < end && (p = memchr(p, want[0], (size_t)(end - p))) != NULL) {
            if (!memcmp(p, want, wl) && shown++ < 40)
                fprintf(stderr, "[FIND] %s at %08X\n", want, lo[r] + (uint32_t)(p - base));
            p++;
        }
    }
}

/* MC3_SWAP_STR=<from>:<to>:<second>[:<lo hex>-<hi hex>]: at that second
 * after start, every NUL-terminated <from> in guest RAM (or the range) becomes
 * <to> (no longer than <from>'s field allows; testing what the game reads). */
static void swap_str(unsigned sec)
{
    static int done;
    const char *e = getenv("MC3_SWAP_STR");
    char from[64], to[64];
    unsigned at = 0, lo = 0, hi = 0;
    int r, n = 0;
    if (done || !e || sscanf(e, "%63[^:]:%63[^:]:%u:%x-%x", from, to, &at, &lo, &hi) < 3 || sec < at)
        return;
    done = 1;
    for (r = 0; r < 2; r++) {
        uint32_t a = r ? CONTIG_BASE : 0x00010000u, b = r ? CONTIG_END : GUEST_RAM_END;
        char *base = (char *)host_of(a), *p, *end;
        size_t fl = strlen(from) + 1;
        if (lo && hi) { if (lo < a || hi > b) continue; base = (char *)host_of(lo); a = lo; b = hi; }
        end = base + (b - a) - 64;
        for (p = base; p < end && (p = memchr(p, from[0], (size_t)(end - p))) != NULL; p++)
            if (!memcmp(p, from, fl) && (p == base || p[-1] == 0 || (p[-1] & 0x80) || p[-1] < 32)) {
                snprintf(p, 40, "%s", to);
                n++;
                fprintf(stderr, "[SWAP] %08X %s -> %s\n", a + (uint32_t)(p - base), from, to);
            }
        if (lo && hi) break;
    }
    fprintf(stderr, "[SWAP] %d replaced\n", n);
}

/* MC3_FIND_POS=<x>,<z>,<radius>: once every 10 s, every float triple in
 * guest RAM whose x and z (offsets 0 and 8) lie within radius of (x, z)
 * and whose y is a plausible height (finding copies of a position). */
static void find_pos(void)
{
    static int parsed;
    static float px, pz, rad;
    static unsigned tick;
    const char *e;
    int r, shown = 0;
    if (!parsed) {
        parsed = -1;
        if ((e = getenv("MC3_FIND_POS")) && sscanf(e, "%f,%f,%f", &px, &pz, &rad) == 3)
            parsed = 1;
    }
    if (parsed < 0 || (tick++ % 10))
        return;
    for (r = 0; r < 2; r++) {
        uint32_t a = r ? CONTIG_BASE : 0x00010000u, b = r ? CONTIG_END : GUEST_RAM_END, va;
        const float *f = (const float *)host_of(a);
        for (va = a; va + 12 <= b; va += 4, f++) {
            float x = f[0], y = f[1], z = f[2];
            if (x > px - rad && x < px + rad && z > pz - rad && z < pz + rad
                    && y > -50.0f && y < 200.0f && shown++ < 60)
                fprintf(stderr, "[POS] %08X %.2f %.2f %.2f\n", va, x, y, z);
        }
    }
}

static void *extras_thread(void *arg)
{
    unsigned tick = 0, misses = 0;
    (void)arg;
    for (;;) {
        sleep(1);
        tick++;
        if (!g_xbox_mem_offset)
            continue;
        {
            uint32_t o = s_pause_obj;
            /* A scan reads up to 120 MB: every second at first, then every 5 s
             * while the menus do not exist yet (boot, movies). */
            if ((!o || mc3_rd32(o) != MC3_VT_MENU_PAUSE)
                    && (misses < 20 || tick % 5 == 0)) {
                o = find_pause_menu();
                if (o != s_pause_obj && o)
                    fprintf(stderr, "[EXTRAS] pause menu object at %08X\n", o);
                misses = o ? 0 : misses + 1;
                s_pause_obj = o;
            }
        }
        memwatch();
        find_vehicles();
        find_str();
        swap_str(tick);
        find_pos();
    }
    return NULL;
}

void mc3_extras_init(void)
{
    const char *lang = getenv("RECOMP_LANGUAGE");
    pthread_t th;
    settings_load();
    if (lang && !strncmp(lang, "de", 2))
        s_txt = TXT_DE;
    mc3_freecam_init();
    if (pthread_create(&th, NULL, extras_thread, NULL) == 0)
        pthread_detach(th);
    fprintf(stderr, "[EXTRAS] ready: R3 in the pause menu%s opens Extras\n",
            env_f("MC3_EXTRAS_COMBO", 1) ? " (or L3+R3)" : "");
}
