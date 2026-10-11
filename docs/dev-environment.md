# Development environment

One Ubuntu ARM64 VM in UTM on the Mac (Apple Silicon) does everything: lifting,
the windowed Linux build with gdb, and Switch cross-builds. Why: [ADR 0003](decisions/0003-linux-vm-debug-loop.md).

## Create the VM

1. Install [UTM](https://mac.getutm.app) and download an **Ubuntu LTS Desktop
   ISO for ARM64** (24.04 or 26.04). If you only find the Server ISO for ARM64,
   install that and add a desktop: `sudo apt install ubuntu-desktop-minimal`.
2. New VM → **Virtualize** (not Emulate) → Linux → the ISO. Leave
   **"Use Apple Virtualization" unchecked**: the QEMU backend is the one with
   GPU-accelerated OpenGL for Linux guests.
   - **Memory: 10 GB** (16 GB Mac). Compiling the lifted C takes ~1.5 GB per
     parallel job; the scripts size `JOBS` from free memory.
   - **CPU: 6–8 cores.**
   - **Disk: 120 GB.** The disc (~4 GB), the lifted C (~100 MB+ of source),
     several build trees and SD staging add up.
   - Display: **virtio-gpu-gl-pci (GPU supported)** for an accelerated GL window.
3. Shared directory: this repo (`mcdub3-sw`), mounted in the VM, e.g.
   `~/mcdub3-sw`. Only the repo is shared. **Game data, lifted C and
   builds stay on the VM's own disk** in `~/mc3-work` (`$MC3_WORK`): building
   ~100 MB of generated C through a shared folder is very slow.
   - Alternative: clone the repo inside the VM and push/pull, if the share is
     slow or awkward with git.
4. In the VM: `bash ~/mcdub3-sw/vm/setup-ubuntu.sh`, then open a new shell.

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

Copy the ISO into the VM (UTM shared folder, or `scp`), then
`tools/extract_disc.sh path/to.iso`. Keep the ISO out of the repo; `source/`
and `*.iso` are gitignored anyway.

## devkitPro on ARM64

ARM64 Linux is a supported devkitPro host: their official `devkitpro/devkita64`
Docker image is built for both linux/amd64 and linux/arm64. `vm/setup-ubuntu.sh`
runs devkitPro's own installer; if that ever fails, build the Switch NRO inside
`docker run -v ~/mc3-work:/work -v ~/mcdub3-sw:/src devkitpro/devkita64`, which
runs natively on the VM. The Linux debug loop is unaffected either way.

## Graphics in the VM

The window needs OpenGL 3.3 core (`nv2a_gl`). UTM's accelerated display
(virtio-gpu-gl, QEMU backend) passes GL through to the Mac's GPU. Check with
`glxinfo -B` (package `mesa-utils`). If it reports less than 3.3, or rendering
is broken, rule the VM out first:

- `LIBGL_ALWAYS_SOFTWARE=1 tools/run_linux.sh`: Mesa llvmpipe, GL 4.5 on the
  CPU. Slow, but a correct reference renderer.
- `VULKAN=1`: lavapipe (Vulkan on the CPU). There is no GPU Vulkan in the VM;
  that is fine, as the Switch's Vulkan (NVK) gets tested on the console.

`getconf PAGESIZE` should say 4096 (Ubuntu's generic ARM64 kernel; the Switch
uses 4 KB pages too). The Xbox memory mirrors assume 4 KB pages.

## Sending builds to the Switch

- **nxlink** (fastest): open hbmenu, press **Y** (netloader), then
  `nxlink -s $MC3_WORK/switch_sd/switch/mcdub3/mc3_recomp.nro`. `-s` keeps the
  connection open and streams the log to your terminal. The game data must
  already be on the card at `sdmc:/switch/mcdub3/game/`.
- **SD card**: copy `$MC3_WORK/switch_sd/switch/mcdub3/` to the card.
- Start with **title takeover** (hold R while launching a game). Applet mode
  leaves too little memory.
