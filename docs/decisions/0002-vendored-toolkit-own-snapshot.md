# 2. Vendor the toolkit as our own snapshot, layered by origin

Date: 2026-10-11

## Context

- Upstream xboxrecomp (sp00nz + ~10 contributors, MIT) moves fast: 509 commits
  since 2026-09-01. It has no Switch support.
- nfsu2-sw (one author) carries the Switch layer in its vendored copy:
  OpenGL/Vulkan pushbuffer renderers, Joy-Con input, Horizon memory/threads,
  translator passes (+16.9k/−8.1k lines over upstream). Its own `src/` has no
  licence; the toolkit files keep upstream's MIT.
- We don't want to depend on either project's pace.

## Decision

`xboxrecomp/` is a git subtree, built as labelled layers:

1. Upstream `49ee746` (2026-09-27), imported with history: the commit
   nfsu2-sw's copy was forked from.
2. nfsu2-sw's delta in six commits by area (renderers, input/USB, APU, kernel,
   recompiler, build/docs), authored as Anton Artemov, verified byte-identical
   to `antoxa2584x/nfsu2-sw@6e19726:xboxrecomp`. It includes upstream PR #128
   (unmerged upstream), whose conflicts with `49ee746` were resolved there.
3. Our own commits, prefixed `xboxrecomp:`.

nfsu2-sw's `src/` is reference only; ours is written from the toolkit's MIT
template and the toolkit API.

## Updating

```sh
git fetch xboxrecomp-upstream
git merge -X subtree=xboxrecomp xboxrecomp-upstream/main
```

A trial merge on 2026-10-11 (101 upstream commits ahead) conflicted in 22
files, mostly kernel, APU and recompiler files the Switch layer also changes.
Merge deliberately, when an upstream fix is wanted, not routinely.

Keep our toolkit edits small and in new files where possible, and offer
generic fixes upstream; the less we carry alone, the cheaper each merge.

## Known gaps found while vendoring

- nfsu2-sw's published toolkit lacks `tools/{disasm,func_id,recomp}/output.py`
  (the pipeline can't import); restored from `49ee746`.
- The renderers hard-coded `sdmc:/switch/nfsu2x/`; now `XBOXRECOMP_APP_NAME`.

## Open

Ask nfsu2-sw to license `src/` or upstream the Switch layer into xboxrecomp
(draft: `docs/drafts/nfsu2-sw-licensing-issue.md`; not posted).
