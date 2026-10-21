#!/usr/bin/env bash
# Build the Switch NRO and stage an SD-card layout:
#
#   <SD>/switch/mcdub3-nx/mcdub3.nro          (mcdub3-vulkan.nro with VULKAN=1)
#   <SD>/switch/mcdub3-nx/game/               the extracted disc (never the ISO)
#   <SD>/switch/mcdub3-nx/mcdub3_env.txt      optional KEY=VALUE runtime switches
#   <SD>/switch/mcdub3-nx/hd/textures/        the HD texture pack, with HD=1 only
#   <SD>/switch/mcdub3-nx/mcdub3_log.txt      written when no nxlink host listens
#
# Environment (defaults from tools/env.sh):
#   DEVKITPRO      /opt/devkitpro (switch-dev, switch-sdl2, switch-mesa)
#   MC3_GEN_DIR    lifted C from tools/regen.sh
#   SD_ROOT        staging SD root (default $MC3_WORK/switch_sd)
#   BUILD_DIR      default $MC3_WORK/build-switch[-vk][-lto]
#   JOBS           parallel compile jobs (default: by free memory)
#   VULKAN=1       Vulkan renderer against NVK_SDK / GLSLANG_DIR
#                  (default $MC3_WORK/deps/{mesa-sdk/usr/local,glslang-switch})
#   LTO=1          link-time optimisation (LTO_JOBS, ~2 GB each)
#   NO_GAME=1      stage only the NRO (skip syncing the disc)
#
# Then: copy <SD>/switch/mcdub3-nx/ to the card, or `nxlink -s` the NRO, and start
# it from hbmenu with title takeover (hold R on a game) for full RAM.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../tools/env.sh"
mc3_check_lift_fresh

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
SD="${SD_ROOT:-$MC3_WORK/switch_sd}"
NRO_NAME=mcdub3
SUFFIX=""
ARGS=(-DMC3_VULKAN=OFF)
if [ "${VULKAN:-0}" = "1" ]; then
    SUFFIX="-vk"; NRO_NAME="$NRO_NAME-vulkan"
    NVK_SDK="${NVK_SDK:-$MC3_WORK/deps/mesa-sdk/usr/local}"
    GLSLANG_DIR="${GLSLANG_DIR:-$MC3_WORK/deps/glslang-switch}"
    [ -d "$NVK_SDK" ] || mc3_fail "VULKAN=1 needs mesa-switch NVK in $NVK_SDK"
    ARGS=(-DMC3_VULKAN=ON -DNVK_SDK="$NVK_SDK" -Dglslang_DIR="$GLSLANG_DIR/lib/cmake/glslang")
fi
if [ "${LTO:-0}" = "1" ]; then
    SUFFIX="$SUFFIX-lto"; NRO_NAME="$NRO_NAME-lto"
    ARGS+=(-DMC3_LTO=ON -DMC3_LTO_JOBS="${LTO_JOBS:-4}")
fi
BUILD="${BUILD_DIR:-$MC3_WORK/build-switch$SUFFIX}"
JOBS="${JOBS:-$(mc3_default_jobs)}"

[ -f "$DEVKITPRO/cmake/Switch.cmake" ] || mc3_fail "devkitPro not found at $DEVKITPRO (vm/setup-ubuntu.sh)"
[ -f "$DEVKITPRO/portlibs/switch/include/SDL2/SDL.h" ] || mc3_fail "switch-sdl2 missing"
[ -f "$MC3_GEN_DIR/recomp_funcs.h" ] || mc3_fail "no generated code in $MC3_GEN_DIR (tools/regen.sh)"

# The app's identity (name, author, version, icon): switch/app.env.
source "$MC3_REPO/switch/app.env"
cmake -S "$MC3_REPO" -B "$BUILD" -G Ninja \
    -DMC3_SWITCH_TITLE="$APP_NAME" -DMC3_SWITCH_AUTHOR="$APP_AUTHOR" \
    -DMC3_SWITCH_VERSION="$APP_VERSION" -DMC3_SWITCH_ICON="$MC3_REPO/$APP_ICON" \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DXBOXRECOMP_DIR="$XBOXRECOMP_DIR" -DMC3_GEN_DIR="$MC3_GEN_DIR" \
    -DMC3_APP="$MC3_APP" -DMC3_SD_DIR="$MC3_SD_DIR" \
    -DMC3_VERSION="$APP_VERSION" -DMC3_BUILD_ID="${MC3_BUILD_ID:-local}" "${ARGS[@]}" >/dev/null
ninja -C "$BUILD" -j"$JOBS"

