# 8. NSPs are built locally; each game's identity lives in its config

Date: 2026-10-25

## Context

ADR 0005 built the NSP forwarders in the release workflow, on a
self-hosted runner holding the console keys (`MC3_KEYS`). That was the one
place the keys met CI, and the owner decided not to keep it.

The app's identity (name, version, editions, title IDs, artwork) was
`switch/app.env` for the DUB Edition and `switch/app-remix.env` for the
Remix, read by the Switch builds only; the Mac app and the Windows exe
took a fixed `assets/cover-art.jpg`, so a Remix package got the DUB
Edition's icon. A game's own things belong in `config/<game>/` (ADR 0006).

## Decision

- **NSPs: local only.** `make nsp` on the owner's machine, with
  `keys/prod.keys` there. No workflow builds NSPs or reads keys; releases
  publish the NROs. The keys rule (CLAUDE.md) has no exception any more.
- **Identity per game**: `config/<game>/app.env` -- name, version,
  editions, per-edition title ID / name / artwork, SD folder.
  `switch/app.env` keeps what the games share (author, the pinned NSP
  tools). Both are read through `mc3_app_env` (tools/env.sh).
- **Editions in the game's file, not files of their own**: editions of a
  game share the lift, the name and the version; what differs is a suffix,
  `APP_TITLE_ID_<edition>`, `APP_NAME_<edition>`, `APP_ICON_<edition>`.
- **One artwork per game** (`APP_ICON`, square, 1024x1024 best), each
  package's icon derived from it: Switch a bare 256x256 baseline JPEG
  (tools/app_icon.py, made on the host as the container has no Pillow;
  the NRO used the artwork as it was, which hbmenu does not show unless it
  is 256x256), Mac a rounded `.icns`, Windows an `.ico`.

## Consequences

- A release's NSPs are made and attached by hand (`make nsp`, then
  `gh release upload v<version> work-dub/nsp/*.nsp` if wanted).
- The `mcdub3-keys` runner label and `MC3_KEYS` are no longer used.
- CI reads the version from `config/dub/app.env`.
