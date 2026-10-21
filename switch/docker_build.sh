#!/usr/bin/env bash
# The Switch build, inside devkitPro's own container image.
#
#   switch/docker_build.sh [script]   (make switch / make nsp; NATIVE=1 skips this)
#                                     script: default switch/build.sh
#
# Why a container: one toolchain everywhere -- this VM, another developer's
# machine, GitHub Actions -- pinned by digest in switch/devkitpro-image, with
# nothing installed or patched on the host. devkitPro publishes the image for
# arm64 and amd64; it has devkitA64, libnx, the Switch portlibs and the build
# tools switch/build.sh uses (cmake, ninja, python3, rsync). Also, devkitpro.org
# answers 403 from some networks (2026-10-16), Docker Hub does not.
#
# The repository is mounted at the same path, so paths in logs and in
# work/build-switch are the host's; the build runs as the calling user, so
# nothing in work/ ends up owned by root. Environment as for switch/build.sh.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../tools/env.sh"

IMAGE=$(grep -v '^#' "$MC3_REPO/switch/devkitpro-image" | head -1)
command -v docker >/dev/null || mc3_fail "no docker: sudo apt install docker.io (or make setup), or NATIVE=1 with devkitPro installed"
DOCKER=(docker)
docker info >/dev/null 2>&1 || DOCKER=(sudo docker)   # not in the docker group yet

ENVS=()
SCRIPT="${1:-switch/build.sh}"
for v in MC3_WORK MC3_GAME_DIR MC3_GEN_DIR MC3_APP MC3_SD_DIR SD_ROOT BUILD_DIR JOBS VULKAN LTO LTO_JOBS NO_GAME KEYS HD; do
    [ -n "${!v:-}" ] && ENVS+=(-e "$v=${!v}")
done

echo "$SCRIPT in $IMAGE"
exec "${DOCKER[@]}" run --rm \
    -u "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$MC3_REPO:$MC3_REPO" -w "$MC3_REPO" \
    "${ENVS[@]}" \
    "$IMAGE" bash "$SCRIPT"
