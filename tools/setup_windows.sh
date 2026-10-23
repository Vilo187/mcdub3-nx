#!/usr/bin/env bash
# One-time setup on Windows, in MSYS2's CLANG64 (x86-64) or CLANGARM64 (ARM64)
# shell: everything to lift the game and build it (make lift, make build run).
#
#   make setup          (in that shell; install_windows.bat does it all)
#
# Safe to re-run. Installs only MSYS2 packages: clang, CMake, Ninja, SDL2,
# FFmpeg, and Python with capstone (the lift) and Pillow (icons, texture
# packs).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
[ "$MC3_HOST" = windows ] || mc3_fail "this is the Windows setup (MSYS2)"

P="${MINGW_PACKAGE_PREFIX:?run this in the MSYS2 CLANG64 or CLANGARM64 shell}"
echo "== MSYS2 packages for $MSYSTEM"
pacman -S --needed --noconfirm make git \
    "$P-clang" "$P-cmake" "$P-ninja" "$P-pkgconf" "$P-SDL2" "$P-ffmpeg" \
    "$P-python" "$P-python-capstone" "$P-python-pillow"

python3 -c 'import capstone' || mc3_fail "Python cannot import capstone"
echo
echo "done. Next: put the extracted disc in game/, then"
echo "  make lift        lift the game's code to C (once per disc; 10-30 minutes)"
echo "  make build run   build and play"
