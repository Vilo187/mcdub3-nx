#!/usr/bin/env bash
# Build the Linux host binary: the day-to-day debug build. Same lifted code
# and runtime as the Switch; opens an SDL2 window.
#
#   tools/build_linux.sh              OpenGL renderer, RelWithDebInfo
#   VULKAN=1 tools/build_linux.sh     Vulkan renderer (what the Switch ships)
#   DEBUG=1 tools/build_linux.sh      -O0 for the hand-written code (gdb stepping);
#                                     the lifted C stays at MC3_GEN_OPT
#   GEN_DEBUGINFO=1 ...               -g for the lifted C too (line-level stepping
#                                     in sub_XXXXXXXX; costs gigabytes of disk)
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
mc3_check_lift_fresh

SUFFIX=""; ARGS=(-DMC3_VULKAN=OFF)
[ "${VULKAN:-0}" = "1" ] && { SUFFIX="-vk"; ARGS=(-DMC3_VULKAN=ON); }
TYPE=RelWithDebInfo
[ "${DEBUG:-0}" = "1" ] && { TYPE=Debug; SUFFIX="$SUFFIX-dbg"; }
ARGS+=(-DMC3_GEN_DEBUGINFO="$([ "${GEN_DEBUGINFO:-0}" = "1" ] && echo ON || echo OFF)")
BUILD="${BUILD_DIR:-$MC3_WORK/build-linux$SUFFIX}"

cmake -S "$MC3_REPO" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE="$TYPE" \
    -DXBOXRECOMP_DIR="$XBOXRECOMP_DIR" -DMC3_GEN_DIR="$MC3_GEN_DIR" "${ARGS[@]}" >/dev/null
ninja -C "$BUILD" -j"${JOBS:-$(mc3_default_jobs)}"
echo "built $BUILD/mc3_recomp   (run: tools/run_linux.sh)"
