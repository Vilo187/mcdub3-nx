#!/usr/bin/env bash
# Run the Linux build in a window, logging to $MC3_WORK/logs/.
#
#   tools/run_linux.sh                        play: window, keyboard/controller
#   GDB=1 tools/run_linux.sh                  under gdb (break sub_XXXXXXXX works:
#                                             every lifted function is a C symbol)
#   HEADLESS=1 tools/run_linux.sh             no window (xvfb); pair with
#                                             RECOMP_GL_DUMP=frames/f,60 and
#                                             RECOMP_PAD_SCRIPT for scripted runs
#   SECS=120 tools/run_linux.sh               stop after N seconds
#
# Any RECOMP_* / MC3_* variable passes straight through, e.g.
#   RECOMP_KEYBOARD=1 RECOMP_WATCHDOG_SECS=30 tools/run_linux.sh
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"

SUFFIX=""
[ "${VULKAN:-0}" = "1" ] && SUFFIX="-vk"
[ "${DEBUG:-0}" = "1" ] && SUFFIX="$SUFFIX-dbg"
BIN="${BIN:-$MC3_WORK/build-linux$SUFFIX/mc3_recomp}"
[ -x "$BIN" ] || mc3_fail "no binary at $BIN (tools/build_linux.sh)"
[ -f "$MC3_GAME_DIR/default.xbe" ] || mc3_fail "no extracted disc in $MC3_GAME_DIR"

mkdir -p "$MC3_WORK/logs"
LOG="$MC3_WORK/logs/run_$(date +%Y%m%d_%H%M%S).log"
export MC3_GAME_DIR
# Relative outputs (frame dumps, progcache.bin) land in the workspace.
cd "$MC3_WORK"

CMD=("$BIN")
[ -n "${SECS:-}" ] && CMD=(timeout --signal=KILL "$SECS" "${CMD[@]}")
[ "${HEADLESS:-0}" = "1" ] && CMD=(xvfb-run -a "${CMD[@]}")

if [ "${GDB:-0}" = "1" ]; then
    # gdb stops at the fault itself, before the crash reporter runs:
    # `bt` gives the host chain (lifted sub_XXXXXXXX frames),
    # `call print_guest_state()` gives the guest's.
    exec gdb -q -ex run --args "$BIN"
fi
echo "log: $LOG"
"${CMD[@]}" 2>&1 | tee "$LOG"
