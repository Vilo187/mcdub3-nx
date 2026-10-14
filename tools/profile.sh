#!/usr/bin/env bash
# Where the time goes: a sampling profile of a headless run (Linux perf).
#
#   tools/profile.sh [seconds]      (default 40; make profile SECS=n)
#
# Samples every thread ~1000x a second and writes the hottest functions --
# lifted sub_XXXXXXXX, runtime, renderer, libc -- to
# work/logs/profile_<time>.txt (per function, and per thread).
#
# One-time setup (vm/setup-ubuntu.sh does it): perf for the running kernel,
#   sudo apt install linux-tools-$(uname -r) linux-tools-generic
# and permission to profile your own processes:
#   sudo sysctl kernel.perf_event_paranoid=1
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"

SECS="${1:-${SECS:-40}}"
BIN="${BIN:-$MC3_WORK/build-linux/mc3_recomp}"
[ -x "$BIN" ] || mc3_fail "no binary at $BIN (make build)"
mc3_need_game
command -v perf >/dev/null && perf --version >/dev/null 2>&1 \
    || mc3_fail "perf not usable: sudo apt install linux-tools-\$(uname -r) linux-tools-generic"
if [ "$(cat /proc/sys/kernel/perf_event_paranoid)" -gt 1 ]; then
    mc3_fail "perf_event_paranoid=$(cat /proc/sys/kernel/perf_event_paranoid): sudo sysctl kernel.perf_event_paranoid=1"
fi

mkdir -p "$MC3_WORK/logs" "$MC3_WORK/save"
STAMP=$(date +%Y%m%d_%H%M%S)
DATA="$MC3_WORK/logs/profile_$STAMP.data"
OUT="$MC3_WORK/logs/profile_$STAMP.txt"
export MC3_GAME_DIR MC3_SAVE_DIR="${MC3_SAVE_DIR:-$MC3_WORK/save}"
cd "$MC3_WORK"

echo "profiling ${SECS}s headless -> $OUT"
xvfb-run -a perf record -q -F 999 -o "$DATA" -- \
    timeout --signal=KILL "$SECS" "$BIN" > "$MC3_WORK/logs/profile_$STAMP.run.log" 2>&1 || true
# Reports never fail the script: an empty section is information too.
set +e +o pipefail
{
    echo "== hottest functions, all threads"
    perf report -i "$DATA" --stdio --no-children --sort symbol 2>&1 \
        | grep -v '^#' | grep -v '^$' | head -40
    echo
    echo "== per thread (pid/tid, function)"
    perf report -i "$DATA" --stdio --no-children --sort pid,tid,symbol 2>&1 \
        | grep -v '^#' | grep -v '^$' | head -40
} > "$OUT"
rm -f "$DATA"
echo "done: $OUT"
head -15 "$OUT"
