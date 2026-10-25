/*
 * gl_overlay.c -- a host overlay over the presented frame: rectangles,
 * gradients and text in a 1280x720 virtual canvas, drawn after the title's
 * picture and before the swap (nv2a_gl.c, gl_flip).
 *
 * For the project's own screens -- menus and counters the title knows
 * nothing of. The project draws from nv2a_gl_host_overlay(), called once a
 * frame on the renderer's thread; everything queued there goes out in one
 * draw call. Text comes from a TrueType font the host supplies
 * (nv2a_gl_host_font), baked once into a 1-channel atlas with stb_truetype;
 * italic is a shear in the vertex data. No font: rectangles only.
 *
 * Costs nothing when the host draws nothing: no GL calls at all.
 */
#include "gl_api.h"
#include "gl_overlay.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "third_party/stb_truetype.h"
#pragma GCC diagnostic pop

int stbi_write_png(char const *filename, int w, int h, int comp, const void *data,
                   int stride_in_bytes);

#define GL_RED       0x1903
#define GL_R8        0x8229
#define GL_DYNAMIC_DRAW 0x88E8

/* The atlas: printable ASCII and Latin-1 (German, French, Spanish and
 * Italian text), baked at one size and scaled. */
#define ATLAS_W     1024
#define ATLAS_H     1024
#define BAKE_PX     56.0f
#define FIRST_CHAR  32
#define NUM_CHARS   224                     /* 32..255 */

typedef struct { float x, y, u, v; uint32_t c; } OvlVert;

static int           s_state;               /* 0 untried, 1 ready, -1 failed */
static GLuint        s_prog, s_vao, s_vbo, s_tex, s_white_tex;
static GLint         s_u_canvas, s_u_tex;
static stbtt_bakedchar s_chars[NUM_CHARS];
static int           s_have_font;
static float         s_ascent;               /* baseline offset at BAKE_PX */
static OvlVert      *s_v;
static size_t        s_nv, s_cap;
static int           s_active;               /* between begin and end */

/* The host's TrueType font (the project: a file on the SD card, the
 * Switch's system font, ...). NULL: no text. The bytes stay the host's. */
__attribute__((weak)) const void *nv2a_gl_host_font(size_t *len)
{
    (void)len;
    return NULL;
}

/* The host's drawing, once a frame. */
__attribute__((weak)) void nv2a_gl_host_overlay(void)
{
}

static GLuint ovl_compile(GLenum type, const char *src)
{
    GLuint sh = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, sizeof log, NULL, log);
        fprintf(stderr, "  [OVL] shader: %s\n", log);
    }
    return sh;
}

