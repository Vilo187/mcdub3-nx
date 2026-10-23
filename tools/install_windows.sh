#!/usr/bin/env bash
# Everything after MSYS2 for a Windows install, run by install_windows.bat in
# MSYS2's CLANG64 / CLANGARM64 shell: packages, the disc, the lift, the build
# (which also makes the desktop shortcut). Safe to re-run: each step is skipped when done.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
[ "$MC3_HOST" = windows ] || mc3_fail "this is the Windows install (MSYS2); elsewhere: make setup lift build"
cd "$MC3_REPO"

step() { echo; echo "==== $*"; }

step "1/4 Packages (MSYS2, $MSYSTEM)"
bash tools/setup_windows.sh

step "2/4 The disc"
if [ ! -f "$MC3_GAME_DIR/default.xbe" ]; then
    ISO=$(ls "$MC3_GAME_DIR"/*.iso "$MC3_GAME_DIR"/*.ISO 2>/dev/null | head -1 || true)
    if [ -n "$ISO" ]; then
        echo "unpacking $(basename "$ISO") into game/"
        mv "$ISO" "$MC3_WORK/" 2>/dev/null || { mkdir -p "$MC3_WORK"; mv "$ISO" "$MC3_WORK/"; }
        bash tools/extract_disc.sh "$MC3_WORK/$(basename "$ISO")"
    else
        mc3_fail "no game in $MC3_GAME_DIR: put the extracted disc (default.xbe and the rest) or the disc's .iso in the game folder, then run install_windows.bat again"
    fi
fi
echo "found $MC3_GAME_DIR/default.xbe"

step "3/4 Lift the game's code to C (once per disc; 10-30 minutes)"
if [ -f "$MC3_GEN_DIR/recomp_funcs.h" ] && MC3_SKIP_LIFT_CHECK=0 bash -c "source tools/env.sh; mc3_check_lift_fresh" 2>/dev/null; then
    echo "already lifted"
else
    make lift
fi

step "4/4 Build (20-60 minutes the first time)"
make build

echo
echo "done. Start the game with the 'Midnight Club 3' shortcut on the desktop"
echo "(or $(cygpath -w "$MC3_WORK/build-windows-$MC3_WIN_ARCH/dist/mcdub3.exe"))."
