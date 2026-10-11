# Development environment

One Ubuntu ARM64 VM in UTM on the Mac (Apple Silicon) does everything: lifting,
the windowed Linux build with gdb, and Switch cross-builds. Why: [ADR 0003](decisions/0003-linux-vm-debug-loop.md).

## Create the VM

1. Install [UTM](https://mac.getutm.app) and download **Ubuntu 24.04 Desktop, ARM64**.
2. New VM → **Virtualize** (not Emulate) → Linux → the ISO.
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

`vm/setup-ubuntu.sh` runs devkitPro's own installer. If it has no package for
ARM64 Linux at the time you run it, use devkitPro's Docker image for Switch
builds only (`devkitpro/devkita64`, x86-64, runs emulated and slower), or build
the Switch NRO on any x86-64 Linux machine. The Linux debug loop is unaffected.

## Sending builds to the Switch

- **nxlink** (fastest): open hbmenu, press **Y** (netloader), then
  `nxlink -s $MC3_WORK/switch_sd/switch/mcdub3/mc3_recomp.nro`. `-s` keeps the
  connection open and streams the log to your terminal. The game data must
  already be on the card at `sdmc:/switch/mcdub3/game/`.
- **SD card**: copy `$MC3_WORK/switch_sd/switch/mcdub3/` to the card.
- Start with **title takeover** (hold R while launching a game). Applet mode
  leaves too little memory.
