# Midnight Club 3: DUB Edition (Xbox) static recompilation: engineering notes

Xbox *Midnight Club 3: DUB Edition*, lifted to C with xboxrecomp and built for
Linux (debug host) and Nintendo Switch (libnx NRO). Modelled on nfsu2-sw.
Keep this file current: findings, rules, status. It is the project's memory.

## Rules

- Never commit game data (ISO, `default.xbe`, disc files) or generated C.
  Everything heavy lives in `$MC3_WORK` (tools/env.sh), outside the repo.
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
- Record decisions in `docs/decisions/` (short ADRs).

## Workflow

| Change | Then |
|---|---|
| `src/*.c` (not overrides) | `tools/build_linux.sh` |
| `src/recomp_manual.c` overrides, translator | `LIFT_ONLY=1 tools/regen.sh`, build |
| `config/seed_functions.json`, disasm/func_id | `tools/regen.sh`, build |
| runtime (`xboxrecomp/src`) | build |
| ready for console | `switch/build.sh`, Eden, then hardware |

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
- Entry point / title ID: config/xbe_facts.h
- Sections / XDK libraries / MMIO sections: TBD (tools/first_lift.sh)
- Movies: **Bink** (`video/*.bik`, 64 files). FFmpeg decodes Bink, so the
  nfsu2-sw route (native decoder in place of the lifted one) applies.
- Threads: TBD

## Status

- 2026-10-11: skeleton; toolkit vendored and verified; lift pipeline
  smoke-tested on the toolkit's test XBE. Waiting for the disc.
