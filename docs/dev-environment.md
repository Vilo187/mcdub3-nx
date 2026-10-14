# Development environment

One Ubuntu ARM64 VM on the Mac (Apple Silicon) does everything: lifting, the
windowed Linux build with gdb, and Switch cross-builds. Parallels Desktop or
UTM both work. Why: [ADR 0003](decisions/0003-linux-vm-debug-loop.md).

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
   **Game data, lifted C and builds stay on the VM's own disk** in
   `~/mc3-work` (`$MC3_WORK`): building ~100 MB of generated C through a
   shared folder is very slow. (Alternative: clone the repo inside the VM.)
4. `bash ~/mcdub3-sw/vm/setup-ubuntu.sh`, then open a new shell.

### Ubuntu 22.04

Works for everything except the **Linux Vulkan build** (`VULKAN=1
tools/build_linux.sh`): the toolkit's Vulkan renderer needs glslang 12+ (the
`glslang-default-resource-limits` CMake target), and 22.04 ships 11.8. The
default OpenGL build, the lift pipeline (Python 3.10 is the minimum) and the
Switch builds (devkitPro brings its own toolchain) are unaffected. If you
need Linux Vulkan later, build glslang from source into `~/mc3-work/deps` and
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

### Disk budget (fits in ~60 GB)

| What | Size |
|---|---|
| Ubuntu desktop + build deps + devkitPro | ~20 GB |
| Extracted disc (`game/`) | ~4–7 GB |
| ISO | keep it **on the Mac**; extract straight from the share |
| Lifted C (`gen/`) + analysis | ~0.5 GB |
| One Linux build tree | ~2–4 GB (lifted C without debug info) |
| One Switch build tree | ~2–4 GB |
| SD staging (`switch_sd/`) | ~0: hard links to `game/` |
| Total, comfortable | **~35–45 GB** |

Disk savers, already the default: the lifted C is built without debug info
(backtraces and breakpoints still name every `sub_XXXXXXXX`;
`GEN_DEBUGINFO=1 tools/build_linux.sh` turns it on when you need line-level
stepping, at a few GB). Delete build trees you're not using
(`rm -rf ~/mc3-work/build-*-lto`); `du -sh ~/mc3-work/*` shows where space went.

## Create the VM (UTM, alternative)

Virtualize → Linux → Ubuntu ARM64 ISO, **"Use Apple Virtualization" unchecked**
(the QEMU backend has GPU-accelerated GL), display **virtio-gpu-gl-pci**, the
same CPU/RAM/disk/sharing as above. Then the same checks.

## The loop

```
tools/extract_disc.sh ~/mc3.iso      # once: disc -> $MC3_WORK/game
tools/first_lift.sh                  # once: parse XBE, config/xbe_facts.h, full lift
tools/build_linux.sh                 # after every change to src/ or the toolkit
tools/run_linux.sh                   # window; GDB=1 / HEADLESS=1 / SECS=n
LIFT_ONLY=1 tools/regen.sh           # after editing recomp_manual.c or the translator
tools/regen.sh                       # after changing config/seed_functions.json
switch/build.sh                      # when it's time for Eden / the console
```

| Rung | When | What it catches |
|---|---|---|
| Linux build (VM) | every change | boot, kernel calls, rendering, audio, game logic |
| Eden (Switch emulator) | before the console | packaging, paths, libnx glue |
| Switch hardware | regularly, not every change | unaligned atomics, memory/aliasing, threads on Horizon, performance |

The VM's CPU is ARM64 like the Switch's, so most "x86 assumptions" (unaligned
atomics, memory ordering) show up here before the console. The VM's GPU is
virtual: fine for correctness, meaningless for performance.

## Getting the game into the VM

Leave the ISO on the Mac and extract it straight through the shared folder,
e.g. put it in the repo's `source/` folder (gitignored) and run
`tools/extract_disc.sh ~/mcdub3-sw/source/<name>.iso`. Only the extracted files
land on the VM disk.

## devkitPro on ARM64

ARM64 Linux is a supported devkitPro host: their official `devkitpro/devkita64`
Docker image is built for both linux/amd64 and linux/arm64. `vm/setup-ubuntu.sh`
runs devkitPro's own installer; if that ever fails, build the Switch NRO inside
`docker run -v ~/mc3-work:/work -v ~/mcdub3-sw:/src devkitpro/devkita64`, which
runs natively on the VM. The Linux debug loop is unaffected either way.

## Sending builds to the Switch

- **nxlink** (fastest): open hbmenu, press **Y** (netloader), then
  `nxlink -s $MC3_WORK/switch_sd/switch/mcdub3/mc3_recomp.nro`. `-s` keeps the
  connection open and streams the log to your terminal. The game data must
  already be on the card at `sdmc:/switch/mcdub3/game/`.
- **SD card**: copy `$MC3_WORK/switch_sd/switch/mcdub3/` to the card.
- Start with **title takeover** (hold R while launching a game). Applet mode
  leaves too little memory.
