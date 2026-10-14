# Project layout

What sits where, who writes it, and whether git tracks it. Every path is
relative to the repo root; the scripts find the root from their own location.

Legend: **[git]** tracked · **[ignored]** gitignored (local only) ·
**[generated]** written by a tool, safe to delete and regenerate.

## The two stages

```
            STAGE 1: LIFT (Python)                     STAGE 2: BUILD (C)
game/default.xbe ──► xboxrecomp/tools/* ──► work/gen/*.c ──► CMake + GCC ──► work/build-*/mc3_recomp
   (x86 machine code)  parse, disassemble,   (portable C)     + src/*.c          (Linux binary)
                       identify, translate                    + xboxrecomp/src   or .nro (Switch)
```

**Python is only used in stage 1.** The toolkit's recompiler is a set of
Python programs that read the Xbox executable and *write C source*:

| Step | Tool (`xboxrecomp/tools/`) | Does | Output |
|---|---|---|---|
| parse | `xbe_parser` | reads the XBE header: sections, entry point, kernel imports | `work/analysis/xbe_*` |
| disassemble | `disasm` (uses **capstone**, a Python x86 disassembler) | finds every function and jump target | `work/analysis/disasm/` |
| identify | `func_id` | recognises library code (C runtime, XDK) | `work/analysis/func_id/` |
| ABI | `abi_analysis` | recovers calling conventions and argument counts | `work/analysis/abi/` |
| translate | `recomp` | turns each x86 function into a C function | `work/gen/` |
| (helpers) | `seed_from_log`, `xiso` | seed candidates from a run log; ISO unpacking | `config/`, `game/` |

**Stage 2 is plain C.** CMake and GCC (or devkitA64 for the Switch) compile
the generated C together with our code and the toolkit's C runtime. No Python
runs here. So: after changing C files, `make build` is enough; Python only runs
again when the C has to be *regenerated* (`make lift / regen / relift`).

## Tree

