/* Every retail default.xbe of the Remix this lift runs, checked at boot
 * (src/main.c). Same format as config/dub/xbe_editions.h:
 *
 *   X(name, FNV-1a 64 of the whole default.xbe, certificate region, version)
 *
 * Only the European disc so far (2026-10-24). Another edition joins only
 * after comparing its XBE: a disc whose code differs needs a lift of its
 * own. The name is used for game-remix-<name>/ folders and EDITION=. */
#pragma once

#define MC3_XBE_EDITIONS(X) \
    X("eur", 0x4438fd3ef9473ce0ull, 0x4u, 0x6u)   /* PAL, En/Fr/De/Es/It (Redump: Europe) */
