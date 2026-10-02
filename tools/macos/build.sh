#!/usr/bin/env bash
# tools/macos/build.sh — build EvoMusicBox for macOS and package a self-contained .app bundle.
#
#   tools/macos/build.sh                 # configure + build + ctest + bundle + zip
#   tools/macos/build.sh --no-tests      # skip ctest
#   tools/macos/build.sh --no-tiktok     # soundboard + OSC only (EVOBOX_WITH_TIKTOK=OFF)
#   tools/macos/build.sh --clean         # wipe build-mac/ first
#
# Needs: Xcode command line tools (clang, otool, install_name_tool, codesign, iconutil, sips),
# cmake >= 3.20, ninja, and the Homebrew libraries the build links (brew install cmake ninja glfw
# ffmpeg protobuf). The resulting bundle vendors every non-system dylib it pulls in (FFmpeg,
# protobuf, abseil, brotli, zstd, libidn2, ...) into EvoMusicBox.app/Contents/Frameworks and
# rewrites their install names, so it runs on a clean Mac that has never seen Homebrew.
#
# Result: build-mac/dist/EvoMusicBox-<version>-macos-<arch>.zip
#   EvoMusicBox.app/
#   ├── Contents/Info.plist          CFBundleIdentifier, version, .liv document type, high-DPI
#   ├── Contents/MacOS/evobox        the executable (rpath -> ../Frameworks)
#   ├── Contents/Frameworks/*.dylib  every non-system library, install names @rpath-relative
#   ├── Contents/Resources/
#   │   ├── EvoMusicBox.icns         app icon (converted from musicbox.ico)
#   │   ├── assets/  fonts/  (tiktok-js/ with TikTok)
#   │   └── README.md README.zh-CN.md LICENSES.txt
# The app is ad-hoc code-signed (unsigned for distribution: Gatekeeper asks on first run — right
# click > Open, or `xattr -dr com.apple.quarantine EvoMusicBox.app`).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${BUILD_DIR:-$ROOT/build-mac}
RUN_TESTS=1
TIKTOK=ON
CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --no-tests) RUN_TESTS=0 ;;
        --no-tiktok) TIKTOK=OFF ;;
        --clean) CLEAN=1 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This script builds the macOS package and must run on macOS." >&2
    exit 1
fi

