# 1. Compile the lifted C natively for the Switch, not the Windows EXE through Carafe

Date: 2026-10-11

## Context

xboxrecomp lifts `default.xbe` to C. That C can be compiled into a Windows x64
`.exe`, and Carafe can run Windows `.exe`s on the Switch (Wine + Box64/FEX +
DXVK). nfsu2-sw instead compiles the same C straight for AArch64 with
devkitA64/libnx and reaches ~20 fps in races.

## Decision

Compile the lifted C natively for the Switch (and for Linux as the debug host).
No Windows step.

## Why

- Carafe would re-emulate x86 code we already recompiled: two translation
  layers on a CPU where the native build is already the bottleneck.
- The runtime rebuilds the Xbox address space with aliased views. nfsu2-sw
  needed Horizon-specific memory calls for that; under Wine + Box64 it is out
  of our control (Carafe ADR 0008: 32-bit processes get ~0.4–1.4 GB, varying
  per run).
- Crashes in native code name the lifted function; through Wine and a JIT
  they don't.

## Kept from Carafe

Ideas, not code: NSP packaging with hacBrewPack for a HOME Menu title later,
a loading screen, and this ADR format.
