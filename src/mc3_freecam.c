/*
 * mc3_freecam.c -- the free camera of the Extras menu.
 *
 * MC3's view goes through camViewCS (vtable 0x00421200): its method
 * 0x00120F90 hands the active camera's matrix (camera +0x0C) to
 * sub_002A4560, which stores it as the renderer's camera (0x0051A3C0),
 * derives the view (0x0051A2B8), the frustum and D3D's view transform;
 * then it sets the projection from the camera's field of view (+0x84) and
 * clip planes (+0x88, +0x8C). recomp_manual.c wraps that method and calls
 * mc3_freecam_apply() around it: while the free camera is on, our matrix
 * stands in the camera object for exactly that call, so rendering, culling
 * and streaming follow it and the game's own camera logic never sees it.
 *
 * The matrix is RAGE's Matrix34: rows a (right), b (up), c (back: the
 * camera looks along -c), d (position), three floats each. Y is up.
 *
 * Opened from the pause menu the world stands still; the game's menus and
 * HUD (screen-space draws) are hidden while flying (nv2a_gl_set_hide_2d).
 */
#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mc3_extras.h"

void nv2a_gl_set_hide_2d(int on);
extern ptrdiff_t g_xbox_mem_offset;

#define CAMVIEW_CAMERA  0x2C                /* camViewCS -> active camera */
#define CAM_MATRIX      0x0C                /* camera -> Matrix34 */
#define RENDER_CAMERA   0x0051A3C0u         /* the renderer's current camera, 4x4 */

typedef struct { float x, y, z; } V3;

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
/* The game camera of this frame (camViewCS's, the player's view). */
static float s_game[12];
static int   s_have_game;
static uint32_t s_cam_obj;                  /* the camera object last applied */
static int   s_keep = -1;                   /* MC3_FREECAM_KEEP */
static volatile int s_on, s_want_init;
static V3     s_pos;
static float  s_yaw, s_pitch;               /* radians; yaw 0 looks along -z */
static float  s_in[8];                      /* lx ly rx ry lt rt fast slow */
static double s_last_apply, s_last_t;

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

static float *guest_f(uint32_t va)
{
    return (float *)((uintptr_t)va + g_xbox_mem_offset);
}

void mc3_freecam_init(void) {}

/* A world camera has been applied in the last half second. */
int mc3_freecam_ready(void)
{
    return now_s() - s_last_apply < 0.5;
}

/* The world stands still while the camera flies: the frame time's limits
 * (mc3_extras.c, mc3_time_freeze). Opened from the pause menu the game is
 * not updating anyway; flying while driving needs it. */
void mc3_time_freeze(int on);

void mc3_freecam_enable(int on)
{
    if (s_keep < 0) {
        const char *e = getenv("MC3_FREECAM_KEEP");
        s_keep = !(e && *e == '0');
    }
    pthread_mutex_lock(&s_lock);
    if (!on && s_on && s_keep && s_cam_obj && s_have_game)
        memcpy(guest_f(s_cam_obj + CAM_MATRIX), s_game, sizeof s_game);   /* the game's back */
    if (on && !mc3_freecam_ready())
        on = 0;
    if (on && !s_on)
        s_want_init = 1;                    /* start where the game's camera is */
    s_on = on;
    memset(s_in, 0, sizeof s_in);
    mc3_time_freeze(on);
    pthread_mutex_unlock(&s_lock);
    nv2a_gl_set_hide_2d(on);
}

int mc3_freecam_on(void)
{
    return s_on;
}

static float axis(int v)
{
    float f = (float)v / 32767.0f;
    float dead = 0.18f;
    if (fabsf(f) < dead)
        return 0.0f;
    f = (fabsf(f) - dead) / (1.0f - dead) * (f < 0 ? -1.0f : 1.0f);
    return f * fabsf(f);                    /* finer control near the centre */
}

