#!/usr/bin/env bash
# Send the NRO to the console and stream its log back (make send).
#
#   switch/send.sh [nro]          default: the staged mcdub3.nro (VULKAN=1: -vulkan)
#   SWITCH_IP=192.168.1.50 ...    address the console directly instead of
#                                 finding it by broadcast
#   NATIVE=1 ...                  use a host nxlink instead of the container
#
# On the Switch: open the homebrew menu and press Y (netloader) first.
#
# nxlink is a devkitPro tool, so like the build it runs from the pinned image
# (switch/devkitpro-image, ADR 0004). --network host because nxlink finds the
# console by UDP broadcast and, with -s, waits for the game to connect back
# and stream its stdout/stderr: both need the host's own network, not
# Docker's. That also means the VM must sit on the console's network
# (Parallels: bridged, not shared/NAT), or the console cannot connect back.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../tools/env.sh"

NRO="${1:-$MC3_WORK/switch_sd/switch/$MC3_SD_DIR/$MC3_APP$([ "${VULKAN:-0}" = 1 ] && echo -vulkan).nro}"
[ -f "$NRO" ] || mc3_fail "no $NRO: make switch first"
ARGS=(-s)
[ -n "${SWITCH_IP:-}" ] && ARGS+=(-a "$SWITCH_IP")

if [ "${NATIVE:-0}" = 1 ]; then
    exec nxlink "${ARGS[@]}" "$NRO"
fi

IMAGE=$(grep -v '^#' "$MC3_REPO/switch/devkitpro-image" | head -1)
command -v docker >/dev/null || mc3_fail "no docker: sudo apt install docker.io (or make setup), or NATIVE=1 with nxlink installed"
DOCKER=(docker)
docker info >/dev/null 2>&1 || DOCKER=(sudo docker)

# -i: Ctrl+C reaches nxlink, which then closes the log stream.
exec "${DOCKER[@]}" run --rm -i --network host \
    -u "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$MC3_REPO:$MC3_REPO" -w "$MC3_REPO" \
    "$IMAGE" nxlink "${ARGS[@]}" "$NRO"
