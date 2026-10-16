# What to send upstream

Fixes and findings from this port that other projects should hear about.
Our toolkit has two origins (ADR 0002), so each item names where it belongs:

- **xboxrecomp** ([sp00nznet/xboxrecomp](https://github.com/sp00nznet/xboxrecomp)):
  the lifter (`tools/`), and the parts of the runtime it already had.
- **nfsu2-sw** ([antoxa2584x/nfsu2-sw](https://github.com/antoxa2584x/nfsu2-sw)):
  the Switch layer we imported: the OpenGL/Vulkan pushbuffer renderers, POSIX
  and Switch audio, the Horizon memory layout and the translator passes.

"Still present" was checked against xboxrecomp v0.13.1 (`193e299`,
2026-10-07) and nfsu2-sw `253bde2` (2026-10-15). Re-check before sending.
Nothing here has been sent yet.

## Patches ready (our commits)

| # | Fix | Our commit | Goes to | Still present upstream |
|---|-----|-----------|---------|------------------------|
| 1 | Lifter: `movsx r32, bp/sp` lifted as a zero extension | `e720852` | xboxrecomp (and nfsu2-sw, which carries the same lifter) | yes, both |
| 2 | Renderer: texture change check samples 256 words, misses small changes | `1290b6b` | nfsu2-sw (nv2a_gl, nv2a_vk) | yes |
| 3 | Memory: AArch64 Linux puts the guest base too high for the 4 GB window | `794632b` | nfsu2-sw (POSIX memory layout) | yes |
| 4 | Build: Switch data folder hard-coded to `nfsu2x` | `e7dca06` | nfsu2-sw | yes |
| 5 | Repo: `tools/*/output.py` missing, so the lift pipeline can't run | `af03785` | nfsu2-sw | yes |
| 5b | Renderer: fixed-function fog factor hard-coded to 0, so fogged draws become the fog colour | `dab6e43` | nfsu2-sw (gl_vsh.c, nv2a_gl, nv2a_vk) | yes |
| 5c | Executor: every inline draw taken as pre-transformed (only XYZRHW is) | `b889914` | nfsu2-sw (nv2a_pb_exec.c) | yes |
| 5d | Input: keyboard does nothing on Linux (events dropped, key state stubbed) | `10f8602` | nfsu2-sw (input/, renderers) | yes |

1. **`movsx` from `bp`/`sp`** (`tools/recomp/lifter.py`, `_lift_movsx`). The
   16-bit source list has ax, bx, cx, dx, si, di but not bp, sp, so
   `movsx ebp, bp` becomes `ebp = LO16(ebp)`, a zero extension: −3 comes
   out as 65533. `_lift_movzx` has all eight. In MC3 this broke Bink's IDCT
   (wrong-colour 8×8 blocks in every movie) plus five sites in game code.
   Two-line fix plus a regression test (`test_lifter_movsx_bp.py`). Found by
   replaying the call on real x86 (see "Tools" below).

2. **Texture change check** (`bytes_hash` in `nv2a_gl.c` and `nv2a_vk.c`).
   It hashes 256 words spread over the texture. For a 640×480 A8R8G8B8 movie
   frame that's one pixel every 4,800 bytes, so a change between samples
   doesn't trigger a re-upload. In MC3's intro, title letters typed out on a
   dark frame stayed stuck in an early animation state for many frames. Fix:
   hash linear (non-swizzled) textures in full, about 0.2 ms per 1.2 MB, and
   keep the sampled hash for swizzled ones. Other titles with CPU-written
   textures (movies, UI) are affected the same way.

3. **Guest memory window on AArch64 Linux** (`xbox_memory_layout.c`). mmap is
   top-down there, so the guest base landed 3.8 GB below the top of the
   48-bit space. Every view at base + guest VA above that (contiguous window,
   NV2A/MCPX/flash apertures) then failed with error 8, and the first NV2A
   register access faulted. x86-64 places mappings lower, which is why
   nfsu2-sw never hit it. Fix: probe for a free 4 GB hole and reserve there.
   POSIX only.

4. **`XBOXRECOMP_APP_NAME`**: a CMake cache variable for the
   `sdmc:/switch/<app>/` folder (pipeline cache, logs), so a second port
   doesn't share nfsu2's files.

5. **Missing `tools/disasm|func_id|recomp/output.py`**. They exist in
   xboxrecomp 49ee746 but not in nfsu2-sw, so the stage-1 tools fail to
   import. Restored from upstream; nothing to change, just add them back.
   Also covered by the draft issue in `docs/drafts/`.

5b. **Fixed-function fog** (`nv2a_gl_vsh_fixed` in `gl_vsh.c`). The shader
   ends with `vFog = 0.0`. D3D's fog blend in the final combiner is
   fog.a × colour + (1 − fog.a) × fog colour, so every fixed-function draw
   with fog on comes out as the plain fog colour. MC3's menu city was flat
   dark grey. Fix: the NV2A fog unit (gen mode, linear/exp/exp2 with
   FOG_PARAMS, factor 1 when fog is off) in GL and Vulkan. Bias constants
   follow xemu; MC3's parameters give a sensible 1 at the camera.

## Issues to report (no patch from us yet)

| # | Problem | Goes to | Evidence / our workaround |
|---|---------|---------|---------------------------|
| 6 | `pushfd`, `popfd` and `cpuid` are silent no-ops | xboxrecomp + nfsu2-sw | CPU feature checks read garbage. MC3's Bink saw "no MMX", left its pixel routines null and looped forever. Workaround: native `sub_002B9C90` |
| 7 | AC'97 "plain" mode: the channel reset bit is cleared too late | nfsu2-sw | DSOUND reads RR back once (hoisted load) and spins forever. Our `src/ac97_nabm.c` models the NABM registers via MMIO hooks; offer it as the fix |
| 8 | APU streaming voices repeat, skip and run slow | nfsu2-sw | Movie sound measured against FFmpeg's decode: 1,567 repeats of 256–768 samples, 304 skips, 1.2 s of holes, ~84% speed over 70 s. Root cause not found; movies now bypass it (`src/bink_native.c`) |
| 9 | 11 toolkit tests fail on stale harnesses | nfsu2-sw | Their C preambles lack `RECOMP_PREEMPT` and the leaf/register-locals helpers that the nfsu2-sw translator always emits. With those defined, 516 of 517 pass |
| 10 | Template `recomp_manual.c` declares `extern uint32_t g_eax` | xboxrecomp + nfsu2-sw | The registers are thread-local (`RECOMP_TLS`): a TLS mismatch that GCC refuses to link. Fix: include `recomp_types.h` |
| 11 | Git conflict markers committed in docs | nfsu2-sw | `>>>>>>> pr-128` in README.md, CONTRIBUTORS.md, docs/pipeline/06-debugging.md, docs/technical/kernel-replacement.md |

For 8, the quickest reproduction is any title with streamed DirectSound sound.
Record with `RECOMP_AUDIO_DUMP` and compare with the source track using
`tools/audio_diff.py`.

## Tools worth offering

- **Differential tester** (`src/difftest.c`, `tools/difftest.py`, `make
  difftest`): captures guest state around one lifted call, replays the
  original x86 in Unicorn from the same state, and names the first wrongly
  written bytes and the instruction that wrote them. It found #1 in a few
  runs. It's generic apart from the wrappers that arm it.
- **`tools/audio_diff.py`**: follows a reference track through a captured
  output and reports holes, skips, repeats and drift. It found #8.

## How to send

One PR or issue per item, smallest first (#1, #5, #4). Each with the
symptom, the cause, the fix and how it was verified. Our commit messages
already read that way. For nfsu2-sw, settle the licensing question first
(`docs/drafts/nfsu2-sw-licensing-issue.md`): a PR into an unlicensed repo
has unclear terms.
