# Release notes

## v0.2.0

The European disc is supported, and career races no longer crash.

### Install

1. Copy the NRO for your edition to `sdmc:/switch/mcdub3-nx/`.
2. Copy the extracted disc into `sdmc:/switch/mcdub3-nx/game/`. To keep both
   editions on the card, put the European disc in `game-eur/` instead: each
   NRO looks in its own `game-<edition>/` folder first, then in `game/`.
3. Start the NRO from the Homebrew Menu in title takeover mode (hold **R**
   while launching a game), or install the NSP for your edition.

| File                | Edition                                                     |
| ------------------- | ----------------------------------------------------------- |
| `mcdub3-nx-usa.nro` | NTSC-U (North America)                                      |
| `mcdub3-nx-eur.nro` | PAL (Europe: English, French, German, Spanish and Italian)  |

Both NROs come with their own NSP forwarder, so they can be installed side by
side.

### What's new

- **European (PAL) edition**: its own NRO and NSP. The game's other languages
  follow the console language setting in `mcdub3_env.txt`:
  `RECOMP_LANGUAGE=fr`, `de`, `es` or `it`.
- **Career races no longer crash** when a race reports its result. This
  crashed every career race.
- **Readable tutorial text**: the line under the race results and on the
  loading screen is no longer scrambled.
- **Higher resolution and UI scale**, up to 5x (`RECOMP_GL_SCALE`,
  `RECOMP_GL_UI_SCALE`). On the Switch, stay at 1.5 or below.
- **Saves**: profiles from an original Xbox (a memory unit dump such as a
  `.gsx` from a save site) can be imported with `make import-save`, and
  profiles can be copied between the Switch and a computer over FTP with
  `make save-sync-down` / `make save-sync-up` (see the README's Saves section).

### Known issues

- Menus and the city flythroughs run below full speed on the Switch.
- The circles on the career loading screen sometimes stutter or don't
  complete; this is being investigated.
- Occasional hangs on career loading screens are being worked on.

## v0.1.0

First beta release of the game.

### Install

1. Copy the NRO for your edition to `sdmc:/switch/mcdub3-nx/`.
2. Copy the extracted disc into `sdmc:/switch/mcdub3-nx/game/`.
3. Start the NRO from the Homebrew Menu in title takeover mode (hold **R**
   while launching a game).

| File                | Edition                |
| ------------------- | ---------------------- |
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
