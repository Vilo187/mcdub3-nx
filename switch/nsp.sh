#!/usr/bin/env bash
# The NSP forwarder: a home-menu app that starts the NRO from the SD card as
# an application (full RAM), instead of hbmenu's title takeover.
#
#   make nsp                          (runs this in devkitPro's container)
#   KEYS=<path>                       default keys/prod.keys (never committed)
#
# Built the way NTON (rlaphoenix/NTON) builds forwarders, which work on
# current firmware; NTON itself ships Windows binaries, so this redoes its
# steps with tools built here. The first version of this script ran plain
# nx-hbloader with the path compiled in and no romfs: the NSP installed but
# did not start the game, and the home menu showed no icon (2026-10-18).
#
#   exefs    NTON's forwarder program (nx-hbloader + Forwarder-Mod, built
#            for firmware 21+), title ID patched in by hacBrewPack
#   romfs    nextNroPath / nextArgv: the edition's NRO, where the forwarder looks
#   control  NACP from nacptool, then NTON's settings: no user selection,
#            screenshots and video capture on, no save data, one language;
#            the icon re-encoded as a bare baseline JPEG (else a '?')
#   logo     APP_LOGO_DIR if it holds both files, else NTON's
#
# Everything comes from switch/app.env. One NSP per edition (APP_EDITIONS,
# or EDITION=<one>): work/nsp/mcdub3-nx-<edition>-<version>.nsp, each
# starting sdmc:/switch/mcdub3-nx/mcdub3-nx-<edition>.nro under its own title
# ID, so editions install side by side.
#
# Two phases, because the container may have no DNS (the VM's Docker could
# not resolve github.com, 2026-10-18): `nsp.sh prepare` runs on the host
# (make nsp does it first) and fetches NTON and hacBrewPack at their pinned
# commits into work/tools and re-encodes the icon there (Pillow in
# work/venv); the build itself then runs offline in the container.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../tools/env.sh"
source "$MC3_REPO/switch/app.env"
TOOLS="$MC3_WORK/tools"
STAGE="$MC3_WORK/nsp-stage"
OUT="$MC3_WORK/nsp"
HBP_DIR="$TOOLS/hacbrewpack"
NTON_DIR="$TOOLS/nton"
ICON_OUT="$TOOLS/icon_AmericanEnglish.jpg"
mkdir -p "$TOOLS" "$OUT"

# A source tree at a pinned commit (cloned once, then only checked out).
fetch() {
    local repo=$1 ref=$2 dir=$3
    if [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null)" = "$ref" ]; then
        return 0                         # already there: no network needed
    fi
    [ -d "$dir/.git" ] || git clone --quiet "$repo" "$dir"
    if ! git -C "$dir" cat-file -e "$ref^{commit}" 2>/dev/null; then
        git -C "$dir" fetch --quiet origin
    fi
    git -C "$dir" checkout --quiet --force "$ref"
    git -C "$dir" clean --quiet -fdx
}

if [ "${1:-}" = prepare ]; then
    echo "== prepare (host): sources and icon"
    fetch "$HACBREWPACK_REPO" "$HACBREWPACK_REF" "$HBP_DIR"
    fetch "$NTON_REPO" "$NTON_REF" "$NTON_DIR"
    rm -f "$ICON_OUT"
    if [ -f "$MC3_REPO/$APP_ICON" ]; then
        # A bare baseline JPEG, 256x256, 4:2:0, nothing else in it: anything
        # more and the home menu shows a '?' or nothing (NTON does the same).
        mc3_python                       # sets MC3_PYTHON (work/venv or the system)
        PY="$MC3_PYTHON"
        "$PY" -c "import PIL" 2>/dev/null || "$PY" -m pip install --quiet pillow \
            || mc3_fail "cannot install Pillow for $PY (needed to clean the icon)"
        "$PY" - "$MC3_REPO/$APP_ICON" "$ICON_OUT" <<'PY'
import sys
from PIL import Image
src, dst = sys.argv[1:3]
im = Image.open(src).convert("RGB")
if im.size != (256, 256):
    im = im.resize((256, 256), Image.LANCZOS)
clean = Image.new("RGB", im.size)
clean.putdata(list(im.getdata()))
clean.save(dst, format="JPEG", quality=98, subsampling=2)
PY
        echo "   icon: $APP_ICON -> ${ICON_OUT#$MC3_REPO/}"
    fi
    exit 0
fi

KEYS="${KEYS:-$MC3_REPO/keys/prod.keys}"
[ -f "$KEYS" ] || mc3_fail "no keys at $KEYS (your console's prod.keys; never committed)"
[ -n "${DEVKITPRO:-}" ] && [ -x "$DEVKITPRO/tools/bin/nacptool" ] \
    || mc3_fail "devkitPro tools not found: run this through make nsp (container)"

echo "== hacBrewPack ${HACBREWPACK_REF:0:12}"
if [ ! -x "$HBP_DIR/hacbrewpack" ] || [ "$(git -C "$HBP_DIR" rev-parse HEAD 2>/dev/null)" != "$HACBREWPACK_REF" ]; then
    fetch "$HACBREWPACK_REPO" "$HACBREWPACK_REF" "$HBP_DIR"
    cp "$HBP_DIR/config.mk.template" "$HBP_DIR/config.mk"
    make -C "$HBP_DIR" -j"$(nproc)" >/dev/null
fi

