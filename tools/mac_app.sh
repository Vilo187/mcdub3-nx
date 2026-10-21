#!/usr/bin/env bash
# Package the macOS build as an app: work/mac_app/Midnight Club 3.app
#
#   make mac-app [SCALE=1.5] [SETTINGS="KEY=VALUE ..."] [HD=1]
#
# The app is self-contained (its Homebrew libraries are copied in with
# dylibbundler) and keeps everything else where a Mac app keeps it, laid out
# like the Switch's SD folder:
#
#   ~/Library/Application Support/mcdub3/
#   ├── game/             the extracted disc (put it here; the app says so)
#   ├── hd/textures/      optional texture pack
#   ├── save/             saves
#   ├── mcdub3_env.txt    your settings: KEY=VALUE per line, '#' comments
#   ├── mcdub3_log.txt    the last run's log
#   └── progcache.bin     the shader cache
#
# Settings, later wins: the defaults baked in at build time (SCALE, SETTINGS:
# Contents/Resources/defaults_env.txt), then your mcdub3_env.txt. Every
# RECOMP_* / MC3_* switch works; the first launch writes mcdub3_env.txt
# from the documented template (switch/mcdub3_env.example.txt), so the full
# list sits beside your settings.
#   SCALE=<f>     rendering resolution, e.g. 1.5 (1280x720) or 2
#   SETTINGS=...  any other defaults, space-separated KEY=VALUE pairs
#   HD=1          the texture pack from hd/textures inside the app (else the
#                 app reads hd/ from Application Support, if there is one)
#   GAME=<dir>    on first launch, link this disc folder in instead of
#                 asking (default: none; the app asks)
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
mc3_mac_native "$0" "$@"
[ "$MC3_HOST" = mac ] || mc3_fail "make mac-app runs on macOS"

NAME="Midnight Club 3"
BIN="$MC3_WORK/build-mac/mcdub3"
OUT="$MC3_WORK/mac_app"
APP="$OUT/$NAME.app"
[ -x "$BIN" ] || mc3_fail "no $BIN: make build first"
command -v dylibbundler >/dev/null || mc3_fail "no dylibbundler: brew install dylibbundler"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/libs"
cp "$BIN" "$APP/Contents/MacOS/mcdub3"

# The Homebrew libraries (SDL2, FFmpeg and theirs) into Contents/libs, the
# binary pointed at them: the app runs without Homebrew.
dylibbundler -od -b -x "$APP/Contents/MacOS/mcdub3" -d "$APP/Contents/libs" \
    -p @executable_path/../libs/ >/dev/null
echo "  libraries: $(ls "$APP/Contents/libs" | wc -l | tr -d ' ') bundled"

# Defaults baked in at build time.
{
    echo "# Defaults built into the app (make mac-app). Your settings in"
    echo "# ~/Library/Application Support/mcdub3/mcdub3_env.txt override these."
    [ -n "${SCALE:-}" ] && echo "RECOMP_GL_SCALE=$SCALE"
    for kv in ${SETTINGS:-}; do echo "$kv"; done
    [ "${HD:-0}" = "1" ] && echo "RECOMP_GL_HD_DIR=@RESOURCES@/hd"
    true
} > "$APP/Contents/Resources/defaults_env.txt"
cp "$MC3_REPO/switch/mcdub3_env.example.txt" "$APP/Contents/Resources/env_template.txt"
if [ "${HD:-0}" = "1" ]; then
    [ -d "$MC3_REPO/hd/textures" ] || mc3_fail "HD=1 but no hd/textures: make hd-dump, then make hd-upscale"
    mkdir -p "$APP/Contents/Resources/hd"
    cp -R "$MC3_REPO/hd/textures" "$APP/Contents/Resources/hd/"
    echo "  HD pack: $(ls "$APP/Contents/Resources/hd/textures" | wc -l | tr -d ' ') textures inside the app"
fi

# The launcher: settings, folders, then the game.
cat > "$APP/Contents/MacOS/launch" <<'LAUNCH'
#!/bin/bash
# Midnight Club 3 launcher (tools/mac_app.sh): settings, folders, game.
HERE="$(cd "$(dirname "$0")" && pwd)"
RES="$HERE/../Resources"
SUP="$HOME/Library/Application Support/mcdub3"
mkdir -p "$SUP/save"
[ -f "$SUP/mcdub3_env.txt" ] || cp "$RES/env_template.txt" "$SUP/mcdub3_env.txt"
if [ ! -f "$SUP/game/default.xbe" ] && [ -n "@GAME@" ] && [ -f "@GAME@/default.xbe" ]; then
    rm -rf "$SUP/game"; ln -s "@GAME@" "$SUP/game"
