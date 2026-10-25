#!/usr/bin/env bash
# The Switch build in devkitPro's pinned image, without Docker or root.
#
#   switch/bwrap_build.sh [script]   (make switch BWRAP=1; default switch/build.sh)
#
# For hosts with neither Docker nor sudo: the image named in
# switch/devkitpro-image is unpacked once into $MC3_WORK/toolchain/rootfs
# (registry download, layer digests checked) and the build runs in it under
# bubblewrap, an unprivileged container. Same toolchain, same paths: the repo
# and work/ are mounted where they are on the host.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../tools/env.sh"

command -v bwrap >/dev/null || mc3_fail "no bwrap: install bubblewrap, or use Docker (make switch)"
ROOT="$MC3_WORK/toolchain/rootfs"
IMAGE=$(grep -v '^#' "$MC3_REPO/switch/devkitpro-image" | head -1)

if [ ! -x "$ROOT/opt/devkitpro/devkitA64/bin/aarch64-none-elf-gcc" ]; then
    REPO="${IMAGE%%:*}"
    DIGEST="${IMAGE##*@}"
    DL="$MC3_WORK/toolchain/layers"
    ACCEPT='Accept: application/vnd.oci.image.index.v1+json, application/vnd.docker.distribution.manifest.list.v2+json, application/vnd.docker.distribution.manifest.v2+json, application/vnd.oci.image.manifest.v1+json'
    echo "unpacking $IMAGE into $ROOT (once)"
    mkdir -p "$DL" "$ROOT"
    TOKEN=$(curl -fsS "https://auth.docker.io/token?service=registry.docker.io&scope=repository:$REPO:pull" \
            | python3 -c 'import sys, json; print(json.load(sys.stdin)["token"])')
    reg() { curl -fsSL -H "Authorization: Bearer $TOKEN" -H "$ACCEPT" "https://registry-1.docker.io/v2/$REPO/$1"; }
    MANIFEST=$(reg "manifests/$DIGEST" | python3 -c '
import sys, json
d = json.load(sys.stdin)
print(next(m["digest"] for m in d["manifests"] if m["platform"]["architecture"] == "amd64"))')
    for layer in $(reg "manifests/$MANIFEST" | python3 -c '
import sys, json
print("\n".join(l["digest"] for l in json.load(sys.stdin)["layers"]))'); do
        reg "blobs/$layer" > "$DL/${layer#sha256:}"
        echo "${layer#sha256:}  $DL/${layer#sha256:}" | sha256sum -c --quiet
        tar -xzf "$DL/${layer#sha256:}" -C "$ROOT" --exclude='dev/*' 2>/dev/null || true
        rm -f "$DL/${layer#sha256:}"
    done
fi

SCRIPT="${1:-switch/build.sh}"
BINDS=(--bind "$MC3_REPO" "$MC3_REPO")
for d in "$MC3_WORK" "$MC3_GAME_DIR" "${RELEASE_DIR:-}" "$([ -n "${KEYS:-}" ] && dirname "$KEYS")"; do
    [ -n "$d" ] && [ -e "$d" ] || continue
    d=$(cd "$d" && pwd -P)
    BINDS+=(--bind "$d" "$d")
done
# The disc folder may be a link out of the repo (game -> elsewhere).
[ -L "$MC3_REPO/game" ] && BINDS+=(--bind "$(readlink -f "$MC3_REPO/game")" "$(readlink -f "$MC3_REPO/game")")

echo "$SCRIPT in $IMAGE (bubblewrap)"
exec bwrap --bind "$ROOT" / --dev /dev --proc /proc --tmpfs /tmp \
    --ro-bind /etc/resolv.conf /etc/resolv.conf \
    "${BINDS[@]}" --chdir "$MC3_REPO" \
    --setenv HOME /tmp --setenv DEVKITPRO /opt/devkitpro \
    --setenv PATH /opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:/usr/local/bin:/usr/bin:/bin \
    bash "$SCRIPT"
