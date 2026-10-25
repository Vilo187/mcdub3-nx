/* mc3_extras.h -- the Extras menu (mc3_extras.c) and the free camera
 * (mc3_freecam.c). */
#ifndef MC3_EXTRAS_H
#define MC3_EXTRAS_H

#include <stdint.h>

void     mc3_extras_init(void);          /* main.c, once the memory is mapped */
uint32_t mc3_rd32(uint32_t va);          /* guest RAM, bounds-checked */
void     mc3_wr32(uint32_t va, uint32_t v);
int      mc3_game_paused(void);          /* the pause menu is up */
int      mc3_in_career(void);

void mc3_freecam_init(void);
int  mc3_freecam_ready(void);            /* there is a camera to take over */
void mc3_freecam_enable(int on);
int  mc3_freecam_on(void);
/* Pad axes (+-32767), triggers (0..255), A held (faster), L3 held (slower). */
void mc3_freecam_input(int lx, int ly, int rx, int ry, int lt, int rt, int fast, int slow);

#endif
