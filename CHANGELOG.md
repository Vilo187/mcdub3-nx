# Changelog of this fork

Everything this fork changes compared to [laureladastra/mcdub3-nx](https://github.com/laureladastra/mcdub3-nx) **v0.2.0**.

🤖 **Vibe-coded with Claude Opus 5.5** (Anthropic) in Claude Code. Claude profiled the game on Linux (perf) and on the Switch (its own sampling profiler, see *Diagnostics*), and tested every step on a real Switch (V1, HOS 21.2, Atmosphère) over the network.

Almost every change can be switched off with a setting (in `mcdub3_env.txt` on the Switch, on the command line elsewhere), so a regression can be told apart from the rest. Defaults are the new behaviour.

## Results

| Switch (V1), career race | Upstream v0.2.0 | This fork |
| --- | --- | --- |
| No overclock | ~20 FPS, 30 FPS cap | ~30 FPS in the 30 FPS mode |
| Overclocked | 20-25 FPS, 30 FPS cap | 45-60 FPS in the 60 FPS driving mode |

On the Switch the game now runs into a plain CPU limit: in the dense city at the race start the game's own thread and the GL thread are both busy all the time.

## 60 FPS mode

| Change | Setting |
| --- | --- |
| *Extras → Graphics → Frame Rate*: 30 FPS, 60 FPS while driving (menus stay at 30) or 60 FPS always | `MC3_FPS60=0/1/2` |
| The game steps its world by a fixed 1/30 s; in the 60 FPS mode the step is the real frame time (clamped to 1/60..1/20 s), so driving speed and the race clock stay right at any frame rate (checked against the wall clock) | |
| D3D's flip interval 2 becomes 1 | |
| Triple buffering in the pushbuffer executor: a frame a little over 16.7 ms no longer waits a whole extra vblank | `MC3_FLIP_AHEAD=0` |
| A 120 Hz virtual vblank (the flip intervals are doubled to match): flips retire on an 8.3 ms grid | `MC3_VBLANK_120=0` |
| D3D's `Swap` may run two frames ahead of the GPU instead of one | `MC3_FRAMES_AHEAD=1` |
| The game called D3D's `BlockUntilIdle` twice a frame (`sub_000A97B0`), draining the whole emulated GPU pipeline each time; skipped | `MC3_IDLE_WAIT=1` |

## Rendering (GL thread)

| Change | Setting |
| --- | --- |
| **Zero-copy vertex data**: the executor writes vertices, packed attributes and 16-bit indices straight into a persistently mapped GL buffer; the GL thread no longer copies every vertex a second time (~30% of that thread) | `RECOMP_GL_ZEROCOPY=0` |
| Persistent, fenced vertex and index rings instead of map/unmap per draw | `RECOMP_GL_PERSIST=0` |
| Vertex attribute formats set once (`glVertexAttribFormat`/`glBindVertexBuffer`) | `RECOMP_GL_VAB=0` |
| Only the render-state groups and uniforms that changed are sent to Mesa (each GL call costs on the Switch, which has no threaded Mesa) | |
| Vertex shader constants: per-row version numbers instead of comparing 3 KB per draw | |
| Combiner colours converted only when they change; surface lookups memoised | |
| Texture checks: static textures every 4th frame, rewritten ones by copy-and-compare instead of hashing, linear and paletted ones included; a texture rewritten within a frame is checked at every 4th use instead of every use (MC3's HUD atlas: 24 uses, ~1 change a frame) | `RECOMP_GL_TEX_CHECK_EVERY=1`, `RECOMP_GL_TEX_EVERY_USE=1` |
| Paletted textures decoded in the byte order the driver stores as it is: RGBA on the Switch (nouveau converted BGRA texel by texel), BGRA elsewhere | `RECOMP_GL_PAL_RGBA=0/1` |
| Faster texture unswizzling (lookup tables) | |
| A cheaper clock for the GL thread's own statistics | |

## Runtime and threads

| Change | Setting |
| --- | --- |
| D3D's vertex transform used by the game everywhere (`sub_000818F0`, vector × 4x4 matrix on the emulated x87): native code instead of the lifted x87 stack | `MC3_NATIVE_XFORM=0` |
| The game's timer thread polled with `Sleep(0)` in a loop (41% of the CPU on Linux); now sleeps | `MC3_TIMER_SLEEP_MS=0` |
| D3D's vblank handler spun under the guest lock until the emulation acknowledged it; acknowledged at once | `MC3_VBLANK_ACK=0` |
| The guest lock wakes waiters only when someone waits (every broadcast is a system call on Horizon) | |
| Wake-ups of the executor's helper threads once per sleep instead of at every turn of every poll loop of the game (12.7% of the main thread on the console) | |
| The executor is woken before the game blocks on the GPU, not at its next 1 ms timeout | |
| Events, semaphores, mutexes and the game's kernel events wake only when a thread waits on them (each wake is a system call on Horizon) | |
| D3D's GPU fence wait sleeps until the executor writes the semaphore instead of yielding in a loop (200,000 turns a second, each a lock hand-over) | `MC3_FENCE_SLEEP=0` |
| The GL thread gets a CPU core of its own; every other thread keeps off it | `RECOMP_NX_GL_CORE=-1` |
| `getenv` out of hot paths (it ran on every kernel call and every untranslated instruction) | |
| Pushbuffer methods that only update the register shadow are learned and skip the method switch | |
| Statistics about unhandled methods only on request | `RECOMP_PB_UNHANDLED=1` |
| The lifted code reads its thread pointer from `TPIDR_EL0` instead of calling libnx's `__aarch64_read_tp` at every register access (141,798 call sites) | `MC3_TP_EL0=OFF` (CMake) |

## Build

- Hidden visibility, `-fomit-frame-pointer`, `-fno-plt` for the lifted code.
- Optional LTO and plain (non-volatile) guest memory with compiler barriers on every loop back edge (`make switch LTO=1 MEMFAST=1`, `tools/recomp/loop_barrier.py`).
- Rootless Switch builds with bubblewrap: `make switch BWRAP=1`.

## Features

- **Extras menu** (R3 in the pause menu, L3+R3 anywhere): free camera (the game's menus and HUD hidden while flying), frame rate counter, vehicle switcher with all 69 vehicles (including the police Impalas, the police motorcycle and Skully's chopper), world (traffic, pedestrians, props, weather, slow motion), graphics (frame rate mode, resolution, FXAA, sharpening, texture filtering), Rockstar's developer switches and, in career mode only, cheats (unlock all, infinite money, +$100,000). *Save Settings* writes to `mcdub3_env.txt`.
- **Apply Vehicle Now (via Garage)** (career mode, from the pause menu): the new car without waiting for the next race. The game only builds a car when it loads the city, so Extras drives the pause menu like a player would (*Go to Garage*, Yes, then *Exit the garage*, Yes) and checks the pause menu's state between the steps.
- **Mods folder** `mods/`: replacement disc files (`files/`, `packs/<name>/files/`), textures, the menu font and extra settings.
- `MC3_ARGS`: Rockstar's 172 developer command-line parameters.

## Diagnostics

- `MC3_PERF=10`: a report every 10 s, now with the GL thread's time (idle, draws, µs per draw, texture work, swap), the executor's waits (traps, flip stalls, vblank) and the CPU per thread.
- `MC3_FENCE_STATS=1`: the game's waits for the GPU by caller.
- `RECOMP_YIELD_STATS=1`: where the game's main thread yields in poll loops.
- `MC3_PROF=1`: a sampling profiler on the Switch (`prof.bin`, `prof_threads.txt`); `tools/prof_nx.py` and `tools/prof_lines.py` name the hot functions and source lines against the build's ELF.
- The log goes to the SD card also when the game is started over nxlink.

## Known limits

- The free camera shows only the part of the city loaded around the car; flying further shows an empty city.
- A vehicle change applies from the next race or garage exit; in career mode *Apply Vehicle Now* makes that garage visit for you.
- Upstream's work after v0.2.0 (the *Remix* edition, packaging) is not merged into this branch.
