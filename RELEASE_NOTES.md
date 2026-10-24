# Release notes

## v0.2.0

### Features

### Features
- **Support for European (PAL) disc.** Updated the [guided installation page](https://laureladastra.github.io/mcdub3-homepage/) and build artifacts (`mcdub3-nx-eur.nro`) to build for this edition.
- **Beta release for Windows.** Updated section in our [README](https://github.com/laureladastra/mcdub3-nx) contains the build and installation guide.
- **Readable tutorial video text**: the text within tutorial videos is no longer scrambled at higher game resolutions.
- **Saves**: profiles from an original Xbox (a memory unit dump such as a
  `.gsx` from a save site) can be imported with `make import-save`, and
  profiles can be copied between the Switch and a computer over FTP with
  `make save-sync-down` / `make save-sync-up` (see the README's Saves section).

### Install

1. Copy the NRO for your edition to `sdmc:/switch/mcdub3-nx/`.
2. Copy the extracted disc into `sdmc:/switch/mcdub3-nx/game/`. To keep both
   editions on the card, put the European disc in `game-eur/` instead: each
   NRO looks in its own `game-<edition>/` folder first, then in `game/`.
3. Start the NRO from the Homebrew Menu in title takeover mode (hold **R**
   while launching a game), or install the NSP for your edition.

| File                | Edition                                                    |
| ------------------- | ---------------------------------------------------------- |
| `mcdub3-nx-usa.nro` | NTSC-U (North America)                                     |
| `mcdub3-nx-eur.nro` | PAL (Europe: English, French, German, Spanish and Italian) |

### Known issues

- Menus and the city flythroughs run below full speed on the Switch.
- The circles on the career loading screen sometimes stutter.
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
