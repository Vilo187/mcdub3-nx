# Release notes

## v0.3.0

### Fixes

- **Smooth loading screens.** The circles on the career loading screen now turn smoothly and close, on every platform. The game copies each frame into the next one with the Xbox GPU's 2D engine and only draws what changed.
- **Faster menus on the Switch.** Moving through the main menu and its city flythrough went from about 20 to 27-30 frames per second. A background thread of the game checked a one-second timer thousands of times a second and kept interrupting the main thread. It now checks every few milliseconds.
- **More of the game lifted correctly.** Functions that end in a switch table were sometimes cut into pieces when the game's code was converted. They are now kept whole.

### Other changes

- On the Switch, the previous run's log is kept as `mcdub3_log_prev.txt` next
  to `mcdub3_log.txt`, so a hang can still be reported after the game was
  started again.
- We're preparing to port Midnight Club 3: Dub Edition Remix and integrate its code into this repo. Tools and scripts have been updated and the repo has been reorganised to support parallel builds for these different games.

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

### Building from source

- Generated files now live in `work-dub/` instead of `work/`. The first
  `make` after updating moves the folder for you; run `make lift` again
  after updating, as the converted game code changed.
- `make mac-app` takes the disc folder as `DISC=` (it was `GAME=`).

### Known issues

- Driving on the Switch runs at 18-25 frames per second.
- A race can occasionally hang (the screen stops, the game does not crash).
  If it happens, leave it for about ten seconds before quitting and share
  `mcdub3_log.txt` (or `mcdub3_log_prev.txt` if you started the game again).
- Right after a race starts, the game can pause for about a second.
- With fast storage the career loading screen can end before its closing
  animation has played; this is how the game reacts to a load that finishes
  very quickly.

## v0.2.0

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