echo "== NTON ${NTON_REF:0:12} (forwarder program, defaults)"
fetch "$NTON_REPO" "$NTON_REF" "$NTON_DIR"
NTON_ASSETS="$NTON_DIR/nton/assets"
[ -f "$NTON_ASSETS/exefs/main" ] && [ -f "$NTON_ASSETS/exefs/main.npdm" ] \
    || mc3_fail "NTON has no nton/assets/exefs at this commit: check the pin"

# One NSP: a forwarder to mcdub3-nx-<edition>.nro with that edition's title
# ID and name (switch/app.env).
build_nsp() {
    local EDITION=$1 TITLE_ID NAME NRO key
    key=${EDITION//-/_}                  # pal-es -> APP_TITLE_ID_pal_es
    [[ "$key" =~ ^[A-Za-z0-9_]+$ ]] || mc3_fail "edition '$EDITION': letters, digits, - and _ only"
    local tid_var="APP_TITLE_ID_$key" name_var="APP_NAME_$key"
    TITLE_ID="${!tid_var:-}"
    NAME="${!name_var:-$APP_NAME}"
    NRO="$APP_NRO_DIR/mcdub3-nx-$EDITION.nro"
    [[ "$TITLE_ID" =~ ^01[0-9a-f]{11}000$ ]] \
        || mc3_fail "switch/app.env: APP_TITLE_ID_$EDITION missing or not 01xxxxxxxxxxx000"
    echo
    echo "==== $EDITION: $NAME, title $TITLE_ID"
    rm -rf "$STAGE"
    mkdir -p "$STAGE/exefs" "$STAGE/romfs" "$STAGE/control" "$STAGE/logo"
    cp "$NTON_ASSETS/exefs/main" "$NTON_ASSETS/exefs/main.npdm" "$STAGE/exefs/"

    echo "== romfs: forward to $NRO"
    printf '%s' "$NRO" > "$STAGE/romfs/nextNroPath"
    printf '%s' "$NRO" > "$STAGE/romfs/nextArgv"

    echo "== control: NACP and icon"
    "$DEVKITPRO/tools/bin/nacptool" --create "$NAME" "$APP_AUTHOR" "$APP_VERSION" \
        "$STAGE/control/control.nacp" --titleid="0x$TITLE_ID" >/dev/null
    python3 - "$STAGE/control/control.nacp" "$NAME" "$APP_AUTHOR" "$APP_VERSION" <<'PY'
import sys
path, name, publisher, version = sys.argv[1:5]
d = bytearray(open(path, "rb").read())
# One name and publisher (AmericanEnglish), as NTON stores them.
d[0x0:0x3000] = b"\0" * 0x3000
d[0x0:0x200] = name.encode()[:0x1FF].ljust(0x200, b"\0")
d[0x200:0x300] = publisher.encode()[:0xFF].ljust(0x100, b"\0")
d[0x3060:0x3070] = version.encode()[:0xF].ljust(0x10, b"\0")
d[0x3025] = 0x00                     # StartupUserAccount: none
d[0x3034] = 0x00                     # Screenshot: allowed
d[0x3035] = 0x02                     # VideoCapture: enabled
for off in (0x3080, 0x3088, 0x3090, 0x3098, 0x30A0, 0x3148, 0x3150, 0x3158,
            0x3160, 0x3168, 0x3170, 0x3178, 0x3180):
    d[off:off + 8] = b"\0" * 8       # no save data: the game keeps its own
d[0x3188:0x318A] = b"\0\0"           # cache storage index max
open(path, "wb").write(d)
PY
    if [ -f "$ICON_OUT" ]; then
        cp "$ICON_OUT" "$STAGE/control/icon_AmericanEnglish.dat"
    elif [ -f "$MC3_REPO/$APP_ICON" ]; then
        mc3_fail "icon not prepared: run 'bash switch/nsp.sh prepare' on the host first (make nsp does)"
    else
        echo "   no icon at $APP_ICON: the home menu shows a placeholder"
    fi

    LOGO_DIR="$MC3_REPO/${APP_LOGO_DIR:-assets/logo}"
    if [ -f "$LOGO_DIR/NintendoLogo.png" ] && [ -f "$LOGO_DIR/StartupMovie.gif" ]; then
        echo "== start-up logo: ${APP_LOGO_DIR:-assets/logo}"
        cp "$LOGO_DIR/NintendoLogo.png" "$LOGO_DIR/StartupMovie.gif" "$STAGE/logo/"
    else
        echo "== start-up logo: NTON's"
        cp "$NTON_ASSETS/logo/NintendoLogo.png" "$NTON_ASSETS/logo/StartupMovie.gif" "$STAGE/logo/"
    fi

    echo "== pack"
    (cd "$STAGE" && "$HBP_DIR/hacbrewpack" -k "$KEYS" --titleid "$TITLE_ID" \
        --exefsdir exefs --romfsdir romfs --controldir control --logodir logo \
        --nspdir nsp --ncadir nca --tempdir tmp --backupdir backup)
    NSP="$OUT/mcdub3-nx-$EDITION-$APP_VERSION.nsp"
    mv "$STAGE/nsp/$TITLE_ID.nsp" "$NSP"
    rm -rf "$STAGE"
    echo
    echo "built $NSP ($(stat -c %s "$NSP") bytes)"
    echo "  starts $NRO as \"$NAME\" $APP_VERSION, title $TITLE_ID"
}

# Every edition in APP_EDITIONS, or the ones given (EDITION=usa make nsp).
for ed in ${EDITION:-$APP_EDITIONS}; do
    build_nsp "$ed"
done