static int ovl_init(void)
{
    static const char *vs =
        "#version 330 core\n"
        "layout(location = 0) in vec2 a_pos;\n"
        "layout(location = 1) in vec2 a_uv;\n"
        "layout(location = 2) in vec4 a_col;\n"
        "uniform vec2 u_canvas;\n"
        "out vec2 v_uv; out vec4 v_col;\n"
        "void main() {\n"
        "  gl_Position = vec4(a_pos.x / u_canvas.x * 2.0 - 1.0, 1.0 - a_pos.y / u_canvas.y * 2.0, 0.0, 1.0);\n"
        "  v_uv = a_uv; v_col = a_col.bgra;\n"     /* ARGB words: B G R A bytes */
        "}\n";
    static const char *fs =
        "#version 330 core\n"
        "in vec2 v_uv; in vec4 v_col;\n"
        "uniform sampler2D u_tex;\n"
        "out vec4 o;\n"
        "void main() { o = vec4(v_col.rgb, v_col.a * texture(u_tex, v_uv).r); }\n";
    GLuint v, f;
    GLint ok = 0;
    size_t flen = 0;
    const void *font;
    static const uint8_t white = 255;

    s_state = -1;
    v = ovl_compile(GL_VERTEX_SHADER, vs);
    f = ovl_compile(GL_FRAGMENT_SHADER, fs);
    s_prog = glCreateProgram();
    glAttachShader(s_prog, v);
    glAttachShader(s_prog, f);
    glLinkProgram(s_prog);
    glDeleteShader(v);
    glDeleteShader(f);
    glGetProgramiv(s_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        fprintf(stderr, "  [OVL] link failed: overlay off\n");
        return 0;
    }
    s_u_canvas = glGetUniformLocation(s_prog, "u_canvas");
    s_u_tex = glGetUniformLocation(s_prog, "u_tex");

    glGenVertexArrays(1, &s_vao);
    glBindVertexArray(s_vao);
    glGenBuffers(1, &s_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(OvlVert), (const void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(OvlVert), (const void *)8);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(OvlVert), (const void *)16);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, &s_white_tex);
    glBindTexture(GL_TEXTURE_2D, s_white_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, &white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    font = nv2a_gl_host_font(&flen);
    if (font && flen) {
        uint8_t *bmp = (uint8_t *)calloc(ATLAS_W, ATLAS_H);
        stbtt_fontinfo fi;
        if (bmp && stbtt_InitFont(&fi, (const unsigned char *)font,
                                  stbtt_GetFontOffsetForIndex((const unsigned char *)font, 0))) {
            int asc, desc, gap;
            float sc = stbtt_ScaleForPixelHeight(&fi, BAKE_PX);
            stbtt_GetFontVMetrics(&fi, &asc, &desc, &gap);
            s_ascent = (float)asc * sc;
            if (stbtt_BakeFontBitmap((const unsigned char *)font, 0, BAKE_PX, bmp,
                                     ATLAS_W, ATLAS_H, FIRST_CHAR, NUM_CHARS, s_chars) != 0) {
                glGenTextures(1, &s_tex);
                glBindTexture(GL_TEXTURE_2D, s_tex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ATLAS_W, ATLAS_H, 0, GL_RED,
                             GL_UNSIGNED_BYTE, bmp);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                s_have_font = 1;
            }
        }
        free(bmp);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    fprintf(stderr, "  [OVL] overlay ready%s\n", s_have_font ? "" : " (no font: no text)");
    s_state = 1;
    return 1;
}

static OvlVert *ovl_reserve(size_t n)
{
    if (s_nv + n > s_cap) {
        size_t cap = (s_nv + n) * 2;
        OvlVert *nv = (OvlVert *)realloc(s_v, cap * sizeof *nv);
        if (!nv)
            return NULL;
        s_v = nv;
        s_cap = cap;
    }
    s_nv += n;
    return s_v + s_nv - n;
}

/* Two triangles; the white texel's coordinates (u < 0) select the solid
 * texture -- text and shapes share the batch through two textures, drawn
 * in two passes (see ovl_flush). */
static void quad(float x0, float y0, float x1, float y1, float u0, float v0,
                 float u1, float v1, uint32_t ctop, uint32_t cbot, float shear)
{
    OvlVert *q = ovl_reserve(6);
    if (!q)
        return;
    q[0] = (OvlVert){ x0 + shear, y0, u0, v0, ctop };
    q[1] = (OvlVert){ x1 + shear, y0, u1, v0, ctop };
    q[2] = (OvlVert){ x0, y1, u0, v1, cbot };
    q[3] = q[1];
    q[4] = (OvlVert){ x1, y1, u1, v1, cbot };
    q[5] = q[2];
}

/* Shapes go into their own list (s_shapes) so they draw under the text
 * queued in the same frame regardless of call order within a layer. */
static OvlVert *s_shape_v;
static size_t   s_shape_n, s_shape_cap;

static void shape_quad(float x0, float y0, float x1, float y1, uint32_t ctop,
                       uint32_t cbot, float shear)
{
    OvlVert *save_v = s_v;
    size_t save_n = s_nv, save_cap = s_cap;
    s_v = s_shape_v; s_nv = s_shape_n; s_cap = s_shape_cap;
    quad(x0, y0, x1, y1, 0.5f, 0.5f, 0.5f, 0.5f, ctop, cbot, shear);
    s_shape_v = s_v; s_shape_n = s_nv; s_shape_cap = s_cap;
    s_v = save_v; s_nv = save_n; s_cap = save_cap;
}

void nv2a_ovl_rect(float x, float y, float w, float h, uint32_t argb)
{
    if (s_active)
        shape_quad(x, y, x + w, y + h, argb, argb, 0.0f);
}

void nv2a_ovl_grad(float x, float y, float w, float h, uint32_t top, uint32_t bottom)
{
    if (s_active)
        shape_quad(x, y, x + w, y + h, top, bottom, 0.0f);
}

void nv2a_ovl_slant(float x, float y, float w, float h, uint32_t top, uint32_t bottom,
                    float lean)
{
    if (s_active)
        shape_quad(x, y, x + w, y + h, top, bottom, lean);
}

static float text_run(float x, float y, float px, uint32_t argb, int flags, const char *s,
                      int emit)
{
    float k = px / BAKE_PX, pen = 0.0f;
    const unsigned char *p = (const unsigned char *)s;

    if (!s_have_font)
        return 0.0f;
    for (; *p; p++) {
        unsigned ch = *p;
        stbtt_bakedchar *b;
        /* UTF-8 for Latin-1 (two bytes, C2/C3 lead). */
        if ((ch == 0xC2 || ch == 0xC3) && (p[1] & 0xC0) == 0x80) {
            ch = ((ch & 0x1F) << 6) | (p[1] & 0x3F);
            p++;
        }
        if (flags & NV2A_OVL_UPPER) {
            if (ch >= 'a' && ch <= 'z') ch -= 32;
            else if (ch >= 0xE0 && ch <= 0xFE && ch != 0xF7) ch -= 32;
        }
        if (ch < FIRST_CHAR || ch >= FIRST_CHAR + NUM_CHARS)
            ch = '?';
        b = &s_chars[ch - FIRST_CHAR];
        if (emit && b->x1 > b->x0) {
            float gx0 = x + pen + b->xoff * k, gy0 = y + (b->yoff + s_ascent) * k;
            float gx1 = gx0 + (float)(b->x1 - b->x0) * k, gy1 = gy0 + (float)(b->y1 - b->y0) * k;
            float lean = (flags & NV2A_OVL_ITALIC) ? (gy1 - gy0) * 0.21f : 0.0f;
            if (flags & NV2A_OVL_SHADOW) {
                uint32_t sh = ((argb >> 24) * 0xA0u / 0xFFu) << 24;   /* black, 63% */
                quad(gx0 + 2, gy0 + 2, gx1 + 2, gy1 + 2, b->x0 / (float)ATLAS_W, b->y0 / (float)ATLAS_H,
                     b->x1 / (float)ATLAS_W, b->y1 / (float)ATLAS_H, sh, sh, lean);
            }
            quad(gx0, gy0, gx1, gy1, b->x0 / (float)ATLAS_W, b->y0 / (float)ATLAS_H,
                 b->x1 / (float)ATLAS_W, b->y1 / (float)ATLAS_H, argb, argb, lean);
        }
        pen += b->xadvance * k;
    }
    return pen;
}

float nv2a_ovl_text_width(float px, int flags, const char *s)
{
    return text_run(0, 0, px, 0, flags, s, 0);
}

float nv2a_ovl_text(float x, float y, float px, uint32_t argb, int flags, const char *s)
{
    float w;
    if (!s_active || !s)
        return 0.0f;
    w = text_run(0, 0, px, 0, flags, s, 0);
    if (flags & NV2A_OVL_RIGHT)
        x -= w;
    else if (flags & NV2A_OVL_CENTER)
        x -= w * 0.5f;
    text_run(x, y, px, argb, flags, s, 1);
    return w;
}

static void ovl_draw_list(const OvlVert *v, size_t n, GLuint tex)
{
    if (!n)
        return;
    glBindTexture(GL_TEXTURE_2D, tex);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof *v), v, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)n);
}

