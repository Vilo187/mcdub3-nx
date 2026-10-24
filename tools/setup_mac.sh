#!/usr/bin/env bash
# One-time setup on a Mac (Apple Silicon): everything to lift the game and
# build it natively (make lift, make build run, make mac-app).
#
#   make setup          (on macOS; on Linux it runs vm/setup-ubuntu.sh)
#
# Safe to re-run. Needs the Xcode command line tools and Homebrew in
# /opt/homebrew (the Apple Silicon one); installs nothing outside them and
# work/.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
mc3_mac_native "$0" "$@"
[ "$MC3_HOST" = mac ] || mc3_fail "this is the macOS setup; on Linux: bash vm/setup-ubuntu.sh"

echo "== 1/3 Xcode command line tools (clang)"
if ! xcode-select -p >/dev/null 2>&1; then
    xcode-select --install || true
    mc3_fail "finish installing the command line tools in the window that opened, then run make setup again"
fi

echo "== 2/3 Homebrew packages: cmake ninja pkgconf sdl2 ffmpeg libepoxy dylibbundler"
BREW=/opt/homebrew/bin/brew
[ -x "$BREW" ] || mc3_fail "no Homebrew in /opt/homebrew: install it from https://brew.sh (an Intel one in /usr/local does not do)"
"$BREW" install cmake ninja pkgconf sdl2 ffmpeg libepoxy dylibbundler

echo "== 3/3 Python venv with capstone (the lift) and Pillow (icons, texture packs): $MC3_WORK/macvenv"
mkdir -p "$MC3_WORK"
PY=/opt/homebrew/bin/python3
[ -x "$PY" ] || PY=python3
[ -x "$MC3_WORK/macvenv/bin/python3" ] || "$PY" -m venv "$MC3_WORK/macvenv"
"$MC3_WORK/macvenv/bin/python3" -m pip install -q --upgrade pip capstone pillow

echo
echo "done. Next: put the extracted disc in game/ (or make extract ISO=<file>), then"
echo "  make lift        lift the game's code to C (once per disc; 10-20 minutes)"
echo "  make mac-app     the app, in work-dub/mac_app/ (or make build run)"
echo "Docker Desktop is only needed for Switch builds (make switch, make nsp)."