```
mcdub3-sw/
├── Makefile                  [git] the development loop; `make` lists targets
├── CMakeLists.txt            [git] stage 2: one C project, Linux and Switch hosts
├── README.md                 [git] what this is, quick start
├── CONTRIBUTING.md           [git] VM setup, the loop, disk budget, sending to the Switch
├── CLAUDE.md                 [git] engineering notes: rules, facts about MC3, status
├── TODO.md                   [ignored] personal work list: bugs, features, decisions to evaluate
├── .gitignore                [git]
│
├── src/                      [git] OUR C code, compiled with the lifted code
│   ├── main.c                  boot: load XBE, map Xbox memory, start devices, jump to the game
│   ├── recomp_manual.c         hand-written replacements for specific lifted functions
│   └── switch_nx.c             Switch only: log file, settings file, crash handler
│
├── config/                   [git] per-game inputs to stage 1 and 2
│   ├── seed_functions.json     extra function start addresses for the disassembler (see "seeds")
│   └── xbe_facts.h             entry point + title ID, written by `make lift`, used by main.c
│
├── tools/                    [git] scripts behind the make targets
│   ├── env.sh                  shared paths (game/, work/), picks a Python with capstone
│   ├── first_lift.sh           make lift: parse + write xbe_facts.h + full regen
│   ├── regen.sh                make regen / relift: the stage-1 pipeline
│   ├── build_linux.sh          make build: CMake + Ninja into work/build-linux*
│   ├── run_linux.sh            make run / run-gdb / run-headless / trap
│   └── extract_disc.sh         make extract: ISO → game/
├── switch/
│   └── build.sh              [git] make switch: NRO build + SD-card staging
├── vm/
│   └── setup-ubuntu.sh       [git] make setup: packages, devkitPro, Python venv
│
├── docs/
│   ├── project-layout.md     [git] this file
│   ├── decisions/            [git] ADRs: why things are the way they are
│   │   ├── 0001-native-aarch64-not-carafe.md
│   │   ├── 0002-vendored-toolkit-own-snapshot.md
│   │   └── 0003-linux-vm-debug-loop.md
│   └── drafts/               [ignored] unpublished texts (e.g. the nfsu2-sw licensing issue)
│
├── xboxrecomp/               [git] THE TOOLKIT, vendored (ADR 0002): our own copy, edited in place
│   ├── tools/                  stage 1, Python: xbe_parser, disasm, func_id, abi_analysis,
│   │                           recomp, seed_from_log, xiso, ... (+ their unit tests)
│   ├── src/                    stage 2, C runtime libraries linked into the game:
│   │   ├── kernel/               Xbox kernel replacement: memory map, files, threads, sync
│   │   ├── platform/             host layer: Win32 API on Linux/Switch (POSIX, libnx)
│   │   ├── nv2a/                 GPU register model (from xemu)
│   │   ├── nv2a_gl/  nv2a_vk/    renderers: OpenGL (Linux) / Vulkan (Switch NVK, Linux)
│   │   ├── apu/  audio/          sound chip model (from xemu), DirectSound, SDL output
│   │   ├── usb/  input/          USB controllers the game talks to; host pads, Joy-Cons
│   │   ├── d3d/  video/          D3D8 layer, movie/framebuffer helpers
│   ├── templates/runtime/      recomp_types.h: the register model the generated C includes
│   ├── include/                public header (xboxrecomp.h)
│   └── docs/  tests/           toolkit documentation and C tests
│
├── game/                     [ignored] the extracted disc (game data; never committed)
│   ├── default.xbe             the game executable: input to stage 1, loaded again at runtime
│   ├── default_analysis.json   [generated] XBE parse, read by the disassembler
│   ├── assets_x.dat(_1.._5), banks.dat, audio/, flash/, media/, resources/, video/ (Bink .bik)
│   └── dashupdate.xbe, downloader.xbe, update.xbe   Xbox system tools on the disc; not used
│
└── work/                     [ignored] EVERYTHING GENERATED; delete any part and rebuild it
    ├── analysis/             [generated] stage-1 intermediate results (make lift / regen)
    │   ├── xbe_summary.txt, xbe_analysis.json    the XBE parse
    │   ├── disasm/             functions.json, labels.json, xrefs.json, strings.json, asm/
    │   ├── func_id/            library functions recognised
    │   ├── abi/                calling conventions
    │   ├── recomp/summary.json translation statistics
    │   └── recomp.log          full translator output (incl. untranslated-instruction tally)
    ├── gen/                  [generated] stage-1 OUTPUT: the game as C (~90 files, ~3 M lines)
    │   ├── recomp_0000.c … recomp_0086.c   the lifted functions, 250 per file
    │   ├── recomp_dispatch.c               address → function table (indirect calls)
    │   ├── recomp_stubs_unresolved.c       placeholders for calls to undetected functions
    │   ├── recomp_funcs.h                  declarations of every lifted function
    │   └── recomp_types.h                  copy of the toolkit's register model
    ├── build-linux/          [generated] stage-2 build tree (make build): CMake/Ninja files,
    │   │                       object files for gen/, src/ and the toolkit runtime
    │   └── mc3_recomp          the Linux executable
    ├── build-linux-vk/, build-linux-dbg/      variants (VULKAN=1, DEBUG=1)
    ├── build-switch/, build-switch-vk/        Switch build trees (make switch); mc3_recomp.nro/.elf
    ├── switch_sd/            [generated] SD-card staging: switch/mcdub3/{mc3_recomp.nro, game/}
    ├── elf/                  [generated] one ELF per staged NRO, to resolve console crash addresses
    ├── logs/                 [generated] every make lift/build/run/switch log, timestamped
    ├── venv/                 [generated] Python + capstone for stage 1 (make setup)
    └── deps/                 [optional] Switch extras built by hand: mesa NVK SDK, glslang, FFmpeg
```

## What to delete when

| Want to | Delete | Then |
|---|---|---|
| Rebuild from scratch | `make clean-build` (`work/build-*`) | `make build` |
| Re-lift from scratch | `make clean-gen` (`work/gen`, `work/analysis`) | `make lift` |
| Free disk | old `work/build-*`, `work/logs/*` | nothing |
| Reset Python | `work/venv` | `make setup` |

Never needed: deleting anything outside `work/` (and `game/` only holds the disc).