fi
if [ ! -f "$SUP/game/default.xbe" ]; then
    mkdir -p "$SUP/game"
    open "$SUP/game"
    osascript -e 'display dialog "Put the extracted Midnight Club 3 disc (default.xbe and its files, not the ISO) into the folder that just opened, then start the game again." with title "Midnight Club 3" buttons {"OK"} default button 1 with icon caution' >/dev/null 2>&1
    exit 1
fi
apply() {   # KEY=VALUE lines; '#' comments; later files win
    local line key val
    while IFS= read -r line || [ -n "$line" ]; do
        line="${line%%#*}"; line="${line%$'\r'}"
        line="$(echo "$line" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"
        [ -z "$line" ] && continue
        key="${line%%=*}"; val="${line#*=}"
        [ "$key" = "$line" ] && continue
        val="${val//@RESOURCES@/$RES}"
        export "$key=$val"
    done < "$1"
}
apply "$RES/defaults_env.txt"
apply "$SUP/mcdub3_env.txt"
export MC3_GAME_DIR="${MC3_GAME_DIR:-$SUP/game}"
export MC3_SAVE_DIR="${MC3_SAVE_DIR:-$SUP/save}"
cd "$SUP"
exec "$HERE/mcdub3" > "$SUP/mcdub3_log.txt" 2>&1
LAUNCH
sed -i '' "s|@GAME@|${GAME:-}|g" "$APP/Contents/MacOS/launch"
chmod +x "$APP/Contents/MacOS/launch"

# Icon from assets/cover-art.jpg, if there is one (personal, not in git):
# rounded like a stock Mac app (tools/mac_icon.py, needs Pillow), square
# when no Python has Pillow.
ICON_KEY=""
if [ -f "$MC3_REPO/assets/cover-art.jpg" ]; then
    SET="$OUT/icon.iconset"
    SRC="$OUT/icon_1024.png"
    rm -rf "$SET"; mkdir -p "$SET"
    PY=""
    for c in "$MC3_WORK/macvenv/bin/python3" "$MC3_WORK/venv/bin/python3" python3; do
        if "$c" -c 'import PIL' >/dev/null 2>&1; then PY="$c"; break; fi
    done
    if [ -n "$PY" ] && "$PY" "$MC3_REPO/tools/mac_icon.py" "$MC3_REPO/assets/cover-art.jpg" "$SRC"; then
        echo "  icon: rounded (tools/mac_icon.py)"
    else
        sips -s format png -z 1024 1024 "$MC3_REPO/assets/cover-art.jpg" --out "$SRC" >/dev/null
        echo "  icon: square (no Python with Pillow: python3 -m pip install pillow)"
    fi
    for s in 16 32 128 256 512; do
        sips -z $s $s "$SRC" --out "$SET/icon_${s}x${s}.png" >/dev/null
        sips -z $((s * 2)) $((s * 2)) "$SRC" --out "$SET/icon_${s}x${s}@2x.png" >/dev/null
    done
    iconutil -c icns "$SET" -o "$APP/Contents/Resources/icon.icns" && ICON_KEY="<key>CFBundleIconFile</key><string>icon</string>"
    rm -rf "$SET" "$SRC"
fi

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>$NAME</string>
  <key>CFBundleDisplayName</key><string>$NAME</string>
  <key>CFBundleIdentifier</key><string>nl.mcdub3.recomp</string>
  <key>CFBundleExecutable</key><string>launch</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>0.1</string>
  <key>CFBundleVersion</key><string>$(git -C "$MC3_REPO" rev-parse --short HEAD 2>/dev/null || echo 0)</string>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.racing-games</string>
  <key>NSHighResolutionCapable</key><true/>
  $ICON_KEY
</dict>
</plist>
PLIST

# Ad-hoc signature (Apple Silicon runs nothing unsigned; libraries first).
codesign --force -s - "$APP/Contents/libs/"*.dylib >/dev/null 2>&1 || true
codesign --force --deep -s - "$APP" >/dev/null
echo "built $APP ($(du -sh "$APP" | cut -f1))"
echo "  defaults: $(grep -v '^#' "$APP/Contents/Resources/defaults_env.txt" | tr '\n' ' ')"
echo "  open it:  open \"$APP\"   (or drag it to /Applications)"
