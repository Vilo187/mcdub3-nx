/* Midnight Club 3: DUB Edition Remix -- the guest addresses shared code
 * needs (src/main.c). Mapped from the DUB Edition's (config/dub/game.h)
 * with tools/match_lifts.py's Mapper, 2026-10-24: the same operand of the
 * twin instruction in identical functions. */
#pragma once

#define MC3_ADDR_PB_STATE    0x00422D80u   /* D3D's push-buffer state: 12 of 12 references agree */
#define MC3_ADDR_FRAME_TIME  0x004B2D70u   /* the game's frame time: 1 of 1 */
#define MC3_CODE_LO          0x00011000u   /* .text .. XPP (ends 0x004476C8) */
#define MC3_CODE_HI          0x00448000u
