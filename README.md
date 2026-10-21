<p align="center"><img src="assets/game-logo.png" alt="Midnight Club 3: DUB Edition" width="560"></p>

The original Xbox release of *Midnight Club 3: DUB Edition*, recompiled and lifted instruction by instruction to C with [xboxrecomp](https://github.com/sp00nznet/xboxrecomp) and compiled natively for the Switch, Linux and macOS without requirement of emulation.

Organised after the release of [nfsu2-sw](https://github.com/antoxa2584x/nfsu2-sw), whose Switch layer has been a great inspiration for this toolkit.

> [!WARNING]
> **No game data is included.** You need your own copy of the Xbox disc.
>

## Platforms

| Platform        | Support | Tested       | Hardware        |
| --------------- | ------- | ------------ | --------------- |
| Nintendo Switch | ✅       | 21.0.0       | V1              |
| Ubuntu Linux    | ✅       | 22.04.5      | Virtual Machine |
| Mac             | ✅       | Sequoia 15.6 | M1              |
| Windows         | ❌       | -            | -               |

## Installation

Download from releases and extract to the root of your Nintendo Switch directory. Copy the game files to the `/game` directory within this folder.

## Technical Overview

| Area          | State                                                                                                                                                               |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Lift          | 21,585 functions, 2.98 M lines of C                                                                                                                                 |
| Movies        | Bink decoded natively with FFmpeg, sound in sync, upscaled on the GPU                                                                                               |
| Audio         | XACT sound effects and the WMA music                                                                                                                                |
| Graphics      | OpenGL renderer: up to 3x resolution, square pixels in 16:9, 4:3 screens filled to widescreen, mipmaps and 16x anisotropic filtering, FXAA, sharpening, motion blur |
| Texture packs | Optional HD replacements. Dumped from the game, upscaled (Real-ESRGAN), loaded in the background and DXT-compressed.                                                |
| Input         | Switch controllers; on Mac/Linux the keyboard and SDL pads (DualSense, Xbox)                                                                                        |
| Saves         | create, load and delete profiles                                                                                                                                    |
| Switch        | NRO and NSP forwarder                                                                                                                                               |

## ⚠️ &nbsp;Known Bugs

* loading screen can hang sometimes with a restart required
* intro menu navigation and rendering is slow

## Settings

Several settings can be configured for the runtime environment. These settings are `KEY=VALUE` pairs and can be configured by defining them in a text file and saving the file as `sdmc:/switch/mcdub3-nx/mcdub3_env.txt` on the Switch, one setting per line, with `#` starting a comment and excluding the setting from execution.

The full list, with every setting explained and commented out at its
default, is [switch/mcdub3_env.example.txt](switch/mcdub3_env.example.txt). The following settings are likely convenient to configure:


### Graphics

| Setting              | Default                    | What it does                                                                                                                                                        |
| -------------------- | -------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `RECOMP_GL_SCALE`    | `1`                        | Rendering resolution as a multiple of the Xbox's 720x480 (0.5 to 4). `1.5` gives 1280x720, the best match for a 720p screen; higher costs frame rate on the Switch. |
| `RECOMP_WIDESCREEN`  | `1`                        | Tells the game the TV is widescreen, so it renders 16:9 itself.                                                                                                     |
| `RECOMP_GL_SQUARE`   | `1`                        | Shows the widescreen picture at its true shape (square pixels).                                                                                                     |
| `RECOMP_GL_FILL`     | `1`                        | Menus and videos laid out for 4:3 fill the whole width instead of showing black side bands.                                                                         |
| `MC3_FILL_SKIP`      | `rockstar,sdlogo,mc3intro` | Movies that keep their original 4:3 framing (names from `game/video/`, without `.bik`).                                                                             |
| `RECOMP_GL_ASPECT`   | *(unset)*                  | `fill` stretches the picture to the whole screen, whatever its shape.                                                                                               |
| `RECOMP_GL_FXAA`     | `1`                        | Smooths jagged edges.                                                                                                                                               |
| `RECOMP_GL_SHARPEN`  | `0.5`                      | Sharpening strength, from 0 (off) to 1.                                                                                                                             |
| `RECOMP_GL_MIPS`     | `1`                        | Smoother textures in the distance, with less shimmering.                                                                                                            |
| `RECOMP_GL_ANISO`    | `16`                       | Keeps road textures sharp at shallow angles. 1 is off, 16 the maximum.                                                                                              |
| `RECOMP_GL_LOD_BIAS` | `-0.25`                    | Texture detail: negative is sharper, positive softer.                                                                                                               |
| `RECOMP_GL_UI_SCALE` | `2`                        | Draws menu and HUD art this many times larger. Whole numbers only; 1 is off.                                                                                        |

### Input and controllers

| Setting             | Default   | What it does                                                                                                                                 |
| ------------------- | --------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| `RECOMP_PAD_LAYOUT` | `label`   | Switch: `label` maps buttons by name (Switch A = Xbox A, as the on-screen prompts say). `position` maps them by position, as on an Xbox pad. |
| `RECOMP_NX_JOYCON`  | *(unset)* | Switch: `single` turns two detached Joy-Cons into one player each, held sideways.                                                            |
| `RECOMP_RUMBLE`     | `1`       | Rumble.                                                                                                                                      |
| `RECOMP_KEYBOARD`   | `1`       | Linux and macOS: the keyboard plays as player 1, alongside any connected controller.                                                         |

### Performance and logging

| Setting               | Default       | What it does                                                                                                                                                                                                               |
| --------------------- | ------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `RECOMP_GL_THREAD`    | `1` on Switch | Draws on a thread of its own.                                                                                                                                                                                              |
| `RECOMP_PROG_PREWARM` | *(unset)*     | Shader compiling. By default the remembered shaders compile one per frame behind the legal screen. `boot` compiles them all at start (a few seconds of black); `0` compiles none ahead (stutters when each is first used). |
| `RECOMP_FPS_LOG`      | `0`           | Writes the frame rate to the log every 10 seconds.                                                                                                                                                                         |
| `MC3_PERF`            | *(unset)*     | Writes a performance report every N seconds: where the game waits, CPU use per thread, audio pacing.                                                                                                                       |
| `RECOMP_QUIET`        | `1` on Switch | Fewer log lines; every line costs an SD-card write.                                                                                                                                                                        |

Some settings undo a fix, for comparison only: `MC3_FENCE_FIX`,
`MC3_FLIP_PACE`, `MC3_TRAP_HOOK`, `RECOMP_TIMER_BATCH`. Others are required
and set by the game itself: `RECOMP_VBLANK`, `RECOMP_PB_EXEC`, `RECOMP_USB`,
`RECOMP_AC97_READY`, `RECOMP_NET_LINK`, `RECOMP_FLIP_VBLANK` and
`RECOMP_APU_DSP_ACK`. Leave both groups alone. The developer traces are
described in the example file and in the source.

## License

The project's own code is under the MIT license ([LICENSE](LICENSE)). The
vendored toolkit in `xboxrecomp/` keeps its own license
([xboxrecomp/LICENSE](xboxrecomp/LICENSE), third-party parts in
`xboxrecomp/LICENSES/`). No game code or data is part of this repository.

## Legal

Midnight Club is a trademark of Take-Two Interactive / Rockstar Games. This is an unofficial fan project, not affiliated with or endorsed by them, Microsoft or Nintendo.