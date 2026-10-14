# Shared defaults for the scripts in tools/ and switch/. Sourced, not run.
#
# All paths derive from the repo's own location: run the scripts from
# anywhere, e.g. `bash tools/first_lift.sh` in the repo root.
#
# The game: the extracted disc (default.xbe + data) in the repo's game/
# folder (gitignored). It is only read, at lift time and at runtime, so
# reading it from a shared folder costs little.
#
# Everything generated lives in one workspace outside the repo, on the build
# machine's own disk (not a folder shared from the Mac: compiling ~100 MB of
# generated C through a shared folder is very slow).
#
#   $MC3_WORK/
#   ├── game/           alternative home for the disc, used when the repo's
#   │                   game/ has no default.xbe
#   ├── analysis/       xbe_parser / disasm / func_id / abi_analysis output
#   ├── gen/            lifted C from tools/regen.sh (never committed)
#   ├── build-linux/    Linux host build (SDL2 window, gdb)
#   ├── build-switch/   Switch NRO (OpenGL); build-switch-vk/ with VULKAN=1
#   └── deps/           Switch-side extras: mesa NVK SDK, glslang, FFmpeg
#
# Override any of these in the environment.

MC3_REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export MC3_WORK="${MC3_WORK:-$HOME/mc3-work}"
export XBOXRECOMP_DIR="${XBOXRECOMP_DIR:-$MC3_REPO/xboxrecomp}"
if [ -z "${MC3_GAME_DIR:-}" ]; then
    if [ -f "$MC3_REPO/game/default.xbe" ]; then
        MC3_GAME_DIR="$MC3_REPO/game"
    else
        MC3_GAME_DIR="$MC3_WORK/game"
    fi
fi
export MC3_GAME_DIR
export MC3_XBE="${MC3_XBE:-$MC3_GAME_DIR/default.xbe}"
export MC3_ANALYSIS_DIR="${MC3_ANALYSIS_DIR:-$MC3_WORK/analysis}"
export MC3_GEN_DIR="${MC3_GEN_DIR:-$MC3_WORK/gen}"

# The lift pipeline's Python (capstone) from vm/setup-ubuntu.sh, if present.
[ -x "$MC3_WORK/venv/bin/python3" ] && export PATH="$MC3_WORK/venv/bin:$PATH"

# The Switch app name: sdmc:/switch/$MC3_APP/ on the card.
export MC3_APP="${MC3_APP:-mcdub3}"

# Parallel compile jobs: ~1.5 GB of RAM per job on the generated chunks.
mc3_default_jobs() {
    local mem_mb cpus jobs
    if [ -r /proc/meminfo ]; then
        mem_mb=$(( $(awk '/^MemAvailable:/ {print $2}' /proc/meminfo) / 1024 ))
    else
        mem_mb=$(( $(sysctl -n hw.memsize) / 1048576 / 2 ))
    fi
    cpus=$(getconf _NPROCESSORS_ONLN)
    jobs=$(( mem_mb / 1500 ))
    [ "$jobs" -lt 1 ] && jobs=1
    [ "$jobs" -gt "$cpus" ] && jobs=$cpus
    echo "$jobs"
}

mc3_fail() { echo "error: $*" >&2; exit 1; }

# Fail with the ways to get the extracted disc where the scripts look for it.
mc3_need_game() {
    [ -f "$MC3_GAME_DIR/default.xbe" ] && return 0
    echo "error: no default.xbe in $MC3_REPO/game or $MC3_WORK/game" >&2
    echo "  Put the extracted disc (default.xbe + data) directly in the repo's game/ folder." >&2
    echo "  - from an ISO:              tools/extract_disc.sh <file.iso>" >&2
    echo "  - or use it in place:       export MC3_GAME_DIR=<folder with default.xbe>" >&2
    exit 1
}