void mc3_freecam_input(int lx, int ly, int rx, int ry, int lt, int rt, int fast, int slow)
{
    pthread_mutex_lock(&s_lock);
    s_in[0] = axis(lx); s_in[1] = axis(ly);
    s_in[2] = axis(rx); s_in[3] = axis(ry);
    s_in[4] = lt / 255.0f; s_in[5] = rt / 255.0f;
    s_in[6] = (float)fast; s_in[7] = (float)slow;
    pthread_mutex_unlock(&s_lock);
}

/* Orientation from yaw/pitch: forward f, right r, up u; the matrix rows are
 * a = r, b = u, c = -f. */
static void basis(V3 *r, V3 *u, V3 *f)
{
    float cy = cosf(s_yaw), sy = sinf(s_yaw), cp = cosf(s_pitch), sp = sinf(s_pitch);
    *f = (V3){ sy * cp, sp, -cy * cp };
    *r = (V3){ cy, 0.0f, sy };
    *u = (V3){ -sy * sp, cp, cy * sp };     /* r x f... = up, kept orthonormal */
}

static void step(double dt)
{
    V3 r, u, f;
    float speed = 18.0f, turn = 1.8f;
    if (s_in[6] > 0) speed *= 4.0f;
    if (s_in[7] > 0) speed *= 0.25f;
    if (dt > 0.1) dt = 0.1;
    s_yaw += s_in[2] * turn * (float)dt;
    s_pitch += s_in[3] * turn * (float)dt;
    if (s_pitch > 1.45f) s_pitch = 1.45f;
    if (s_pitch < -1.45f) s_pitch = -1.45f;
    basis(&r, &u, &f);
    s_pos.x += (f.x * s_in[1] + r.x * s_in[0]) * speed * (float)dt;
    s_pos.y += (f.y * s_in[1] + (s_in[5] - s_in[4])) * speed * (float)dt;
    s_pos.z += (f.z * s_in[1] + r.z * s_in[0]) * speed * (float)dt;
}

/* The game camera of this frame (camViewCS's, the player's view), and ours
 * in both forms the renderer's two setters take. */
static float s_ours34[12], s_ours44[16];

static void build_ours(void)
{
    V3 r, u, f;
    basis(&r, &u, &f);
    s_ours34[0] = r.x;  s_ours34[1] = r.y;  s_ours34[2] = r.z;
    s_ours34[3] = u.x;  s_ours34[4] = u.y;  s_ours34[5] = u.z;
    s_ours34[6] = -f.x; s_ours34[7] = -f.y; s_ours34[8] = -f.z;
    s_ours34[9] = s_pos.x; s_ours34[10] = s_pos.y; s_ours34[11] = s_pos.z;
    /* sub_0001D1C0's 4x4: each row of three plus w (0, 0, 0, 1). */
    memset(s_ours44, 0, sizeof s_ours44);
    memcpy(&s_ours44[0], &s_ours34[0], 12);
    memcpy(&s_ours44[4], &s_ours34[3], 12);
    memcpy(&s_ours44[8], &s_ours34[6], 12);
    memcpy(&s_ours44[12], &s_ours34[9], 12);
    s_ours44[15] = 1.0f;
}

/* recomp_manual.c, before camViewCS's apply (sub_00120F90): the game's
 * camera this frame, and the step of ours. */
