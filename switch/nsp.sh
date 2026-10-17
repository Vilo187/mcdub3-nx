#!/usr/bin/env bash
# The NSP forwarder: a home-menu app that starts the NRO from the SD card as
# an application (full RAM), instead of hbmenu's title takeover.
#
#   make nsp                          (runs this in devkitPro's container)
#   KEYS=<path>                       default keys/prod.keys (never committed)
#
# Everything comes from switch/app.env: name, author, version, title ID, icon,
# the NRO's path on the card, and the pinned sources of the two tools:
#   nx-hbloader   devkitPro's own NRO loader (what hbmenu runs), built with
#                 APP_NRO as its default NRO and APP_TITLE_ID in its NPDM:
#                 that is the forwarder (exefs/main, exefs/main.npdm)
#   hacBrewPack   packs exefs + control (NACP, icon) into an NSP, encrypted
#                 with the console's keys
# Both are built from source in work/tools (once per pin). Output:
# work/nsp/mcdub3-<version>.nsp. Install it with your installer of choice;
# the NRO itself stays at APP_NRO (make switch, then copy to the card).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../tools/env.sh"
source "$MC3_REPO/switch/app.env"

KEYS="${KEYS:-$MC3_REPO/keys/prod.keys}"
[ -f "$KEYS" ] || mc3_fail "no keys at $KEYS (your console's prod.keys; never committed)"
[ -n "${DEVKITPRO:-}" ] && [ -x "$DEVKITPRO/tools/bin/nacptool" ] \
    || mc3_fail "devkitPro tools not found: run this through make nsp (container)"
TOOLS="$MC3_WORK/tools"
STAGE="$MC3_WORK/nsp-stage"
OUT="$MC3_WORK/nsp"
mkdir -p "$TOOLS" "$OUT"

# A source tree at a pinned commit (cloned once, then only checked out).
fetch() {
    local repo=$1 ref=$2 dir=$3
    [ -d "$dir/.git" ] || git clone --quiet "$repo" "$dir"
    if ! git -C "$dir" cat-file -e "$ref^{commit}" 2>/dev/null; then
        git -C "$dir" fetch --quiet origin
    fi
    git -C "$dir" checkout --quiet --force "$ref"
    git -C "$dir" clean --quiet -fdx
}

echo "== hacBrewPack ${HACBREWPACK_REF:0:12}"
HBP_DIR="$TOOLS/hacbrewpack"
if [ ! -x "$HBP_DIR/hacbrewpack" ] || [ "$(git -C "$HBP_DIR" rev-parse HEAD 2>/dev/null)" != "$HACBREWPACK_REF" ]; then
    fetch "$HACBREWPACK_REPO" "$HACBREWPACK_REF" "$HBP_DIR"
    cp "$HBP_DIR/config.mk.template" "$HBP_DIR/config.mk"
    make -C "$HBP_DIR" -j"$(nproc)" >/dev/null
fi

echo "== forwarder: nx-hbloader ${HBLOADER_REF:0:12} -> $APP_NRO, title $APP_TITLE_ID"
HBL_DIR="$TOOLS/nx-hbloader"
fetch "$HBLOADER_REPO" "$HBLOADER_REF" "$HBL_DIR"
grep -q '^#define DEFAULT_NRO ' "$HBL_DIR/source/main.c" \
    || mc3_fail "nx-hbloader no longer has DEFAULT_NRO: check the pin"
sed -i "s|^#define DEFAULT_NRO .*|#define DEFAULT_NRO \"$APP_NRO\"|" "$HBL_DIR/source/main.c"
python3 - "$HBL_DIR/hbl.json" "$APP_TITLE_ID" "$APP_NAME" <<'PY'
import json, sys
path, tid, name = sys.argv[1], "0x" + sys.argv[2], sys.argv[3]
j = json.load(open(path))
j["title_id"] = j["title_id_range_min"] = j["title_id_range_max"] = tid
j["name"] = "mcdub3"
json.dump(j, open(path, "w"), indent=4)
PY
make -C "$HBL_DIR" -j"$(nproc)" >/dev/null

echo "== control: NACP and icon"
rm -rf "$STAGE"
mkdir -p "$STAGE/exefs" "$STAGE/control"
cp "$HBL_DIR/hbl.nso" "$STAGE/exefs/main"
cp "$HBL_DIR/hbl.npdm" "$STAGE/exefs/main.npdm"
"$DEVKITPRO/tools/bin/nacptool" --create "$APP_NAME" "$APP_AUTHOR" "$APP_VERSION" \
    "$STAGE/control/control.nacp" --titleid="0x$APP_TITLE_ID" >/dev/null
if [ -f "$MC3_REPO/$APP_ICON" ]; then
    # The home menu wants a baseline 256x256 JPEG; some EXIF fields leave the
    # install stuck (hacBrewPack's README). Checked, not converted.
    python3 - "$MC3_REPO/$APP_ICON" <<'PY'
import struct, sys
d = open(sys.argv[1], "rb").read()
if d[:2] != b"\xff\xd8":
    sys.exit(f"{sys.argv[1]}: not a JPEG")
i, size = 2, None
while i + 4 <= len(d):
    marker, length = d[i + 1], struct.unpack(">H", d[i + 2:i + 4])[0]
    if marker == 0xE1 and d[i + 4:i + 8] == b"Exif":
        sys.exit(f"{sys.argv[1]}: has EXIF data; save it again without (e.g. via BMP)")
    if marker in (0xC0, 0xC1):
        size = struct.unpack(">HH", d[i + 5:i + 9])[::-1]
    if marker == 0xC2:
        sys.exit(f"{sys.argv[1]}: progressive JPEG; save it as baseline")
    if marker == 0xDA:
        break
    i += 2 + length
if size != (256, 256):
    sys.exit(f"{sys.argv[1]}: {size}, needs 256x256")
PY
    cp "$MC3_REPO/$APP_ICON" "$STAGE/control/icon_AmericanEnglish.dat"
else
    echo "   no icon at $APP_ICON: the home menu shows a placeholder"
fi

echo "== pack"
(cd "$STAGE" && "$HBP_DIR/hacbrewpack" -k "$KEYS" --titleid "$APP_TITLE_ID" \
    --exefsdir exefs --controldir control --noromfs --nologo \
    --nspdir nsp --ncadir nca --tempdir tmp --backupdir backup)
NSP="$OUT/mcdub3-$APP_VERSION.nsp"
mv "$STAGE/nsp/$APP_TITLE_ID.nsp" "$NSP"
rm -rf "$STAGE"
echo
echo "built $NSP ($(stat -c %s "$NSP") bytes)"
echo "  starts $APP_NRO as \"$APP_NAME\" $APP_VERSION, title $APP_TITLE_ID"
