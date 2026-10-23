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
PKGS=(make git "$P-clang" "$P-cmake" "$P-ninja" "$P-pkgconf" "$P-SDL2" "$P-ffmpeg"
      "$P-python" "$P-python-capstone" "$P-python-pillow")
# The mirrors rate-limit (HTTP 429, "Too Many Requests"): about 190 packages
# five at a time made pacman give up on a first install (2026-10-23). Try
# again after a pause -- downloaded packages stay in pacman's cache -- and
# from the second try one download at a time.
for try in 1 2 3 4 5; do
    if pacman -S --needed --noconfirm "${PKGS[@]}"; then
        break
    fi
    [ "$try" = 5 ] && mc3_fail "the MSYS2 packages could not be downloaded (the mirrors may be busy): run install_windows.bat again later"
    if grep -q '^ParallelDownloads' /etc/pacman.conf; then
        sed -i 's/^ParallelDownloads.*/ParallelDownloads = 1/' /etc/pacman.conf
        echo "== downloading one package at a time from now on (/etc/pacman.conf)"
    fi
    wait_s=$((try * 30))
    echo "== the download failed (try $try of 5); trying again in $wait_s s"
    sleep "$wait_s"
done

python3 -c 'import capstone' || mc3_fail "Python cannot import capstone"
echo
echo "done. Next: put the extracted disc in game/, then"
echo "  make lift        lift the game's code to C (once per disc; 10-30 minutes)"
echo "  make build run   build and play"
