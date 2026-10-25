/* gl_overlay.h -- the host overlay drawn over the presented frame
 * (gl_overlay.c). Coordinates are a 1280x720 canvas stretched to the
 * window; colours are 0xAARRGGBB. Draw calls only count inside the host's
 * nv2a_gl_host_overlay(), which the renderer calls once a frame. */
#ifndef NV2A_GL_OVERLAY_H
#define NV2A_GL_OVERLAY_H

#include <stddef.h>
#include <stdint.h>

#define NV2A_OVL_W 1280.0f
#define NV2A_OVL_H 720.0f

/* Text flags. */
#define NV2A_OVL_ITALIC 0x01
#define NV2A_OVL_RIGHT  0x02             /* x is the right edge */
#define NV2A_OVL_CENTER 0x04             /* x is the centre */
#define NV2A_OVL_SHADOW 0x08             /* a drop shadow under the glyphs */
#define NV2A_OVL_UPPER  0x10             /* shown in capitals */

/* Shapes draw under all text of the same frame. */
void  nv2a_ovl_rect(float x, float y, float w, float h, uint32_t argb);
void  nv2a_ovl_grad(float x, float y, float w, float h, uint32_t top, uint32_t bottom);
/* A parallelogram: the top edge shifted right by `lean` (MC3's slanted bars). */
void  nv2a_ovl_slant(float x, float y, float w, float h, uint32_t top, uint32_t bottom,
                     float lean);
/* Text at (x, y) = top-left of the line, `px` high; returns its width. */
float nv2a_ovl_text(float x, float y, float px, uint32_t argb, int flags, const char *utf8);
float nv2a_ovl_text_width(float px, int flags, const char *utf8);

/* Supplied by the host (weak defaults in gl_overlay.c). */
void        nv2a_gl_host_overlay(void);
const void *nv2a_gl_host_font(size_t *len);

/* The renderer's call (nv2a_gl.c): 1 if anything was drawn. */
int nv2a_gl_overlay_frame(int win_w, int win_h);

#endif
