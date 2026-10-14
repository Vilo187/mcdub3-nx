# Shared defaults for the scripts in tools/, switch/ and vm/. Sourced, not run.
#
# Every path is relative to the repo root, found from this file's location, so
# the scripts work from anywhere (e.g. `bash tools/first_lift.sh` in the root).
#
#   <repo>/
#   ├── game/           the extracted disc: default.xbe + data (gitignored)
#   └── work/           everything generated (gitignored):
#       ├── analysis/   xbe_parser / disasm / func_id / abi_analysis output
#       ├── gen/        lifted C from tools/regen.sh
#       ├── build-linux/, build-switch/ ...
#       ├── switch_sd/  SD-card staging
#       ├── logs/, elf/
#       ├── venv/       Python + capstone for the lift pipeline
#       └── deps/       Switch-side extras (mesa NVK SDK, glslang, FFmpeg)
#
# Overrides, all optional: MC3_GAME_DIR (disc), MC3_WORK (generated files).

MC3_REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export MC3_WORK="${MC3_WORK:-$MC3_REPO/work}"
export XBOXRECOMP_DIR="${XBOXRECOMP_DIR:-$MC3_REPO/xboxrecomp}"
export MC3_GAME_DIR="${MC3_GAME_DIR:-$MC3_REPO/game}"
export MC3_XBE="${MC3_XBE:-$MC3_GAME_DIR/default.xbe}"
export MC3_ANALYSIS_DIR="${MC3_ANALYSIS_DIR:-$MC3_WORK/analysis}"
export MC3_GEN_DIR="${MC3_GEN_DIR:-$MC3_WORK/gen}"

# The lift pipeline's Python: the first that can import capstone, from the
# project venv (vm/setup-ubuntu.sh) or the system. Scripts call mc3_python.
mc3_python() {
    local py
    for py in "$MC3_WORK/venv/bin/python3" python3; do
        if "$py" -c 'import capstone' >/dev/null 2>&1; then
            MC3_PYTHON="$py"
            return 0
        fi
    done
    echo "error: no Python with capstone (the x86 disassembler the lift needs)" >&2
    echo "  - re-run the setup, which creates work/venv:  bash vm/setup-ubuntu.sh" >&2
    echo "  - or install it for your user:                python3 -m pip install --user capstone" >&2
    exit 1
}

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
    echo "error: no default.xbe in $MC3_GAME_DIR" >&2
    echo "  Put the extracted disc (default.xbe + data) directly in the repo's game/ folder." >&2
    echo "  - from an ISO:              tools/extract_disc.sh <file.iso>" >&2
    echo "  - or use it in place:       export MC3_GAME_DIR=<folder with default.xbe>" >&2
    exit 1
}
