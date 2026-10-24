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
#       ├── build-linux/, build-mac/, build-switch/ ...
#       ├── switch_sd/  SD-card staging
#       ├── logs/, elf/
#       ├── venv/       Python + capstone for the lift pipeline
#       └── deps/       Switch-side extras (mesa NVK SDK, glslang, FFmpeg)
#
# Overrides, all optional: MC3_GAME_DIR (disc), MC3_WORK (generated files).

MC3_REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export MC3_WORK="${MC3_WORK:-$MC3_REPO/work}"
export XBOXRECOMP_DIR="${XBOXRECOMP_DIR:-$MC3_REPO/xboxrecomp}"
# The disc: game/, or game-<edition>/ beside it for several editions side by
# side (EDITION=eur picks game-eur/; config/xbe_editions.h lists the editions,
# one lift runs them all). Without EDITION: game/, else game-usa/.
# MC3_GAME_DIR overrides all of it.
if [ -z "${MC3_GAME_DIR:-}" ]; then
    if [ -n "${EDITION:-}" ]; then
        MC3_GAME_DIR="$MC3_REPO/game-$EDITION"
        # The first disc may still sit in game/ (before editions): usa.
        if [ "$EDITION" = usa ] && [ ! -e "$MC3_GAME_DIR/default.xbe" ] && [ -e "$MC3_REPO/game/default.xbe" ]; then
            MC3_GAME_DIR="$MC3_REPO/game"
        fi
    elif [ ! -e "$MC3_REPO/game/default.xbe" ] && [ -e "$MC3_REPO/game-usa/default.xbe" ]; then
        MC3_GAME_DIR="$MC3_REPO/game-usa"
    else
        MC3_GAME_DIR="$MC3_REPO/game"
    fi
fi
export MC3_GAME_DIR
export MC3_XBE="${MC3_XBE:-$MC3_GAME_DIR/default.xbe}"
export MC3_ANALYSIS_DIR="${MC3_ANALYSIS_DIR:-$MC3_WORK/analysis}"
export MC3_GEN_DIR="${MC3_GEN_DIR:-$MC3_WORK/gen}"

# The desktop host: Linux (the VM) or macOS (native, Apple Silicon). Each
# builds into its own tree under work/ (build-linux*, build-mac*), so the
# VM and the Mac can share the workspace folder.
case "$(uname -s)" in
    Darwin) MC3_HOST=mac ;;
    MINGW*|MSYS*|CYGWIN*) MC3_HOST=windows ;;   # MSYS2 (tools/setup_windows.sh)
    *) MC3_HOST=linux ;;
esac
export MC3_HOST
# Windows: the MSYS2 environment decides the architecture (CLANG64 = x86-64,
# CLANGARM64 = ARM64); builds go to work/build-windows-<arch>/.
if [ "$MC3_HOST" = windows ]; then
    case "${MSYSTEM:-}" in
        CLANGARM64) export MC3_WIN_ARCH=aarch64 ;;
        CLANG64) export MC3_WIN_ARCH=x86_64 ;;
        *) echo "error: run this in MSYS2's CLANG64 or CLANGARM64 shell (install_windows.bat does)" >&2; exit 1 ;;
    esac
fi

# macOS: build and run as arm64 with the Apple Silicon Homebrew
# (/opt/homebrew). A shell under Rosetta (uname -m = x86_64) would pick the
# Intel Homebrew in /usr/local and build an x86 binary; re-run natively.
# Call at the top of a script: mc3_mac_native "$0" "$@".
mc3_mac_native() {
    [ "$MC3_HOST" = mac ] || return 0
    if [ "$(uname -m)" != arm64 ] && [ "$(sysctl -n hw.optional.arm64 2>/dev/null)" = 1 ]; then
        exec arch -arm64 /bin/bash "$@"
    fi
    [ -d /opt/homebrew/bin ] && export PATH="/opt/homebrew/bin:$PATH"
    return 0
}

# The lift pipeline's Python: the first that can import capstone, from the
# project venv (vm/setup-ubuntu.sh) or the system. Scripts call mc3_python.
mc3_python() {
    local py
    # work/venv: Linux (vm/setup-ubuntu.sh); work/macvenv: macOS
    # (tools/setup_mac.sh). A shared work/ may hold both.
    for py in "$MC3_WORK/venv/bin/python3" "$MC3_WORK/macvenv/bin/python3" python3; do
        if "$py" -c 'import capstone' >/dev/null 2>&1; then
            MC3_PYTHON="$py"
            return 0
        fi
    done
    echo "error: no Python with capstone (the x86 disassembler the lift needs)" >&2
    echo "  - run the setup, which creates the venv:      make setup" >&2
    echo "  - or install it for your user:                python3 -m pip install --user capstone" >&2
    exit 1
}

# The Switch app: files named $MC3_APP (.nro, _env.txt, _log.txt) in
# sdmc:/switch/$MC3_SD_DIR/ on the card.
export MC3_APP="${MC3_APP:-mcdub3}"
export MC3_SD_DIR="${MC3_SD_DIR:-mcdub3-nx}"