/* Called by gl_flip with the default framebuffer bound and the window's
 * size. Returns 1 if anything was drawn (the caller then resets its GL state
 * cache). */
int nv2a_gl_overlay_frame(int win_w, int win_h)
{
    if (s_state < 0 || win_w <= 0 || win_h <= 0)
        return 0;
    s_nv = 0;
    s_shape_n = 0;
    s_active = 1;
    nv2a_gl_host_overlay();
    s_active = 0;
    if (!s_nv && !s_shape_n)
        return 0;
    if (!s_state && !ovl_init())
        return 0;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, win_w, win_h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(1, 1, 1, 1);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(0x0302, 0x0303);                /* SRC_ALPHA, ONE_MINUS_SRC_ALPHA */
    glUseProgram(s_prog);
    glUniform2fv(s_u_canvas, 1, (const GLfloat[2]){ NV2A_OVL_W, NV2A_OVL_H });
    glUniform1i(s_u_tex, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    ovl_draw_list(s_shape_v, s_shape_n, s_white_tex);
    if (s_have_font)
        ovl_draw_list(s_v, s_nv, s_tex);
    {
        /* RECOMP_GL_OVL_SHOT=<prefix>[,every]: the window as shown, overlay
         * included, as PNG every n-th frame with an overlay (testing). */
        static int every = -1;
        static unsigned n;
        static char prefix[256];
        if (every < 0) {
            const char *e = getenv("RECOMP_GL_OVL_SHOT"), *c;
            every = 0;
            if (e && *e) {
                c = strchr(e, ',');
                snprintf(prefix, sizeof prefix, "%.*s", c ? (int)(c - e) : (int)strlen(e), e);
                every = c ? atoi(c + 1) : 30;
                if (every <= 0) every = 30;
            }
        }
        if (every && (n++ % (unsigned)every) == 0) {
            uint8_t *px = (uint8_t *)malloc((size_t)win_w * win_h * 4);
            if (px) {
                char path[300];
                int y;
                glPixelStorei(GL_PACK_ALIGNMENT, 4);
                glReadPixels(0, 0, win_w, win_h, GL_RGBA, GL_UNSIGNED_BYTE, px);
                for (y = 0; y < win_h / 2; y++) {          /* GL rows are bottom-up */
                    uint8_t tmp[16384];
                    size_t row = (size_t)win_w * 4;
                    if (row > sizeof tmp) break;
                    memcpy(tmp, px + y * row, row);
                    memcpy(px + y * row, px + (win_h - 1 - y) * row, row);
                    memcpy(px + (win_h - 1 - y) * row, tmp, row);
                }
                snprintf(path, sizeof path, "%s%05u.png", prefix, n - 1);
                stbi_write_png(path, win_w, win_h, 4, px, win_w * 4);
                free(px);
            }
        }
    }
    return 1;
}