ARCH=$(uname -m)                                   # arm64 or x86_64
VERSION=$(sed -n 's/^set(EVOBOX_VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt" | head -1)
VERSION=${VERSION:-0.0.0}
APPNAME="EvoMusicBox"
PKG="$APPNAME-$VERSION-macos-$ARCH"

echo "==> EvoMusicBox $VERSION  macOS $ARCH  (TikTok=$TIKTOK)"

[[ "$CLEAN" == 1 ]] && rm -rf "$BUILD"

# ---- configure + build
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DEVOBOX_WITH_TIKTOK=$TIKTOK
ninja -C "$BUILD"

if [[ "$RUN_TESTS" == 1 ]]; then
    ctest --test-dir "$BUILD" --output-on-failure
fi

EXE="$BUILD/evobox"
[[ -x "$EXE" ]] || { echo "build produced no evobox executable" >&2; exit 1; }

# ---- assemble the .app skeleton
APP="$BUILD/$APPNAME.app"
rm -rf "$APP"
MACOSDIR="$APP/Contents/MacOS"
FWDIR="$APP/Contents/Frameworks"
RESDIR="$APP/Contents/Resources"
mkdir -p "$MACOSDIR" "$FWDIR" "$RESDIR"

cp "$EXE" "$MACOSDIR/evobox"
chmod +x "$MACOSDIR/evobox"

# resources: assets, fonts (same set Deploy.cmake stages next to the dev build), docs
cp -R "$ROOT/assets" "$RESDIR/assets"
mkdir -p "$RESDIR/fonts"
cp "$ROOT/third_party/web/src/regular/Phosphor.ttf" "$RESDIR/fonts/"
cp "$ROOT/third_party/web/src/fill/Phosphor-Fill.ttf" "$RESDIR/fonts/"
cp "$ROOT/third_party/imgui/misc/fonts/Roboto-Medium.ttf" "$RESDIR/fonts/"
for f in README.md README.zh-CN.md; do [[ -f "$ROOT/$f" ]] && cp "$ROOT/$f" "$RESDIR/"; done
[[ -f "$ROOT/windows/LICENSES.txt" ]] && cp "$ROOT/windows/LICENSES.txt" "$RESDIR/"
if [[ "$TIKTOK" == ON ]]; then
    cp -R "$ROOT/third_party/ttlive-cpp/js" "$RESDIR/tiktok-js"
fi

# ---- app icon: musicbox.ico -> EvoMusicBox.icns (best effort; skipped if conversion fails)
ICNS="$RESDIR/$APPNAME.icns"
ICON_OK=0
if command -v sips >/dev/null && command -v iconutil >/dev/null && [[ -f "$ROOT/musicbox.ico" ]]; then
    TMPICON=$(mktemp -d)
    if sips -s format png "$ROOT/musicbox.ico" --out "$TMPICON/base.png" >/dev/null 2>&1; then
        ICONSET="$TMPICON/$APPNAME.iconset"; mkdir -p "$ICONSET"
        for sz in 16 32 128 256 512; do
            sips -z $sz $sz "$TMPICON/base.png" --out "$ICONSET/icon_${sz}x${sz}.png" >/dev/null 2>&1 || true
            dbl=$((sz*2))
            sips -z $dbl $dbl "$TMPICON/base.png" --out "$ICONSET/icon_${sz}x${sz}@2x.png" >/dev/null 2>&1 || true
        done
        if iconutil -c icns "$ICONSET" -o "$ICNS" >/dev/null 2>&1; then ICON_OK=1; fi
    fi
    rm -rf "$TMPICON"
fi
[[ "$ICON_OK" == 1 ]] || echo "   (no .icns produced; bundle ships without a custom icon)"

# ---- Info.plist
ICON_LINE=""
[[ "$ICON_OK" == 1 ]] && ICON_LINE="    <key>CFBundleIconFile</key><string>$APPNAME</string>"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>$APPNAME</string>
    <key>CFBundleDisplayName</key><string>$APPNAME</string>
    <key>CFBundleIdentifier</key><string>com.evomusicbox.app</string>
    <key>CFBundleVersion</key><string>$VERSION</string>
    <key>CFBundleShortVersionString</key><string>$VERSION</string>
    <key>CFBundleExecutable</key><string>evobox</string>
    <key>CFBundlePackageType</key><string>APPL</string>
$ICON_LINE
    <key>LSMinimumSystemVersion</key><string>11.0</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSPrincipalClass</key><string>NSApplication</string>
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeName</key><string>EvoMusicBox show</string>
            <key>CFBundleTypeExtensions</key><array><string>liv</string></array>
            <key>CFBundleTypeRole</key><string>Editor</string>
        </dict>
    </array>
</dict>
</plist>
PLIST

# ---------------------------------------------------------------- dylib relocation
# Walk the executable's dependency graph; copy every non-system dylib into Contents/Frameworks and
# rewrite all install names to @rpath/<leaf>. System libraries (/usr/lib, /System, OpenGL and the
# other frameworks) stay as absolute references — they exist on every Mac.
is_system() { case "$1" in /usr/lib/*|/System/*|@rpath/*|@loader_path/*|@executable_path/*) return 0;; *) return 1;; esac; }

echo "==> vendoring dylibs into $APPNAME.app/Contents/Frameworks"
# Collect the transitive set of non-system dylibs (BFS over otool -L). macOS ships Bash 3.2, which
# has no associative arrays, so the work queue and "seen" set are kept in plain temp files keyed by
# the library leaf name (install names are unique per leaf here).
WORK=$(mktemp -d)
echo "$MACOSDIR/evobox" > "$WORK/queue"
: > "$WORK/seen"        # leaf names already copied
while [[ -s "$WORK/queue" ]]; do
    cur=$(head -1 "$WORK/queue"); sed -i '' '1d' "$WORK/queue"
    otool -L "$cur" | tail -n +2 | awk '{print $1}' | while IFS= read -r dep; do
        dep="${dep#"${dep%%[![:space:]]*}"}"              # ltrim
        dep="${dep%% (*}"                                 # strip " (compatibility ...)"
        [[ -z "$dep" ]] && continue
        is_system "$dep" && continue
        [[ -f "$dep" ]] || continue
        leaf=$(basename "$dep")
        grep -qxF "$leaf" "$WORK/seen" && continue
        echo "$leaf" >> "$WORK/seen"
        cp "$dep" "$FWDIR/$leaf"
        chmod u+w "$FWDIR/$leaf"
        echo "$FWDIR/$leaf" >> "$WORK/queue"              # recurse into the copied lib's own deps
    done
done
rm -rf "$WORK"
echo "   $(ls -1 "$FWDIR" | wc -l | tr -d ' ') libraries bundled"

# Rewrite install names: each vendored lib's own id and every inter-lib reference becomes @rpath/<leaf>.
for lib in "$FWDIR"/*.dylib; do
    [[ -e "$lib" ]] || continue
    leaf=$(basename "$lib")
    install_name_tool -id "@rpath/$leaf" "$lib" 2>/dev/null || true
    while IFS= read -r dep; do
        dep="${dep#"${dep%%[![:space:]]*}"}"; dep="${dep%% (*}"
        [[ -z "$dep" ]] && continue
        is_system "$dep" && continue
        install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$lib" 2>/dev/null || true
    done < <(otool -L "$lib" | tail -n +2 | awk '{print $1}')
done

# Rewrite the executable's references and point its rpath at the bundled frameworks.
while IFS= read -r dep; do
    dep="${dep#"${dep%%[![:space:]]*}"}"; dep="${dep%% (*}"
    [[ -z "$dep" ]] && continue
    is_system "$dep" && continue
    install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$MACOSDIR/evobox" 2>/dev/null || true
done < <(otool -L "$MACOSDIR/evobox" | tail -n +2 | awk '{print $1}')
install_name_tool -add_rpath "@executable_path/../Frameworks" "$MACOSDIR/evobox" 2>/dev/null || true

# install_name_tool rewrites can clear the mode bits; make sure the executable stays runnable.
chmod 0755 "$MACOSDIR/evobox"

# ---- ad-hoc sign (bundle is unsigned for distribution; this just satisfies the arm64 loader,
# which refuses to run a dylib whose contents changed after signing). Sign the vendored libraries
# first, then the main executable, then seal the bundle.
find "$FWDIR" -name '*.dylib' -exec codesign --force -s - --timestamp=none {} \; 2>/dev/null || true
codesign --force -s - --timestamp=none "$MACOSDIR/evobox" 2>/dev/null || true
codesign --force -s - --timestamp=none "$APP" 2>/dev/null || true

# ---- sanity: no non-system absolute references should remain
echo "==> verifying no Homebrew paths remain"
if otool -L "$MACOSDIR/evobox" "$FWDIR"/*.dylib 2>/dev/null | grep -E '/opt/homebrew|/usr/local/(opt|Cellar)' ; then
    echo "WARNING: unresolved Homebrew references above — the bundle is not self-contained" >&2
else
    echo "   clean (only @rpath + system libraries)"
fi

# ---- zip
DIST="$BUILD/dist"
mkdir -p "$DIST"
ZIP="$DIST/$PKG.zip"
rm -f "$ZIP"
# ditto's zip preserves the executable bit and bundle layout; --sequesterRsrc is deliberately NOT
# used (it strips the executable mode of the main binary on extraction).
( cd "$BUILD" && /usr/bin/ditto -c -k --keepParent "$APPNAME.app" "$ZIP" )
echo "==> $ZIP"
ls -lh "$ZIP"
