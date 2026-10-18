/*
 * nv2a_gl.h -- OpenGL renderer for the NV2A pushbuffer executor.
 *
 * Call nv2a_gl_install() before the title starts rendering, with the
 * executor enabled (RECOMP_PB_EXEC). The window and GL context are created
 * on the executor's thread on first use.
 */
#ifndef NV2A_GL_H
#define NV2A_GL_H

void nv2a_gl_install(void);

/* A static screen is up (the title is loading behind it): compile the
 * remembered shader programs now, a few a frame, when they were not
 * compiled at start-up (no program binaries). Any thread. */
void nv2a_gl_prewarm_start(void);

#endif
