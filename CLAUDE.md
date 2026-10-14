# Midnight Club 3: DUB Edition (Xbox) static recompilation: engineering notes

Xbox *Midnight Club 3: DUB Edition*, lifted to C with xboxrecomp and built for
Linux (debug host) and Nintendo Switch (libnx NRO). Modelled on nfsu2-sw.
Keep this file current: findings, rules, status. It is the project's memory.

## Rules

- Never commit game data (ISO, `default.xbe`, disc files) or generated C.
  Every path is relative to the repo root: the disc in `game/`, everything
  generated in `work/` (both gitignored; tools/env.sh). No default paths
  outside the repo.
- Toolkit changes go in `xboxrecomp/` as their own commits, subject prefixed
  `xboxrecomp:`. Prefer new files over edits to files the Switch layer or
  upstream also change (ADR 0002). Note upstreamable fixes.
- nfsu2-sw's `src/` is unlicensed: read it for ideas, don't copy it.
- Upstream merges are deliberate (`git merge -X subtree=xboxrecomp
  xboxrecomp-upstream/main`), never routine.
- After toolkit header changes (`templates/runtime/recomp_types.h`), regen:
  it rewrites `gen/recomp_types.h`. Then rebuild all generated code.
- Every seed added to `config/seed_functions.json` gets a note, and is
  reviewed: a seed inside a function splits it.
- Record decisions in `docs/decisions/` (short ADRs). Open work, bugs and
  decisions still to evaluate live in `TODO.md` (gitignored, personal);
  this file keeps facts and rules.

## Workflow

Run `make` in the repo root for every target. Logs land in `work/logs/`.
Technical docs (stages, what sits where, VM, the loop): CONTRIBUTING.md, the source of truth.

| Change | Then |
|---|---|
| `src/*.c` (not overrides) | `make build run` |
| `src/recomp_manual.c` overrides, translator | `make relift build run` |
| `config/seed_functions.json`, disasm/func_id | `make regen build run` |
| runtime (`xboxrecomp/src`) | `make build run` |
| ready for console | `make switch send`, Eden, then hardware |

Runtime switches: any `RECOMP_*` / `MC3_*` variable; on the Switch in
`sdmc:/switch/mcdub3/mcdub3_env.txt`. The full `RECOMP_*` list with meanings is
in nfsu2-sw's README (Configuration section); the toolkit is the same.

## Lessons inherited from nfsu2-sw (verify for MC3)

- **Guest memory on Horizon:** code memory (`svcCreateCodeMemory`), aliased
  views via `svcMapProcessMemory`; shared memory may kill the process on
  hardware.
- **Unaligned atomics** fault on AArch64 (exception 0x103), not on x86 and not
  in Eden. The runtime falls back to a spinlock for misaligned addresses.
- **Guest threads race** (streaming systems handing half-filled blocks to
  workers): the guest lock (GIL, `RECOMP_GIL`) runs one guest thread at a time.
  MC3 streams a whole city: expect this class of bug.
- **AArch64 Linux address space** (found 2026-10-14, MC3): mmap is top-down,
  so the guest base landed 3.8 GB under the 48-bit limit and every mapping at
  base + guest VA above that (contiguous window, NV2A/MCPX/flash apertures)
  failed with error 8. Fixed in xbox_memory_layout.c by probing for a free
  4 GB hole first. Watch for `failed (error 8)` lines at boot.
- **Threads:** keep busy threads off core 3 (no time-slicing at priority 59).
- **Leaked directory handles** crash Atmosphère's ams.mitm.
- **SD logging** was the boot bottleneck (33 s); a RAM log + writer thread fixed it.
- **Movies:** replacing the lifted decoder with a native one (FFmpeg) was a big
  win. Find MC3's movie format early.
- **Performance levers:** register/x87 locals in the translator, spin-loop
  hints, native replacements for hot functions, shader/pipeline cache, LTO,
  Vulkan (NVK).

## MC3 facts

(Fill in from tools/first_lift.sh and bring-up.)

- Disc: NTSC-U retail, `default.xbe` "Midnight Club 3: DUB edition" (TT-121),
  4.9 MB XBE, 3.4 GB extracted. Data: `assets_x.dat` (+ `_1`..`_5`),
  `banks.dat`, `audio/` (XACT `.xwb`/`.xsb`), `flash/`, `resources/`.
  Other XBEs on the disc (`dashupdate.xbe`, `downloader.xbe`, `update.xbe`)
  are Xbox system/online tools: not lifted.
- XBE: entry 0x000129DF, title ID 0x54540079 (config/xbe_facts.h), retail,
  built 2005-03-25 (`C:\soft\mc3\src\mcproduct\xbox_final\mc.exe`), XDK
  5849 (same as NFSU2's era), image 5.23 MB, base 0x10000, 154 kernel imports.
- Code: `.text` 3.35 MB (game). Statically linked XDK/middleware, each its own
  section: D3D, XGRPH, D3DX (graphics); DSOUND, XACTENG (XACT audio), WMADEC
  (WMA decoder), DOLBY (APU DSP code); XPP (pads); BINK + 14 BINK* blitter
  sections + BINKDATA (Bink video player); XNET, XONLINE, XON_RD (Xbox Live:
  stub, never needed offline).
- MMIO sections (`MC3_MMIO_SECTIONS`, default `DSOUND,XPP`): the libraries
  that drive hardware registers directly, as in NFSU2.
- Kernel imports worth watching: `XeLoadSection`/`XeUnloadSection` (sections
  loaded on demand), `Phy*` + XNET (network: stub), `Io*` device creation
  (XAPI internals), `Xc*` crypto (saves/signatures), `MmClaimGpuInstanceMemory`.
- Movies: **Bink** (`video/*.bik`, 64 files), played by the statically linked
  Bink library (BINK* sections). FFmpeg decodes Bink, so the nfsu2-sw route
  (native decoder in place of the lifted one) applies.
- Threads: TBD

### First lift (2026-10-14)

21,585 functions, 0 failed, 2.98 M lines of C in 87 chunks (`--split 250`).
49 poll loops hinted, 733 MMIO functions (DSOUND, XPP), 328 unresolved call
targets stubbed (`recomp_stubs_unresolved.c`: addresses called but not found
as functions; fix with seeds when they are hit). All 89 files pass a clang C11
syntax check. For scale: Burnout 3 lifts to 22,097 functions.

353 unimplemented instructions (40 mnemonics): no-ops that print `[UNIMPL]`
when reached (`RECOMP_UNIMPL_TRAP=1` stops at the first). Mostly data decoded
as code (D3DX, WMADEC, XON_RD tables); the few likely-real ones are tracked in
TODO.md.

## Status

- 2026-10-11: skeleton; toolkit vendored and verified; lift pipeline
  smoke-tested on the toolkit's test XBE.
- 2026-10-14: disc in `game/`; XBE parsed and fully lifted in the VM (above).
- 2026-10-14: first Linux build OK on Ubuntu 22.04 ARM64 / GCC 11: all 87
  lifted chunks + runtime link into `work/build-linux/mc3_recomp` (39 MB
  aarch64 ELF). Only warnings, all in the toolkit (printf `%lu` vs `DWORD`,
  `-Wnonnull` on `XBOX_TO_NATIVE`, comments, unused code). Next: first run.