void mc3_freecam_apply(uint32_t view, int before)
{
    uint32_t cam;
    float *m;
    double t;

    if (!before)
        return;
    cam = mc3_rd32(view + CAMVIEW_CAMERA);
    t = now_s();
    if (!cam || mc3_rd32(cam) < 0x00010000u)
        return;
    s_last_apply = t;
    m = guest_f(cam + CAM_MATRIX);
    pthread_mutex_lock(&s_lock);
    /* The game's camera: what the object holds, unless that is still ours
     * from the last frame (the game had no update in between). */
    if (!(s_on && s_have_game && !memcmp(m, s_ours34, sizeof s_ours34)))
        memcpy(s_game, m, sizeof s_game);
    s_have_game = 1;
    s_cam_obj = cam;
    if (s_on) {
        if (s_want_init) {
            /* From the game's camera: position d, looking along -c. */
            float fx = -m[6], fy = -m[7], fz = -m[8];
            s_pos = (V3){ m[9], m[10], m[11] };
            s_yaw = atan2f(fx, -fz);
            s_pitch = asinf(fy < -1 ? -1 : fy > 1 ? 1 : fy);
            s_want_init = 0;
            s_last_t = t;
        }
        step(t - s_last_t);
        s_last_t = t;
        build_ours();
        /* Ours stays in the camera object for the whole frame: whatever reads
         * the camera after the view is set -- visibility, level of detail,
         * streaming -- sees the free camera too (MC3_FREECAM_KEEP=0: only
         * during the setters, as before). */
        if (s_keep)
            memcpy(m, s_ours34, sizeof s_ours34);
        {
            /* MC3_FREECAM_FOCUS=<hex va>[,...]: our position written over
             * each of those every frame (finding what the city's streaming
             * and visibility centre on). */
            static int nf = -1;
            static uint32_t fva[16];
            int k;
            if (nf < 0) {
                const char *e = getenv("MC3_FREECAM_FOCUS");
                nf = 0;
                while (e && *e && nf < 16) {
                    char *end;
                    fva[nf++] = (uint32_t)strtoul(e, &end, 16);
                    e = *end == ',' ? end + 1 : end;
                    if (end == e && *e) break;
                }
            }
            for (k = 0; k < nf; k++)
                memcpy(guest_f(fva[k]), &s_ours34[9], 12);
        }
    }
    {
        /* MC3_FREECAM_TRACE=1: once a second, where both cameras are. */
        static int trace = -1;
        static double last;
        if (trace < 0) {
            const char *e = getenv("MC3_FREECAM_TRACE");
            trace = e && *e == '1';
        }
        if (trace && t - last >= 1.0) {
            float *rc = guest_f(RENDER_CAMERA);
            fprintf(stderr, "[FREECAM] on %d game %.1f %.1f %.1f ours %.1f %.1f %.1f renderer %.1f %.1f %.1f\n",
                    s_on, m[9], m[10], m[11], s_pos.x, s_pos.y, s_pos.z, rc[12], rc[13], rc[14]);
            last = t;
        }
    }
    pthread_mutex_unlock(&s_lock);
}

static int is_game_pos(const float *p)
{
    return s_have_game && fabsf(p[0] - s_game[9]) < 0.01f && fabsf(p[1] - s_game[10]) < 0.01f
        && fabsf(p[2] - s_game[11]) < 0.01f;
}

/* recomp_manual.c, around the renderer's camera setters: sub_002A4560
 * (Matrix34*) and sub_002A4530 (4x4). The view is set from the game's camera
 * more than once a frame (camViewCS, then copies kept by viewports and
 * passes); each time it is that camera, ours stands in for the call.
 * Returns 1 if *va's matrix was swapped (restore with the same call, 0). */
int mc3_freecam_setter(uint32_t va, int is44, int before)
{
    static float saved[16];
    static uint32_t swapped_va;
    float *m;

    if (!before) {
        if (swapped_va != va || !va)
            return 0;
        memcpy(guest_f(va), saved, is44 ? 64 : 48);
        swapped_va = 0;
        return 1;
    }
    if (!s_on || !va || !s_have_game)
        return 0;
    m = guest_f(va);
    if (!is_game_pos(is44 ? &m[12] : &m[9]))
        return 0;
    pthread_mutex_lock(&s_lock);
    memcpy(saved, m, is44 ? 64 : 48);
    memcpy(m, is44 ? s_ours44 : s_ours34, is44 ? 64 : 48);
    pthread_mutex_unlock(&s_lock);
    swapped_va = va;
    return 1;
}
