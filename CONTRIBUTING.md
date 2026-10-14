# Contributing: development guide

The technical source of truth: how the project turns the Xbox game into a
Switch program, what sits where, how to set up the development VM, and the
day-to-day loop. Decisions and their reasons: [docs/decisions/](docs/decisions/).

## How it works: two stages

```
STAGE 1: LIFT (Python)                            STAGE 2: BUILD (C)
game/default.xbe ─► xboxrecomp/tools ─► work/gen/*.c ─► CMake + GCC ─► work/build-linux/mc3_recomp
 (x86 machine code)  parse, disassemble,  (the game as C)  + src/*.c          or devkitA64 ─► .nro (Switch)
                     identify, translate                   + toolkit runtime
```

**Stage 1, the lift (`make lift`, `make regen`, `make relift`).** The
toolkit's recompiler is a set of Python programs (`xboxrecomp/tools/`). They
read the Xbox executable, disassemble it (with the *capstone* library), find
every function, recognise library code, recover how each function takes its
arguments, and translate every x86 function into a C function. Purpose: turn
machine code that only runs on an Xbox CPU into portable C that any compiler
can build. Output: `work/gen/` (~90 files, ~3 M lines) plus intermediate
results in `work/analysis/`. It runs once, then again only when the C must be
regenerated: after changing seeds (`config/seed_functions.json`, full
`make regen`), or hand-written overrides in `src/recomp_manual.c` / the
translator (`make relift`, skips the analysis steps).

The stage-1 steps, each a Python tool in `xboxrecomp/tools/`:

| Step | Tool (`xboxrecomp/tools/`) | Does | Output |
|---|---|---|---|
| parse | `xbe_parser` | reads the XBE header: sections, entry point, kernel imports | `work/analysis/xbe_*` |
| disassemble | `disasm` (uses **capstone**, a Python x86 disassembler) | finds every function and jump target | `work/analysis/disasm/` |
| identify | `func_id` | recognises library code (C runtime, XDK) | `work/analysis/func_id/` |
| ABI | `abi_analysis` | recovers calling conventions and argument counts | `work/analysis/abi/` |
| translate | `recomp` | turns each x86 function into a C function | `work/gen/` |
| (helpers) | `seed_from_log`, `xiso` | seed candidates from a run log; ISO unpacking | `config/`, `game/` |

**Stage 2, the build (`make build`, `make switch`).** Plain C: CMake + GCC
(Linux) or devkitA64 (Switch) compile three things into one program:

- `work/gen/`: the game itself, as generated C;
- `xboxrecomp/src/`: the toolkit's runtime, which stands in for the Xbox: its
  kernel, memory map, GPU, sound chip and controllers;
- `src/`: our own code: boot (`main.c`), hand-written function replacements
  (`recomp_manual.c`), Switch glue (`switch_nx.c`).

Purpose: produce a native program for the host: a Linux binary to debug in a
window, or a Switch `.nro`. No Python runs here; after editing C, `make build`
is all it takes.

**Why two stages:** lifting is slow and only depends on the XBE, seeds and
overrides; building is the part you repeat all day. Keeping them apart means
an ordinary code change never re-runs the lift.

## Project layout

What sits where, who writes it, and whether git tracks it. Every path is
relative to the repo root; the scripts find the root from their own location.

Legend: **[git]** tracked · **[ignored]** gitignored (local only) ·
**[generated]** written by a tool, safe to delete and regenerate.

