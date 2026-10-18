# Midnight Club 3: DUB Edition (Xbox) static recompilation: engineering notes

Xbox *Midnight Club 3: DUB Edition*, lifted to C with xboxrecomp and built for
Linux (debug host) and Nintendo Switch (libnx NRO). Modelled on nfsu2-sw.
Keep this file current: findings, rules, status. It is the project's memory.

## Rules

- Never commit game data (ISO, `default.xbe`, disc files) or generated C.
  Every path is relative to the repo root: the disc in `game/`, everything
  generated in `work/` (both gitignored; tools/env.sh). No default paths
  outside the repo.
- Never commit, upload or pass to CI the console keys: `keys/prod.keys`
  (gitignored; the NSP forwarder build reads them from there, locally only).
- Toolkit changes go in `xboxrecomp/` as their own commits, subject prefixed
  `xboxrecomp:`. Prefer new files over edits to files the Switch layer or
  upstream also change (ADR 0002). Note upstreamable fixes in
  docs/upstream.md (patch or issue, and which upstream: xboxrecomp or nfsu2-sw).
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
| body of an existing override in `src/recomp_manual.c` | `make build run` |
| added/removed an override, translator | `make relift build run` |
| `config/seed_functions.json`, disasm/func_id | `make regen build run` |
| runtime (`xboxrecomp/src`) | `make build run` |
| ready for console | `make switch send`, Eden, then hardware |

Runtime switches: any `RECOMP_*` / `MC3_*` variable; on the Switch in
`sdmc:/switch/mcdub3-nx/mcdub3_env.txt`. The full `RECOMP_*` list with meanings is
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
- **AC'97 reset in "plain" mode** (found 2026-10-14, MC3): DSOUND writes the
  channel reset bit (NABM +0x0B, RR) and reads it back once (MSVC hoisted the
  load), so the toolkit's after-the-fact clearing thread loses and DirectSound
  spins forever. src/ac97_nabm.c models those registers through the MMIO hooks
  (DSOUND is lifted with them): RR never lands. Needs DSOUND in
  MC3_MMIO_SECTIONS.
- **Lifter: movsx from bp/sp** (found 2026-10-15, MC3, also upstream): was
  lifted as a zero extension. Bink IDCT corruption + 5 game-code sites.
  Fixed in tools/recomp/lifter.py with a regression test. Found by
  `make difftest` (src/difftest.c + tools/difftest.py: replay one captured
  call on real x86 in Unicorn and name the first wrongly written bytes):
  the tool to reach for whenever output is subtly wrong.
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
  src/bink_native.c replaces only the video decode (0x0037CCD0); Bink keeps
  I/O, timing, audio and the YUV→RGB copy. Bink's frame buffer is Y, **V**, U,
  packed (pitch = width); FFmpeg's binkvideo is bit-exact with it.
  `MC3_NATIVE_BINK=0` brings back the lifted decoder. On the Switch too
  since 2026-10-17 (FFmpeg from the portlibs; paths opened as `file:sdmc:/…`,
  since FFmpeg reads `sdmc:` as a protocol).
  Result (2026-10-15, windowed, virgl): movies at a steady 30 fps; per frame
  decode <1.5 ms, Bink's lifted YUV→RGB copy 4–7 ms, draw+Present ~11 ms,
  ~15 ms spare. `MC3_BINK_TRACE=1` prints this split every second.