DEST="$SD/switch/$MC3_SD_DIR"
mkdir -p "$DEST"
cp "$BUILD/mcdub3.nro" "$DEST/$NRO_NAME.nro"
# Keep the ELF of every staged NRO, by hash: crash offsets in a console log
# only resolve against the exact build that wrote it.
mkdir -p "$MC3_WORK/elf"
ELF_KEEP="$MC3_WORK/elf/$NRO_NAME-$(md5sum "$DEST/$NRO_NAME.nro" | cut -c1-8).elf"
cp "$BUILD/mcdub3.elf" "$ELF_KEEP"
# Release output (CI, .github/workflows/switch-build.yml): the NRO named
# for its edition, as players pick it (mcdub3-nx-usa.nro, later -pal-..),
# its ELF for crash offsets, and what was built.
if [ -n "${RELEASE_DIR:-}" ]; then
    EDITION=$(sed -n 's/^#define MC3_EDITION *"\(.*\)"/\1/p' "$MC3_REPO/config/xbe_facts.h")
    [ -n "$EDITION" ] || mc3_fail "config/xbe_facts.h has no MC3_EDITION: make lift"
    mkdir -p "$RELEASE_DIR"
    cp "$DEST/$NRO_NAME.nro" "$RELEASE_DIR/mcdub3-nx-$EDITION.nro"
    cp "$BUILD/mcdub3.elf" "$RELEASE_DIR/mcdub3-nx-$EDITION.elf"
    {
        echo "version=$APP_VERSION"
        echo "edition=$EDITION"
        echo "build=${MC3_BUILD_ID:-local}"
        echo "commit=${GITHUB_SHA:-$(git -C "$MC3_REPO" rev-parse HEAD 2>/dev/null || echo unknown)}"
        echo "nro_sha256=$(sha256sum "$RELEASE_DIR/mcdub3-nx-$EDITION.nro" | cut -d' ' -f1)"
    } > "$RELEASE_DIR/mcdub3-nx-$EDITION.buildinfo"
    echo "  release: $RELEASE_DIR/mcdub3-nx-$EDITION.nro (version $APP_VERSION)"
fi
if [ "${NO_GAME:-0}" != "1" ]; then
    mc3_need_game
    # The disc is gigabytes: hard-link it from $MC3_GAME_DIR instead of
    # copying (same disk, no extra space); only changed files are copied.
    rsync -a --size-only --link-dest="$MC3_GAME_DIR/" \
        "$MC3_GAME_DIR/" "$DEST/game/" --exclude '*_analysis.json'
fi
# The HD texture pack (hd/textures beside game/, see nv2a_gl/hd_pack.c):
# HD=1 stages it next to the game, otherwise a pack staged by an earlier
# HD=1 build is removed, so the staged folder matches the choice. Only
# textures/ goes: hd/dump is raw material for tools/hd_upscale.sh.
if [ "${HD:-0}" = "1" ]; then
    [ -d "$MC3_REPO/hd/textures" ] || mc3_fail "HD=1 but no hd/textures: make hd-dump, then make hd-upscale"
    mkdir -p "$DEST/hd/textures"
    rsync -a --delete "$MC3_REPO/hd/textures/" "$DEST/hd/textures/"
    echo "  HD pack: $(find "$DEST/hd/textures" -name '*.png' | wc -l) textures staged ($(du -sh "$DEST/hd" | cut -f1))"
else
    rm -rf "$DEST/hd"
fi
# Runtime switches for the console, staged with the NRO: your own
# switch/<app>_env.txt (gitignored) if there is one, else the documented
# template with every setting commented out (switch/<app>_env.example.txt).
# The template also goes along as <app>_env.example.txt, the reference.
if [ -f "$MC3_REPO/switch/${MC3_APP}_env.txt" ]; then
    cp "$MC3_REPO/switch/${MC3_APP}_env.txt" "$DEST/${MC3_APP}_env.txt"
    echo "  settings: switch/${MC3_APP}_env.txt staged"
elif [ -f "$MC3_REPO/switch/${MC3_APP}_env.example.txt" ]; then
    cp "$MC3_REPO/switch/${MC3_APP}_env.example.txt" "$DEST/${MC3_APP}_env.txt"
    echo "  settings: the template staged (all defaults; copy it to switch/${MC3_APP}_env.txt to keep your own)"
fi
[ -f "$MC3_REPO/switch/${MC3_APP}_env.example.txt" ] \
    && cp "$MC3_REPO/switch/${MC3_APP}_env.example.txt" "$DEST/${MC3_APP}_env.example.txt"
echo
echo "staged $DEST"
echo "  $NRO_NAME.nro $(stat -c %s "$DEST/$NRO_NAME.nro") bytes, ELF kept as $ELF_KEEP"
echo "  send it:  nxlink -s $DEST/$NRO_NAME.nro   (hbmenu open, Y = netloader)"