```
mcdub3-sw/
├── Makefile                  [git] the development loop; `make` lists targets
├── CMakeLists.txt            [git] stage 2: one C project, Linux and Switch hosts
├── README.md                 [git] what this is, quick start
├── CONTRIBUTING.md           [git] technical source of truth: stages, layout, VM, the loop
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

### What to delete when

| Want to | Delete | Then |
|---|---|---|
| Rebuild from scratch | `make clean-build` (`work/build-*`) | `make build` |
| Re-lift from scratch | `make clean-gen` (`work/gen`, `work/analysis`) | `make lift` |
| Free disk | old `work/build-*`, `work/logs/*` | nothing |
| Reset Python | `work/venv` | `make setup` |

Never needed: deleting anything outside `work/` (and `game/` only holds the disc).

## Development environment

One Ubuntu ARM64 VM on the Mac (Apple Silicon) does everything: lifting, the
windowed Linux build with gdb, and Switch cross-builds. Parallels Desktop or
UTM both work. Why: [ADR 0003](docs/decisions/0003-linux-vm-debug-loop.md).

## Create the VM (Parallels Desktop)

1. Parallels' installation assistant → **Download Ubuntu Linux**. It installs
   Parallels' own ARM64 image (22.04 at the time of writing) with Parallels
   Tools already in it. That is the supported route: on 2026-10-14, Ubuntu's
   24.04 ARM64 desktop ISO failed to boot in Parallels (UEFI file system
   error), and a server install + desktop could not take Parallels Tools.
   22.04 is fine for this project; see "Ubuntu 22.04" below. Before the first
   boot, tick **Customize settings before installation** (or set these
   afterwards with the VM shut down):
   - **Hardware → CPU & Memory:** manual, **6 cores, 10 GB**. Compiling the
     lifted C takes ~1.5 GB per parallel job; the scripts size `JOBS` from
     free memory.
   - **Hardware → Hard Disk:** **64–80 GB**, expanding (the default). An
     expanding disk only uses what is actually written; see the budget below.
   - **Hardware → Graphics:** leave Parallels' default; check it after
     install (below).
   - **Options → Sharing:** share **only the repo folder** (`mcdub3-sw`), not
     the whole home folder. It shows up under `/media/psf/`.
2. Install **Parallels Tools** when prompted: shared folders and the
   graphics driver need it.
3. Symlink the share somewhere short: `ln -s /media/psf/mcdub3-sw ~/mcdub3-sw`.
   Every path is relative to the repo root: the disc in `game/`, everything
   generated in `work/` (both gitignored). Run the scripts from the root.
4. `cd ~/mcdub3-sw && bash vm/setup-ubuntu.sh`, then open a new shell.

### Ubuntu 22.04

Works for everything except the **Linux Vulkan build** (`VULKAN=1
tools/build_linux.sh`): the toolkit's Vulkan renderer needs glslang 12+ (the
`glslang-default-resource-limits` CMake target), and 22.04 ships 11.8. The
default OpenGL build, the lift pipeline (Python 3.10 is the minimum) and the
Switch builds (devkitPro brings its own toolchain) are unaffected. If you
need Linux Vulkan later, build glslang from source into `work/deps` and
pass `-Dglslang_DIR`, or move to 24.04.

### Check the VM (5 minutes, before anything else)

```sh
uname -m               # aarch64
getconf PAGESIZE       # 4096 (the Xbox memory mirrors assume 4 KB pages)
nproc; free -g         # the cores/RAM you gave it
df -h ~                # free space on the VM disk
glxinfo -B | grep -E 'renderer|core profile version'
vulkaninfo --summary 2>/dev/null | grep -E 'deviceName|apiVersion'
```

- `OpenGL core profile version` must be **3.3 or higher**.
- `OpenGL renderer`: a Parallels/virtio name means GPU-accelerated. `llvmpipe`
  means software rendering. That still works and is correct, just slower; fine
  for debugging, and performance is only ever measured on the Switch.
- Vulkan: whatever it lists (often only `llvmpipe` = lavapipe on the CPU) is
  fine; the Switch's Vulkan (NVK) gets tested on the console.

If GL is below 3.3: Parallels → Configure → Hardware → Graphics, try the
other options; otherwise run with `LIBGL_ALWAYS_SOFTWARE=1` (llvmpipe, GL 4.5).

### Disk budget

`game/` and `work/` live in the repo, i.e. on the **Mac's** disk (through the
shared folder); the VM disk only holds Ubuntu and the tools.

| What                                                   | Size                                  |
| ------------------------------------------------------ | ------------------------------------- |
| VM: Ubuntu desktop + build deps + devkitPro            | ~20 GB                                |
| Mac: extracted disc (`game/`)                          | 3.4 GB                                |
| Mac: lifted C + analysis (`work/gen`, `work/analysis`) | ~0.5 GB                               |
| Mac: one Linux build tree (`work/build-linux`)         | ~2–4 GB (lifted C without debug info) |
| Mac: one Switch build tree                             | ~2–4 GB                               |
| Mac: SD staging (`work/switch_sd`)                     | ~3.4 GB once you build for the Switch |

Disk savers, already the default: the lifted C is built without debug info
(backtraces and breakpoints still name every `sub_XXXXXXXX`;
`GEN_DEBUGINFO=1 tools/build_linux.sh` turns it on when you need line-level
stepping, at a few GB). Delete build trees you're not using
(`rm -rf work/build-*-lto`); `du -sh work/*` shows where space went.

**If builds through the shared folder are slow:** `export MC3_WORK=<a folder
on the VM disk>` moves only the generated files (lifted C, builds) off the
share. The disc stays in `game/`.

## Create the VM (UTM, alternative)

Virtualize → Linux → Ubuntu ARM64 ISO, **"Use Apple Virtualization" unchecked**
(the QEMU backend has GPU-accelerated GL), display **virtio-gpu-gl-pci**, the
same CPU/RAM/disk/sharing as above. Then the same checks.

## The loop

```
                                     # once: extracted disc in game/ (ISO: tools/extract_disc.sh)
tools/first_lift.sh                  # once: parse XBE, config/xbe_facts.h, full lift
tools/build_linux.sh                 # after every change to src/ or the toolkit
tools/run_linux.sh                   # window; GDB=1 / HEADLESS=1 / SECS=n
LIFT_ONLY=1 tools/regen.sh           # after editing recomp_manual.c or the translator
tools/regen.sh                       # after changing config/seed_functions.json
switch/build.sh                      # when it's time for Eden / the console
```

| Rung                   | When                        | What it catches                                                     |
| ---------------------- | --------------------------- | ------------------------------------------------------------------- |
| Linux build (VM)       | every change                | boot, kernel calls, rendering, audio, game logic                    |
| Eden (Switch emulator) | before the console          | packaging, paths, libnx glue                                        |
| Switch hardware        | regularly, not every change | unaligned atomics, memory/aliasing, threads on Horizon, performance |

The VM's CPU is ARM64 like the Switch's, so most "x86 assumptions" (unaligned
atomics, memory ordering) show up here before the console. The VM's GPU is
virtual: fine for correctness, meaningless for performance.

## Getting the game into the VM

Put the **extracted** disc (`default.xbe` + data) in the repo's `game/`
folder on the Mac (gitignored). The scripts use it from there through the
shared folder; nothing is copied to the VM disk. Run everything from the repo
root:

```sh
cd ~/mcdub3-sw
bash tools/first_lift.sh
```

Only for an ISO: `tools/extract_disc.sh <file.iso>` unpacks it into `game/`.
`MC3_GAME_DIR=<folder>` points the scripts at a disc elsewhere.

## devkitPro on ARM64

ARM64 Linux is a supported devkitPro host: their official `devkitpro/devkita64`
Docker image is built for both linux/amd64 and linux/arm64. `vm/setup-ubuntu.sh`
runs devkitPro's own installer; if that ever fails, build the Switch NRO inside
`docker run -v ~/mcdub3-sw:/src devkitpro/devkita64`, which
runs natively on the VM. The Linux debug loop is unaffected either way.

## Sending builds to the Switch

- **nxlink** (fastest): open hbmenu, press **Y** (netloader), then
  `nxlink -s work/switch_sd/switch/mcdub3/mc3_recomp.nro`. `-s` keeps the
  connection open and streams the log to your terminal. The game data must
  already be on the card at `sdmc:/switch/mcdub3/game/`.
- **SD card**: copy `work/switch_sd/switch/mcdub3/` to the card.
- Start with **title takeover** (hold R while launching a game). Applet mode
  leaves too little memory.
