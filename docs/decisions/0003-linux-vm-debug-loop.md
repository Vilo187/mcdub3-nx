# 3. Debug on Linux in an ARM64 VM; build for the Switch periodically

Date: 2026-10-11

## Context

The Switch and Linux builds run the same lifted code and runtime; only a thin
platform layer differs. The runtime has no native macOS support. The
development machine is an M1 Pro Mac (16 GB).

## Decision

One Ubuntu ARM64 VM (Parallels Desktop or UTM) for lifting, the windowed Linux build with gdb, and
Switch cross-builds (CONTRIBUTING.md). The repo is shared from the
Mac into the VM, and every path is relative to the repo root: the disc in
`game/`, generated files in `work/` (both gitignored). Nothing defaults to a
path outside the repo (2026-10-14).

Trade-off: `work/` then sits on the shared folder, so compiling the lifted C
goes through Parallels' file sharing and builds use the Mac's disk. If builds
get slow, `MC3_WORK=<folder on the VM disk>` moves only the generated files.

Debug ladder: Linux (every change) → Eden (before the console) → hardware
(hardware-only bugs, performance).

## Why

- A window and a debugger on every change; lifted functions are C symbols.
- ARM64 like the Switch: AArch64-specific faults show up before the console.
- No macOS port of the runtime to write and maintain.

## Rejected

- Headless Docker only: no live window, slower x86-64 emulation for some images.
- A native macOS runtime port: real work that doesn't move the port forward.
