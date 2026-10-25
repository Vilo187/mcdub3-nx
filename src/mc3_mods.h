/* mc3_mods.h -- the mods folder (mc3_mods.c). */
#ifndef MC3_MODS_H
#define MC3_MODS_H

#include <stddef.h>

/* Index the mods folder beside the disc folder (or MC3_MODS_DIR) and apply
 * its settings.txt. Once, before the runtime reads its settings. */
void mc3_mods_init(const char *game_dir);
/* <mods>/<name> into out if that file exists: 1, else 0. */
int  mc3_mods_path(const char *name, char *out, size_t size);
/* The mods folder, or NULL when there is none. */
const char *mc3_mods_dir(void);

#endif