# Parallel compile jobs: ~1.5 GB of RAM per job on the generated chunks.
mc3_default_jobs() {
    local mem_mb cpus jobs
    if [ -r /proc/meminfo ]; then
        # MemAvailable on Linux; MSYS2's /proc/meminfo has only MemFree and
        # MemTotal (an empty value broke the arithmetic, 2026-10-23).
        mem_mb=$(awk '/^MemAvailable:/ {a=$2} /^MemFree:/ {f=$2} /^MemTotal:/ {t=$2}
                      END {print int((a ? a : (f ? f : t / 2)) / 1024)}' /proc/meminfo)
    else
        mem_mb=$(( $(sysctl -n hw.memsize) / 1048576 / 2 ))
    fi
    [ -n "$mem_mb" ] || mem_mb=3000
    cpus=$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || echo 2)
    jobs=$(( mem_mb / 1500 ))
    [ "$jobs" -lt 1 ] && jobs=1
    [ "$jobs" -gt "$cpus" ] && jobs=$cpus
    echo "$jobs"
}

mc3_fail() { echo "error: $*" >&2; exit 1; }

# Which lifted functions src/recomp_manual.c replaces or wraps, as the lifter
# sees it: the toolkit's own scanner (tools/recomp/manual_scan.py), which
# skips `#if 0` blocks and counts `extern void sub_X_gen(void);` as a wrap.
# regen.sh records this list next to the lifted C; the build compares.
mc3_manual_overrides() {
    local py
    for py in "$MC3_WORK/venv/bin/python3" python3; do
        (cd "$XBOXRECOMP_DIR" && "$py" - "$MC3_REPO/src/recomp_manual.c" "$MC3_GEN_DIR/zz_trace_wrappers.c" <<'PY'
import os, sys
from tools.recomp.manual_scan import scan
replaced, wrapped = set(), set()
for path in sys.argv[1:]:          # the overrides, and the trace wrappers if lifted
    if os.path.exists(path):
        r, w = scan(path)[:2]
        replaced |= set(r)
        wrapped |= set(w)
for a in sorted(replaced): print("replaces sub_%08X" % a)
for a in sorted(wrapped):  print("wraps    sub_%08X" % a)
PY
        ) 2>/dev/null && return 0
    done
    return 1
}

# Stop a build of lifted C that predates its inputs:
#  - new seeds only take effect after a full regen;
#  - adding or removing an override in src/recomp_manual.c needs a relift, so
#    the lifter drops (or restores) the lifted body it replaces -- otherwise
#    "multiple definition of sub_XXXXXXXX" at link time. Editing the body of an
#    existing override, or any other C, is just a build.
# MC3_SKIP_LIFT_CHECK=1 skips this.
mc3_check_lift_fresh() {
    local gen="$MC3_GEN_DIR/recomp_funcs.h" stamp="$MC3_GEN_DIR/.manual_overrides"
    [ "${MC3_SKIP_LIFT_CHECK:-0}" = "1" ] && return 0
    [ -f "$gen" ] || mc3_fail "no lifted C in $MC3_GEN_DIR: run make lift"
    if [ "$MC3_REPO/config/seed_functions.json" -nt "$gen" ]; then
        mc3_fail "config/seed_functions.json is newer than the lifted C: run make regen first (make regen build)"
    fi
    local now
    if ! now=$(mc3_manual_overrides | tr -d '\r'); then
        echo "warning: no Python to scan src/recomp_manual.c; override check skipped" >&2
        return 0
    fi
    # Without CRs: Windows' Python writes CRLF, and a work/ shared with a
    # Windows VM then failed the check both ways (2026-10-23).
    if [ "$now" != "$(tr -d '\r' < "$stamp" 2>/dev/null)" ]; then
        echo "error: the overrides in src/recomp_manual.c differ from the last lift:" >&2
        diff <(tr -d '\r' < "$stamp" 2>/dev/null) <(echo "$now") | grep '^[<>]' \
            | sed 's/^</  gone since the lift: /; s/^>/  new since the lift:  /' >&2
        mc3_fail "run make relift first (make relift build)"
    fi
}

# Fail with the ways to get the extracted disc where the scripts look for it.
mc3_need_game() {
    [ -f "$MC3_GAME_DIR/default.xbe" ] && return 0
    echo "error: no default.xbe in $MC3_GAME_DIR" >&2
    [ -n "${EDITION:-}" ] && echo "  (EDITION=$EDITION: the disc goes in game-$EDITION/, e.g. make extract ISO=<file.iso> EDITION=$EDITION)" >&2
    echo "  Put the extracted disc (default.xbe + data) directly in the repo's game/ folder." >&2
    echo "  - from an ISO:              tools/extract_disc.sh <file.iso>" >&2
    echo "  - or use it in place:       export MC3_GAME_DIR=<folder with default.xbe>" >&2
    exit 1
}
