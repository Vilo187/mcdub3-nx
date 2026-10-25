# Midnight Club 3: DUB Edition (Xbox) static recompilation: engineering notes

Xbox *Midnight Club 3: DUB Edition*, lifted to C with xboxrecomp and built for
Linux (debug host) and Nintendo Switch (libnx NRO). Modelled on nfsu2-sw.
Keep this file current: findings, rules, status. It is the project's memory.

## Rules

- Never commit game data (ISO, `default.xbe`, disc files) or generated C.
  Every path is relative to the repo root: the disc in `game/`, everything
  generated in `work/` (both gitignored; tools/env.sh). No default paths
  outside the repo.
- Never commit, upload or store on GitHub the console keys: `keys/prod.keys`
  (gitignored; the NSP forwarder build reads them from there). Not as a
  secret, not as an artifact, never on GitHub's runners. The one exception
  (decided 2026-10-21, ADR 0005): the release's NSP job on the owner's own
  self-hosted runner reads them from that machine's disk (`MC3_KEYS`); only
  the finished NSPs leave it.
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
  4.9 MB XBE, 3.4 GB extracted. The European (PAL) disc (Redump "Europe
  (En,Fr,De,Es,It)") runs on the same lift (2026-10-24): its XBE differs in
  531 header bytes only (signature, certificate region 0x4 and version 2,
  section digests; code identical), its data in three files
  (flash/pause_x.xbck, video/Story/Suschamp.bik, video/Tutorial/Trlogos.bik).
  config/xbe_editions.h lists every accepted XBE (FNV-1a 64) for the boot
  check, which logs `disc: edition <name>`. Discs side by side: game/ and
  game-<edition>/ (gitignored). Which plays (tools/env.sh, windows_host.c):
  EDITION= first; else the only disc; of several game/, then the lifted
  edition's, then the first by name (noted once). Packages per edition:
  make switch EDITION=eur -> mcdub3-nx-eur.nro (MC3_PACKAGE_EDITION; looks
  in sdmc:/switch/mcdub3-nx/game-<edition>/ first, then game/), NSPs from
  APP_EDITIONS in switch/app.env (eur: its own title ID), CI matrix
  [usa, eur] from the one lifted disc. Languages come
  from XC_LANGUAGE: RECOMP_LANGUAGE=fr|de|es|it (kernel_xbox.c; the title
  screen's "Press Start" stays English). Data: `assets_x.dat` (+ `_1`..`_5`),
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
- Motion blur (fixed 2026-10-18): MC3 reads the depth buffer (0x80A24000)
  as a colour texture (LIN_A8R8G8B8 / LIN_A8B8G8R8) to weight its blur.
  Depth only existed in GL, so the mask was garbage and races smeared at
  speed; nv2a_gl now packs GL depth into those formats
  (RECOMP_GL_DEPTH_TEX=0 to compare). A race frame is ~1800 draws: use
  RECOMP_GL_WATCH_N=4000 for a whole one.
- Picture quality (GL, 2026-10-18; ideas from nfsu2-sw's Vulkan renderer):
  square pixels in 16:9 (RECOMP_GL_SQUARE; 1280x720 at RECOMP_GL_SCALE=1.5,
  the sweet spot for a 720p screen), the title's mip levels + trilinear +
  16x anisotropic + -0.25 LOD bias (RECOMP_GL_MIPS/_ANISO/_LOD_BIAS), FXAA
  and CAS sharpening in the present pass (RECOMP_GL_FXAA, RECOMP_GL_SHARPEN),
  and the 4:3-to-16:9 fill: MC3 lays menus and videos out in the middle 640
  of 720 columns and clears the rest; black side bands -> the middle is
  shown full width (RECOMP_GL_FILL), except the boot movies
  (MC3_FILL_SKIP=rockstar,sdlogo,mc3intro). MC3 has no 720p mode (no
  1280x720 in its code). Movies upscaled 2x on the GPU (MC3_MOVIE_HD,
  RECOMP_GL_MOVIE_SCALE; first test pending). HD video packs made from the
  game's videos can't be shipped.
- NSP forwarder (works since 2026-10-18; one per edition since 2026-10-21,
  each starting mcdub3-nx-<edition>.nro under APP_TITLE_ID_<edition> from
  switch/app.env): `make nsp` follows NTON's recipe
  (its Forwarder-Mod forwarder reading romfs:/nextNroPath, its NACP
  settings, icon re-encoded bare); start-up logo from assets/logo/ or NTON's.
  The VM's Docker has no DNS: `nsp.sh prepare` fetches on the host.
- Input must reach the game as native controls do (the emulated pad), so
  the game decides what can be skipped: the Rockstar and Rockstar San Diego
  logos cannot, mc3intro can (Enter). The keyboard is pad 1 by default
  (src/main.c; RECOMP_KEYBOARD=0 off). `make run WATCH=1`: F12 writes every
  draw of the frame on screen to the log (a diagnostic; nothing visible).
  Keyboard (Linux since 2026-10-16, input/sdl_keyboard.c; relaid
  2026-10-21 so buttons sit where their names say): A/B/X/Y = buttons A/B/
  X/Y, Enter = Start, Backspace = Back, arrows left/right = steer (left
  stick), up = gas (right trigger) + stick up, down = brake (left trigger)
  + stick down (menus navigate with the stick), I/J/K/L = d-pad (songs and
  map in a race), Q/E (or Shift/Ctrl) = White/Black (the Switch's L/R),
  Z/C = stick clicks, T/F/G/H or numpad 8/4/2/6 = right stick, Space = A
  (MC3's handbrake, the Switch's A), Tab = Back too. Unattended runs only:
  `MC3_SKIP_MOVIES=1` pulses A+Start on the pad while intro movies play.
- Debug switches (toolkit): `RECOMP_CLOCK_RATE=<f>` (+ `RECOMP_CLOCK_ONLY=
  tsc|qpc|tick|sys`) runs the guest clocks at f x real time -- tells clock-
  timed from frame-paced; `RECOMP_IO_MBPS=<n>` (+ `RECOMP_IO_SEEK_MS=<m>`)
  slows reads to a DVD's pace -- for streaming bugs hidden by fast storage.
- Measuring speed in the VM: headless runs (`make record`, `make profile`)
  render with llvmpipe (software) and run slower; windowed runs use virgl
  (the Mac's GPU). Judge frame rate windowed; use headless for relative
  profiles only.
- Threads: the guest lock serialises guest code. On the Switch each
  hand-over is a scheduler round trip: behind the legal screen (a loader
  thread at work) the main thread waited 66-75% of the time in short waits,
  4 fps. Uncontended lock calls (mutant/critical section) now keep the lock;
  console measurement pending. `MC3_PERF=<s>` reports lock waits, CPU per
  thread (Switch), APU pacing; `pkill -USR1 mcdub3` snapshots the main
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

- Career loading-screen hang (found 2026-10-21, fix under test): the main
  thread spun in D3D's fence wait (sub_003DF4D0, loop at 0x003DF607) while
  the loading-screen thread kept drawing. The loop keeps the time handed out
  (+0x2C of the push-buffer state 0x003ECAF0) from before its poll yield;
  the GPU ran past it and `handed - gpu` wrapped. src/recomp_manual.c ends
  the loop through a spin hook (recomp_set_spin_hook) once the GPU has
  passed the fence (MC3_FENCE_FIX=0 off). `pkill -USR1 mcdub3` now also
  prints the main thread's kernel call and that fence state.
- macOS (2026-10-21): native Apple Silicon build into work/build-mac
  (`make build run`; Homebrew in /opt/homebrew). Boots and plays the
  movies on GL 4.1 (Metal). The game runs off the main thread there; the
  main thread serves SDL (xboxrecomp/src/platform/host_main.c). Linux SDL
  pads are opened at boot and hot-plugged (they were never opened before).

- Releases (2026-10-21, ADR 0005): version in switch/app.env, notes in
  RELEASE_NOTES.md; ci.yml (tests) -> switch-build.yml (self-hosted runner
  with the disc, label mcdub3-disc) -> switch-release.yml (environment
  `release`, approval). NROs are mcdub3-nx-<edition>.nro; the boot check
  (MC3_XBE_SIZE / MC3_XBE_FNV64 in config/xbe_facts.h) refuses another XBE.

- Career race crash (found 2026-10-21, fixed 2026-10-22; every career race,
  every host): the race-event listener sub_0023D630 leaves through `jne`
  onto `pop edi; pop esi; ret 4` at 0x23D72D, bytes the detector left in a
  falsely split neighbour (gap_prologue at 0x23D71F), so the target was a
  ret-only stub that skipped both pops. The broadcast loop (sub_00251CD0,
  called by mcPgCareer::ReportRaceResult sub_0024F8C0) then walked on with
  a garbage esi into a null call. The translator now lifts such epilogues
  (21 in MC3; test_stub_epilogue.py). Found with src/recomp_manual.c's
  broadcast replacement, which checks every listener call for a broken
  calling convention (`[LISTEN]`, MC3_LISTEN_TRACE=1; MC3_LISTENER_CHECK=0
  runs the game's own loop).

- Saves (2026-10-22): a profile is UDATA/54540079/<12 hex>/<name>.mc3
  (99,162 bytes) + SaveMeta.xbx (UTF-16 "Name=<name>"). The .mc3 starts
  with a format stamp (Xbox 0x02F59447) and ends with HMAC-SHA1 of the
  rest under the kernel's XboxSignatureKey-derived key; the port's keys are
  zero, so its key is all zeros and a real Xbox's profile reads "damaged".
  `make import-save FILE=` (tools/save_import.py: .gsx = gzip + 128-byte
  GSXM header + FATX memory unit with 16 KB clusters; folders; .mc3; .max)
  re-signs and installs, backing up first; verified in game. PS2 saves
  (.max = LZARI, .sps/.xps = SharkPort, plain; profile BxSLUS-nnnnn<name>,
  no signature) can't be used: the same game's PS2 release (SLUS-21029)
  writes stamp 0x02F6DC47, ~61 KB of data (+ 0xCD fill to 67,836), about a
  third of its words in line with the Xbox's; the Remix (SLUS-21355)
  0x00512C4D, about half. The tool names them and refuses. CodeBreaker
  .cbs (RC4 + zlib) is not read.

- Windows (2026-10-23, in testing): MinGW/clang builds, x86-64 and ARM64
  (`make windows` cross from the Mac with llvm-mingw + MSYS2 libraries,
  tools/win_deps.py; natively in MSYS2 `make setup build`;
  install_windows.bat does all of it). The toolkit's desktop path on Win32
  (XBOXRECOMP_WIN_SDL_GL; CONTRIBUTING.md, Windows build). Found
  on the way: the title has to run off the main thread (window messages);
  host paths are UTF-16 on Windows (bink_native.c converts for FFmpeg);
  Parallels' GL refuses uniform-array uploads larger than what the
  compiler kept (movies black). Runs in Parallels (Windows 11 ARM):
  movies, sound, pads; slow there. Open: speed and picture quality in the
  VM, an occasional hang opening the first movie, a test on a real PC.
  Diagnose with `mcdub3_env.txt` beside the exe (read by run_mcdub3.bat):
  RECOMP_GL_ERRORS=2, RECOMP_GL_PROBE=1, RECOMP_WATCHDOG_SECS (+ every
  thread's native stack; tools/windows/symbolize.sh resolves it).

- Extras menu (2026-10-24, src/mc3_extras.c): drawn by the toolkit's host
  overlay (nv2a_gl/gl_overlay.c: rects, slanted bars, stb_truetype text in
  a 1280x720 canvas, one draw call, after the title's picture), driven by
  the input filter hook (xinput_device.c xbox_input_filter; usb_gamepad.c
  now merges pad scripts *before* it, so RECOMP_PAD_SCRIPT drives the menu
  too). Game facts it uses, all found by disassembly:
  mcMenuPause vtable 0x00427B68 (object found by scanning RAM for it; +0x44
  bit 0 = menu active, tested first by its input/update methods
  sub_001524E0/sub_001526E0); game mode 0x00507B14 (2 = career, >100
  compares); money = [[0x0051893C]+0x0C]+0x838 (save field "Money",
  sub_002598B0); unlock flags 0x00507B2C..32 (races, cities, vehicles,
  customisation, abilities, difficulty, perf parts), set by the developer
  parameter -unlockall (param objects + list head 0x004FCF0C, sub_00241670)
  and by the game's own cheat screen (sub_001794F0), read live by the
  unlocking rules (0x2607xx). Camera: camViewCS (vtable 0x00421200) +0x2C
  active camera, camera +0x0C Matrix34 (rows right, up, back, position;
  looks along -c), +0x84 FOV, +0x88/+0x8C clip; camViewCS::apply
  sub_00120F90 -> sub_002A4560(Matrix34*) sets camera 0x0051A3C0, view
  0x0051A2B8, frustum, D3D view. The player's camera set holds POV +0x28,
  chase1-4 +0x2C..+0x38, reverse +0x3C, wipeout +0x44, cinematic +0x50
  (sub_00297000). The free camera (src/mc3_freecam.c): camViewCS::apply
  (wrapped) records the game camera each frame; the view is set from that
  camera more than once a frame (viewports keep copies at +0x84 and reload
  them, sub_002A57D0 SetCurrentViewport), so both setters are wrapped --
  sub_002A4560 (Matrix34*) and sub_002A4530 (4x4*) -- and swap in ours
  whenever their matrix is at the game camera's position. While paused the
  world is not updated, so flying from the pause menu needs no freeze; for
  flying while driving the frame time clamps 0x0047C3F0/F4 (min/max of
  sub_001EDBE0's dt) are set to 1e-5 s. Menus and HUD are hidden in the
  executor (nv2a_pb_exec.c raw_batch_is_2d: pre-transformed, orthographic
  fixed function, or a vertex program whose first vertex has w=1, z=0 --
  MC3's fonts). MC3_FREECAM_TRACE=1 logs game/ours/renderer positions.
  The camera object keeps our matrix for the whole frame while flying
  (MC3_FREECAM_KEEP): only then does the city's visibility follow the free
  camera (with the matrix restored after the view call the city vanished
  ~150 m from the car).
- CPU (2026-10-24, Linux, same race scene at 30 fps: 160% -> 94% of a
  core): MC3's timer thread sub_00196230 calls sub_00044B90 once a second
  and waits for it with Sleep(0) (sub_0001739C) in a loop -- 41% of all CPU,
  every turn a guest-lock hand-over against the main thread; on that thread
  Sleep(0) now sleeps MC3_TIMER_SLEEP_MS (20). D3D's vblank handler
  sub_003E4FD0 spun at 0x003E5080 for PMC_INTR bit 24 (runtime's 300 us
  grace, under the guest lock): the wrapper calls xbox_Nv2aVblankTaken
  first (MC3_VBLANK_ACK=0 off) -- the runtime had it, nothing called it.
- 60 fps (2026-10-24): the menus ask D3D for presentation interval 2;
  the pushbuffer callback wrapper (sub_003E52D0) turns a flip's interval 2
  into 1 while g_mc3_fps60. The world's step: sub_001EDBE0 measures the real
  frame time (0x0047C408), snaps it up to 1/60..1/15 while 0x004FCF42 (raw
  -> 0x0047C404), and in races and most states runs in fixed-step mode
  ([0x004FCF3C] = 1 via sub_00020490 -> sub_001EE0B0): step 0x0047C3DC =
  fixed step [0x0047C3EC] (1/30) x time factor [0x0047C400]. At 57 fps a
  race ran 1.9x fast; the wrapper now sets the fixed step to the host-
  measured frame time (1/60..1/20) before each frame -- race timer within 1%
  of the wall clock at ~54 fps. Slow motion and the free camera's freeze
  scale [0x0047C400] (the dt clamps 0x0047C3F0 max / 0x0047C3F4 min do
  nothing in fixed-step mode). Restarting a race reuses the player vehicle
  (no set-up call): the vehicle switcher applies at the next race load.
- 60 fps profile (Linux, 2026-10-24, ~58 fps): the main thread waits 36%
  of the time in D3D's GPU wait (KeWait.. from 0x003DF62F): the executor
  thread (pushbuffer walk + methods + draw assembly) is the limit, then
  Mesa's driver thread. Switch hardware log (OC, 60 fps mode): 45-53 fps.
  Done: tex_get no longer hashes unchanged textures every frame (static ones
  every 4th frame after 120 frames unchanged, RECOMP_GL_TEX_CHECK_EVERY;
  rewritten ones compared with memcmp against a copy), the pushbuffer
  method survey note() only with RECOMP_PB_SURVEY=1, vertex formats via
  glVertexAttribFormat once + glBindVertexBuffer per draw (RECOMP_GL_VAB).
  Next candidate: a side-effect table in nv2a_pb_exec_method (most method
  words only update the register shadow).
- Developer parameters: 172 of them (list work/re/params.txt; objects
  {name, value, default, next}, head 0x004FCF0C, registered by static
  initialisers calling sub_001ED930), parsed from main's argv by
  sub_001ED9D0. MC3_ARGS adds arguments (wrapper builds a new argv in
  xbox_ContiguousAlloc memory). Boot-time effects: sub_00241670 (unlock
  flags, world switches 0x0047CD40 props / 41 traffic / 42 pedestrians /
  43 AI / 44 "cpvs"), sub_002408E0 (game state [0x00507B24]: +0x10C
  devmenus, weather by name {clear, cloudy, rainy} at 0x0047CDB0 -> +8).
  Sessions copy the world switches into the state (sub_00241120: +0x106..
  +0x10A).
- Vehicles: 69 player vehicles (the "_base" models in assets_x.dat), table
  [0x0051885C] (count word [0x00518860], 0x1C each, name first), lookup by
  name sub_002426E0. The local player's car is built by sub_0029D090 then
  the virtual sub_0029D2E0/sub_0029D3B0 on one set-up struct (name at
  this+4; sub_0029CF30 is the variant that sets +0xE0 = 1): the switcher
  (MC3_VEHICLE) rewrites that name. Not career-only.
- Guest addresses: the game's heap is in the contiguous window
  0x80000000-0x84000000 (kernel.h XBOX_CONTIG_BASE, its own storage, not an
  alias of low RAM); read both through g_xbox_mem_offset + VA.
- Mods (2026-10-24, src/mc3_mods.c): mods/ beside game/ -- files/ and
  packs/<name>/files/ replace disc files (kernel_path.c xbox_path_override,
  index built once, case-insensitive), textures/ is the HD-pack folder,
  ui/font.ttf the menu font, settings.txt extra settings.
- Hot spots found with perf on Linux (2026-10-24, career race): getenv
  5.7% (recomp_unimpl read RECOMP_UNIMPL_TRAP on every executed untranslated
  instruction -- D3D's `in al, dx` at 0x003E504D and cli/sti run constantly;
  now cached) and swizzle_index 5.3% (per-texel bit loop for textures the
  game rewrites each frame; now two lookup tables, nv2a_gl.c unswizzle32).
  Lifted-code loops: tools/recomp/loop_barrier.py puts RECOMP_LOOP_BARRIER()
  on every backward goto (15k), a compiler barrier only in MC3_MEM_PLAIN
  builds -- D3D's vblank wait at 0x003E5080 stores and loads in one loop with
  no call and no spin hint, and would hang with plain memory otherwise.
- Performance work 2026-10-24: persistent coherent vertex/index rings with
  per-quarter fences (nv2a_gl.c ring_alloc_persist, RECOMP_GL_PERSIST=0
  off) instead of a map/unmap pair per draw; Switch lifted code built with
  hidden visibility (src/nx_hidden.h: globals PC-relative, not via the
  GOT), -fomit-frame-pointer, -fno-plt; MC3_MEM_PLAIN=ON builds plain
  (non-volatile) guest memory with the spin hint as a compiler barrier.
- Building without root or Docker: `make switch BWRAP=1`
  (switch/bwrap_build.sh unpacks the pinned image into work/toolchain).

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
  lifted chunks + runtime link into `work/build-linux/mcdub3` (39 MB
  aarch64 ELF). Only warnings, all in the toolkit (printf `%lu` vs `DWORD`,
  `-Wnonnull` on `XBOX_TO_NATIVE`, comments, unused code). Next: first run.
- 2026-10-17: music and sound in menus, garage and races; cars drawn; race view
  and HUD drawn (racing itself not yet: open); garage reachable; movies native on the console; boot
  black screen gone (RECOMP_NET_LINK=0).
- 2026-10-18: frame pacing fixed (flythrough, menus, garage at hardware
  speed on Linux; boot black screen short again). The Switch "1.5x scale
  glitches" (menu, RECOMP_GL_SCALE=1.5) were the slow menu navigation fixed
  on 2026-10-17 (APU EP alive, In-game audio fix 3): closed.
- 2026-10-18 (Switch): legal-screen stall fixed -- MC3's heartbeat thread
  (sub_00196230) polls with Sleep(0) and took the guest lock back at every
  main-thread kernel call; a guest yield now gives way for a quantum
  (RECOMP_YIELD_QUANTUM_MS). Boot black screen fixed -- Mesa 20.1 on the
  Switch has no program binaries (Linux does: progcache-gpu.bin), so the
  shader list (4.8 s) compiles a program a frame behind the legal screen
  (sub_0001B740 -> nv2a_gl_prewarm_start; RECOMP_PROG_PREWARM=boot for the
  old way).
