#!/usr/bin/env bash
# Build the Windows binary (mcdub3.exe), into work/build-windows-<arch>/dist/
# (natively) or work/build-wincross-<arch>/dist/ (cross) with the DLLs it
# needs and a launcher (run_mcdub3.bat).
#
# On Windows (MSYS2's CLANG64 or CLANGARM64 shell; make build there, or
# install_windows.bat for everything): MSYS2's clang and libraries, the
# shell's architecture.
#
# On macOS or Linux, a cross build with llvm-mingw (in work/tools/) and the
# same MSYS2 libraries, fetched by tools/win_deps.py:
#   tools/build_windows.sh               x86-64
#   WIN_ARCH=aarch64 tools/build_windows.sh  Windows on ARM
#
#   DEBUG=1 ...                          -O0 for the hand-written code
#   TARGET="xbox_kernel ..."             only these ninja targets (no dist/)
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
mc3_mac_native "$0" "$@"
mc3_check_lift_fresh

ARGS=()
if [ "$MC3_HOST" = windows ]; then
    ARCH="$MC3_WIN_ARCH"
    DLL_DIRS=("$MSYSTEM_PREFIX/bin")
    PY=python3
else
    ARCH="${WIN_ARCH:-x86_64}"
    case "$ARCH" in x86_64|aarch64) ;; *) mc3_fail "WIN_ARCH must be x86_64 or aarch64";; esac
    LLVM_MINGW="${MC3_LLVM_MINGW:-$(ls -d "$MC3_WORK"/tools/llvm-mingw-*/ 2>/dev/null | tail -1)}"
    LLVM_MINGW="${LLVM_MINGW%/}"
    [ -x "$LLVM_MINGW/bin/$ARCH-w64-mingw32-clang" ] \
        || mc3_fail "no llvm-mingw in work/tools: download a release (github.com/mstorsjo/llvm-mingw, *-ucrt-macos-universal or *-ucrt-ubuntu-*) and unpack it there"
    SYSROOT="$MC3_WORK/win-sysroot/$ARCH"
    PY=/opt/homebrew/bin/python3; [ -x "$PY" ] || PY=python3
    if [ ! -f "$SYSROOT/lib/pkgconfig/sdl2.pc" ]; then
        echo "== Windows libraries (MSYS2) into $SYSROOT"
        "$PY" "$MC3_REPO/tools/win_deps.py" "$ARCH" "$SYSROOT"
    fi
    export MC3_WIN_ARCH="$ARCH" MC3_LLVM_MINGW="$LLVM_MINGW" MC3_WIN_SYSROOT="$SYSROOT"
    ARGS+=(-DCMAKE_TOOLCHAIN_FILE="$MC3_REPO/tools/windows/toolchain.cmake")
    DLL_DIRS=("$SYSROOT/bin" "$LLVM_MINGW/$ARCH-w64-mingw32/bin")
fi

TYPE=RelWithDebInfo
[ "${DEBUG:-0}" = "1" ] && TYPE=Debug

# The icon (cover art, rounded like a Windows 11 app icon) and the release
# version, both embedded in mcdub3.exe. No Pillow: no icon, still a build.
ICON="$MC3_WORK/mcdub3.ico"
if [ ! -f "$ICON" ] || [ "$MC3_REPO/assets/cover-art.jpg" -nt "$ICON" ]; then
    ICON_OK=""
    for IPY in "$MC3_WORK/macvenv/bin/python3" "$MC3_WORK/venv/bin/python3" "$PY"; do
        [ -x "$IPY" ] || command -v "$IPY" >/dev/null || continue
        "$IPY" "$MC3_REPO/tools/windows/win_icon.py" "$MC3_REPO/assets/cover-art.jpg" "$ICON" 2>/dev/null \
            && { ICON_OK=1; break; }
    done
    [ -n "$ICON_OK" ] || { echo "  (no icon: no Python with Pillow)"; ICON=""; }
