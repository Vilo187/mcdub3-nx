#!/usr/bin/env bash
# Turn the [HOSTSTACK] lines of a Windows log (the watchdog's every-thread
# stacks, platform/win32_stacks.c) into function names:
#
#   tools/windows/symbolize.sh work/logs/windows_run_<time>.log [exe]
#
# mcdub3.exe frames are resolved with llvm-symbolizer (llvm-mingw's, or one
# on PATH) against the exe that ran (default: the newest dist/mcdub3.exe);
# frames in DLLs keep their module+offset.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../env.sh"
LOG="${1:?usage: $0 <log> [exe]}"
EXE="${2:-$(ls -t "$MC3_WORK"/build-windows-*/dist/mcdub3.exe 2>/dev/null | head -1)}"
SYM=$(ls "$MC3_WORK"/tools/llvm-mingw-*/bin/llvm-symbolizer 2>/dev/null | tail -1 || true)
[ -n "$SYM" ] || SYM=$(command -v llvm-symbolizer || true)
[ -n "$SYM" ] || mc3_fail "no llvm-symbolizer (llvm-mingw in work/tools, or MSYS2's clang)"
echo "exe: $EXE"
# The base the exe was linked for (the log's own "linked" column is the live
# address once Windows has moved the image: it rewrites the header in memory).
OBJDUMP="$(dirname "$SYM")/llvm-objdump"
BASE=$("$OBJDUMP" -p "$EXE" 2>/dev/null | awk '/^ImageBase/ {print "0x"$2; exit}')
[ -n "$BASE" ] || BASE=0x140000000
grep '\[HOSTSTACK\]' "$LOG" | while IFS= read -r line; do
    if [[ "$line" =~ \[HOSTSTACK\]\ thread ]] || [[ "$line" =~ every\ thread ]]; then
        echo; echo "${line#*\] }"; continue
    fi
    if [[ "$line" =~ \#([0-9]+)\ +([^+ ]+)\+(0x[0-9a-f]+)\ +\(([^ ]+)\ (0x[0-9a-f]+)\) ]]; then
        n="${BASH_REMATCH[1]}" mod="${BASH_REMATCH[2]}" off="${BASH_REMATCH[3]}"
        addr=$(printf '0x%x' $((BASE + off)))
        if [ "$(echo "$mod" | tr A-Z a-z)" = mcdub3.exe ]; then
            fn=$("$SYM" --obj="$EXE" --functions=linkage --inlining=false "$addr" | sed -n '1p;2p' | paste -sd' ' -)
            printf '  #%-2s %s\n' "$n" "$fn"
        else
            printf '  #%-2s %s %s\n' "$n" "$mod" "$addr"
        fi
    fi
done