- Movie sound: Bink decodes it correctly (matches FFmpeg, r = 0.99999), but
  the way out (Bink's DirectSound ring → emulated APU voice → output)
  repeats 256–768-sample pieces, skips, and runs at ~84% speed. So FFmpeg
  plays the soundtrack on a runtime software-mixer voice (`apu_mixer_*`),
  synced to Bink's frame number, and Bink's decoded PCM is zeroed
  (`MC3_NATIVE_BINK_AUDIO=0` to compare). `tools/audio_diff.py capture.wav
  movie.bik` measures holes, skips, repeats and drift against the original
  track. The renderer is set up at boot (Nv2aBackend.prepare): set up at the
  first flip, the Rockstar logo stayed black ~1 s while its soundtrack (tyre
  screeches) already played.
- In-game audio (XACT, music WMA from `audio/XStreams/mx.xwb`) works since
  2026-10-17, after four fixes, each found by measuring:
  1. lifter: a jcc after a join of different-width cmps lifted as
     `if (_flags)` (never taken) -- XACT refused every sound start;
  2. bus addresses: heap PCM (0x0120FF80) was read from the contiguous window
     at the same offset (noise); MmGetPhysicalAddress now tags RAM (bit 26);
  3. APU GP/EP memory read as 0, so DirectSound found the EP "dead" (P-memory
     word 6 != 0xCCCCCC) and reset both DSPs with a 10 ms stall from every
     XACT call: half-speed APU, menu lag, garage hang. Now storage, and the
     stub EP reads alive;
  4. (movies) see above. Tools: `MC3_XACT_TRACE`, `MC3_WMA_TRACE`,
     `RECOMP_APU_TRACE` (adds `[APU-ST]`: samples/s, stream consumption,
     pacing), `RECOMP_AUDIO_DUMP=path,start,secs` (written at exit/Ctrl+C).
- Rendering found and fixed 2026-10-17: vertex programs read the viewport
  scale/offset as constants c[-38]/c[-37] (NV2A mirrors the registers there:
  cars were invisible); a narrower clip into the back buffer (race minimap,
  175x451) rebuilt the surface (black race screen). Find such bugs with
  `make run WATCH=1 RECOMP_GL_WATCH_N=1500 RECOMP_GL_WATCH_SHOTS=<prefix>`:
  F12 dumps every draw and clear of the frame, images every 50 draws.
- Input must reach the game as native controls do (the emulated pad), so
  the game decides what can be skipped: the Rockstar and Rockstar San Diego
  logos cannot, mc3intro can (Enter). The keyboard is pad 1 by default
  (src/main.c; RECOMP_KEYBOARD=0 off). `make run WATCH=1`: F12 writes every
  draw of the frame on screen to the log (a diagnostic; nothing visible).
  Keyboard (Linux since 2026-10-16, input/sdl_keyboard.c; laid out for
  racing 2026-10-17): W/S = gas/brake (right/left trigger), A/D = steer
  (left stick), J/K/U/I = buttons A/B/X/Y (A = OK, B = back), Space = X,
  arrows = d-pad (menus; in a race left/right change the song), Enter =
  Start, Backspace = Back, Shift/Ctrl = White/Black, Q/E = stick clicks,
  numpad 4/6/8/2 or F/H/T/G = right stick. Unattended runs only:
  `MC3_SKIP_MOVIES=1` pulses A+Start on the pad while intro movies play.
- Measuring speed in the VM: headless runs (`make record`, `make profile`)
  render with llvmpipe (software) and run slower; windowed runs use virgl
  (the Mac's GPU). Judge frame rate windowed; use headless for relative
  profiles only.
- Threads: the guest lock serialises guest code. On the Switch each
  hand-over is a scheduler round trip: behind the legal screen (a loader
  thread at work) the main thread waited 66-75% of the time in short waits,
  4 fps. Uncontended lock calls (mutant/critical section) now keep the lock;
  console measurement pending. `MC3_PERF=<s>` reports lock waits, CPU per
  thread (Switch), APU pacing; `pkill -USR1 mc3_recomp` snapshots the main
  thread's stack (`tools/stackwalk.py`).
- Frame pacing (fixed 2026-10-18): the first flythrough (1.55 s vs 5.5 s on
  hardware), menus and garage ran 2-4x fast on Linux, with every clock and
  the frame time right. The game asks D3D for presentation interval 2
  (30 fps) there and advances per frame, so the presenter must honour D3D's
  flip targets: sub_003E4EF0 (retirement) reports to nv2a_pb_flip_retired.
  That only works once D3D's PGRAPH handler (sub_003E5540) runs under the
  executor's trap lock and reports traps taken: its write-one-to-clear ack
  is a no-op on plain memory, and the trap storm (412k flips, targets
  millions of vblanks ahead) wrecked the flip queue. D3D device 0x003EE718:
  vblank count +0x1C0, last target +0x1C8. `MC3_FLIP_TRACE=1` logs the
  pushbuffer callback (sub_003E52D0; param & 0x1F = command, 1 = flip,
  (param >> 5) & 7 = interval); `MC3_FLIP_PACE=0` / `MC3_TRAP_HOOK=0` undo.

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
- 2026-10-17: music and sound in menus, garage and races; cars drawn; race view
  and HUD drawn (racing itself not yet: open); garage reachable; movies native on the console; boot
  black screen gone (RECOMP_NET_LINK=0). Open: legal-screen stall (console), 1.5x scale glitches, NSP forwarder.
- 2026-10-18: frame pacing fixed (flythrough, menus, garage at hardware
  speed on Linux; boot black screen short again).
