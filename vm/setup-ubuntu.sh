#!/usr/bin/env bash
# One-time setup of the Ubuntu (ARM64) development VM: everything needed to
# lift, build and debug on Linux, and to cross-build the Switch NRO.
#
#   bash vm/setup-ubuntu.sh
#
# Safe to re-run. Needs sudo. See CONTRIBUTING.md for creating the VM.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../tools/env.sh"

echo "== 1/4 runtime build deps (the toolkit's own list: SDL2, GL/epoxy, OpenSSL, ...)"
bash "$XBOXRECOMP_DIR/tools/linux/install_deps.sh"

echo "== 2/4 tools: ninja, gdb, Vulkan (lavapipe for headless runs), rsync, python venv"
sudo apt-get install -y ninja-build gdb rsync python3-venv python3-pip \
    libvulkan-dev vulkan-tools mesa-vulkan-drivers glslang-dev glslang-tools \
    spirv-tools xvfb mesa-utils ffmpeg

echo "   perf for profiling (make profile), and permission to profile own processes"
sudo apt-get install -y "linux-tools-$(uname -r)" linux-tools-generic || \
    echo "   (no linux-tools for kernel $(uname -r); make profile will say so)"
echo 'kernel.perf_event_paranoid=1' | sudo tee /etc/sysctl.d/60-mc3-perf.conf >/dev/null
sudo sysctl -q -p /etc/sysctl.d/60-mc3-perf.conf || true

echo "== 3/4 Python venv with capstone for the lift pipeline ($MC3_WORK/venv)"
mkdir -p "$MC3_WORK"
# --copies: work/ is on the shared folder, which may not take symlinks.
[ -x "$MC3_WORK/venv/bin/python3" ] || python3 -m venv --copies "$MC3_WORK/venv"
"$MC3_WORK/venv/bin/pip" install -q --upgrade pip capstone pytest
# No shell profile changes: tools/env.sh puts work/venv on PATH for the scripts.

echo "== 4/4 devkitPro (devkitA64 + libnx + switch-sdl2 + switch-mesa)"
if [ ! -x /opt/devkitpro/pacman/bin/pacman ] && ! command -v dkp-pacman >/dev/null; then
    # devkitPro's own installer; it picks the package for this CPU.
    # ARM64 is supported; if this fails, see CONTRIBUTING.md.
    wget -q https://apt.devkitpro.org/install-devkitpro-pacman -O /tmp/install-devkitpro-pacman
    chmod +x /tmp/install-devkitpro-pacman
    sudo /tmp/install-devkitpro-pacman
fi
DKP_PACMAN=$(command -v dkp-pacman || echo /opt/devkitpro/pacman/bin/pacman)
sudo "$DKP_PACMAN" -Syu --noconfirm
sudo "$DKP_PACMAN" -S --needed --noconfirm switch-dev switch-sdl2 switch-mesa switch-glm
grep -q 'DEVKITPRO=' "$HOME/.bashrc" || {
    echo 'export DEVKITPRO=/opt/devkitpro' >> "$HOME/.bashrc"
    echo 'export PATH="$DEVKITPRO/tools/bin:$PATH"   # nxlink, elf2nro' >> "$HOME/.bashrc"
}

echo
echo "done. Open a new shell (or: source ~/.bashrc), then:"
echo "  tools/extract_disc.sh <iso>   ->  tools/first_lift.sh  ->  tools/build_linux.sh"
