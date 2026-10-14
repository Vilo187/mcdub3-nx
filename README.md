# Midnight Club 3: DUB Edition: Xbox static recompilation for Nintendo Switch

The original Xbox release of *Midnight Club 3: DUB Edition*, lifted
instruction by instruction to C with [xboxrecomp](https://github.com/sp00nznet/xboxrecomp)
and compiled natively for the Switch (and Linux). No emulator.

Organised after [nfsu2-sw](https://github.com/antoxa2584x/nfsu2-sw), whose
Switch layer for the toolkit this project builds on.

> **No game data is included.** You need your own copy of the Xbox disc.

## Status

Skeleton. The lift pipeline runs; the game has not been lifted yet.

## Pipeline

```
default.xbe ──xboxrecomp (Python)──▶ lifted C ──┬── GCC, Linux ──▶ windowed debug build
                                                └── devkitA64 ───▶ mc3_recomp.nro (Switch)
```

## Quick start (in the Ubuntu ARM64 VM: docs/dev-environment.md)

```sh
bash vm/setup-ubuntu.sh                 # once
# put the extracted disc (default.xbe + data) in game/; for an ISO: tools/extract_disc.sh
bash tools/first_lift.sh                # parse the XBE, write config/xbe_facts.h, lift
tools/build_linux.sh && tools/run_linux.sh
switch/build.sh                         # NRO + SD staging in work/switch_sd
```

## Layout

| Path | What |
|---|---|
| `src/main.c` | boot, runtime defaults, crash reporter |
| `src/recomp_manual.c` | hand-written overrides of lifted functions |
| `src/switch_nx.c` | Switch log, `mcdub3_env.txt` settings, exception handler, error dialog |
| `config/seed_functions.json` | entry points the static pass cannot see |
| `config/xbe_facts.h` | entry point + title ID, from `tools/first_lift.sh` |
| `xboxrecomp/` | the toolkit, vendored as labelled layers ([ADR 0002](docs/decisions/0002-vendored-toolkit-own-snapshot.md)) |
| `tools/` | `env.sh` (workspace paths), extract, lift, Linux build/run |
| `switch/build.sh` | Switch NRO build + SD-card staging |
| `vm/setup-ubuntu.sh` | one-time VM setup |
| `docs/decisions/` | why things are the way they are |

Everything is relative to the repo root, and both folders are gitignored:
`game/` holds the extracted disc; `work/` holds everything generated
(`analysis/`, `gen/`, `build-*/`, `switch_sd/`, `logs/`, `elf/`, `venv/`).

## On the Switch

```
sdmc:/switch/mcdub3/
├── mc3_recomp.nro
├── mcdub3_env.txt      optional: KEY=VALUE per line (any RECOMP_* / MC3_* switch)
├── mcdub3_log.txt      written when not launched through nxlink
└── game/               the extracted disc (not the ISO)
```

Start with title takeover (hold **R** while launching a game).

## Legal

Midnight Club is a trademark of Take-Two Interactive / Rockstar Games. This is
an unofficial fan project, not affiliated with or endorsed by them, Microsoft
or Nintendo.
