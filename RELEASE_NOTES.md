# Release notes

One section per release, newest first, headed `## v<version>` with the
version in `switch/app.env` (`APP_VERSION`). The Switch release workflow
(`.github/workflows/switch-release.yml`) publishes the section of the
version it releases; write it before starting the release.

## v0.1.0

First test release of mcdub3-nx: the Xbox version of *Midnight Club 3: DUB
Edition*, recompiled to run natively on the Nintendo Switch.

You need your own copy of the game: the extracted Xbox disc (`default.xbe`
and its files, not the ISO). The NRO contains no game data and does not run
without it.

### Install

1. Copy the NRO for your edition to `sdmc:/switch/mcdub3-nx/`.
2. Copy the extracted disc into `sdmc:/switch/mcdub3-nx/game/`.
3. Start the NRO from the Homebrew Menu in title takeover mode (hold **R**
   while launching a game).

| File | Edition |
|---|---|
| `mcdub3-nx-usa.nro` | NTSC-U (North America) |

A build refuses a disc from another edition (or a modified `default.xbe`)
with a message, instead of crashing.

### What works

- Intro movies, menus, garage, career and races, with music and sound.
- Saves: create, load and delete profiles.
- Widescreen, higher resolutions and picture settings
  (`mcdub3_env.txt`; see the README's Settings section).
- Optional HD texture packs you make yourself from your disc.

### Known issues

- Menus and the city flythroughs run below full speed on the Switch.
- Occasional hangs on career loading screens and in races are being worked
  on.