fi
APP_VERSION=$(sed -n 's/^APP_VERSION="\(.*\)"/\1/p' "$MC3_REPO/switch/app.env")
ARGS+=(-DMC3_WIN_ICON="$ICON" -DMC3_VERSION="${APP_VERSION:-dev}")
# Native (MSYS2) and cross builds keep separate trees: work/ may be shared
# between the Mac and a Windows VM, and CMake refuses a tree configured at
# another path (the first install stopped there, 2026-10-23).
if [ "$MC3_HOST" = windows ]; then
    BUILD="${BUILD_DIR:-$MC3_WORK/build-windows-$ARCH}"
else
    BUILD="${BUILD_DIR:-$MC3_WORK/build-wincross-$ARCH}"
fi
cmake -S "$MC3_REPO" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE="$TYPE" ${ARGS[@]+"${ARGS[@]}"} \
    -DXBOXRECOMP_DIR="$XBOXRECOMP_DIR" -DMC3_GEN_DIR="$MC3_GEN_DIR" -DMC3_VULKAN=OFF \
    -DMC3_GEN_DEBUGINFO=OFF >/dev/null
# shellcheck disable=SC2086
ninja -C "$BUILD" -j"${JOBS:-$(mc3_default_jobs)}" ${TARGET:-}

[ -n "${TARGET:-}" ] && exit 0      # single targets: no exe to package

# dist/: the exe and every DLL it pulls in (Windows looks beside the exe).
DIST="$BUILD/dist"
[ -f "$DIST/mcdub3_env.txt" ] && cp "$DIST/mcdub3_env.txt" "$BUILD/mcdub3_env.txt.keep"
rm -rf "$DIST"; mkdir -p "$DIST"
[ -f "$BUILD/mcdub3_env.txt.keep" ] && mv "$BUILD/mcdub3_env.txt.keep" "$DIST/mcdub3_env.txt"
cp "$BUILD/mcdub3.exe" "$DIST/"
"$PY" "$MC3_REPO/tools/windows/copy_dlls.py" "$DIST/mcdub3.exe" "${DLL_DIRS[@]}" "$DIST"

# The launcher: the disc from MC3_GAME_DIR, else game\ beside the exe, else
# the repo's game\ (dist\ sits in work\build-windows-<arch>\); saves' drive
# letters in the repo's work\save\, a log per run in work\logs\windows_run_<time>.log
# (else mcdub3_log.txt beside the exe); refuses a second start.
printf '%s\r\n' \
  '@echo off' \
  'rem mcdub3-nx on Windows.' \
  'pushd "%~dp0"' \
  'rem Settings: NAME=value lines in mcdub3_env.txt beside the exe (# comments), as on the Switch.' \
  'if exist "%~dp0mcdub3_env.txt" for /f "usebackq eol=# tokens=* delims=" %%l in ("%~dp0mcdub3_env.txt") do set "%%l"' \
  'if not defined MC3_GAME_DIR if exist "%~dp0game\default.xbe" set "MC3_GAME_DIR=%~dp0game"' \
  'if not defined MC3_GAME_DIR if exist "%~dp0..\..\..\game\default.xbe" set "MC3_GAME_DIR=%~dp0..\..\..\game"' \
  'if not defined MC3_GAME_DIR (echo No game folder: put the extracted disc in the game\ folder of the repo, or set MC3_GAME_DIR & pause & exit /b 1)' \
  'if not defined MC3_SAVE_DIR if exist "%~dp0..\..\save\" set "MC3_SAVE_DIR=%~dp0..\..\save"' \
  'tasklist /fi "imagename eq mcdub3.exe" | find /i "mcdub3.exe" >nul && (echo The game is already running: close it first, or end mcdub3.exe in Task Manager & pause & exit /b 1)' \
  'for /f %%t in ('"'"'powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"'"'"') do set "TS=%%t"' \
  'set "LOG=%~dp0mcdub3_log.txt"' \
  'if exist "%~dp0..\..\logs\" set "LOG=%~dp0..\..\logs\windows_run_%TS%.log"' \
  'echo Game: %MC3_GAME_DIR%' \
  'echo Log:  %LOG%' \
  'mcdub3.exe > "%LOG%" 2>&1' \
  'set "RC=%ERRORLEVEL%"' \
  'popd' \
  'if not "%RC%"=="0" (echo The game stopped with code %RC%: see %LOG% & pause)' > "$DIST/run_mcdub3.bat"
echo "built $DIST/mcdub3.exe   (start it with run_mcdub3.bat, or make run)"
