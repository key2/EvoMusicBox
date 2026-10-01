#!/usr/bin/env bash
# tools/windows/build.sh — cross-compile EvoMusicBox for Windows x64 from Linux (mingw-w64) and
# package it as a distributable zip. Optionally runs the unit tests and a smoke run through Wine.
#
#   tools/windows/build.sh                 # deps + configure + build + tests (Wine) + zip
#   tools/windows/build.sh --no-tests      # skip ctest/smoke
#   tools/windows/build.sh --no-tiktok     # soundboard + OSC only (EVOBOX_WITH_TIKTOK=OFF)
#   tools/windows/build.sh --clean         # wipe build-win/ (keeps the downloaded dependencies)
#
# Needs: x86_64-w64-mingw32-g++-posix (Debian/Ubuntu: g++-mingw-w64-x86-64-posix), cmake >= 3.20,
# ninja, curl, unzip, tar, python3 (icon header); for the TikTok client also gendef +
# x86_64-w64-mingw32-dlltool (mingw-w64-tools, binutils-mingw-w64) and a protoc (protobuf-compiler;
# a matching one is built when the installed version differs); wine for the tests (optional);
# makensis for the installer (the "nsis" package — on Debian/Ubuntu the script extracts it locally
# with `apt-get download` when it is not installed, no root needed).
# Result: build-win/dist/EvoMusicBox-<version>-win64.zip   (unzip-and-run)
#         build-win/dist/EvoMusicBox-<version>-win64.exe   (NSIS installer: Program Files, Start
#                                                           Menu / desktop shortcuts, .liv files,
#                                                           uninstaller)
#   evobox.exe (icon: musicbox.ico) + FFmpeg DLLs (BtbN win64 LGPL shared build, includes
#   libmp3lame) + GCC runtime DLLs + assets/ + fonts/ + README.md + LICENSES.txt
#   + with TikTok: libcurl-impersonate.dll (upstream Windows release), cacert.pem (Mozilla CA
#   bundle: the DLL has no certificate store), tiktok-js/ (the signer's scripts).
# TikTok dependencies (build-win/deps, downloaded / built once): curl-impersonate's Windows DLL
# with a mingw import library generated from it (gendef + dlltool), and a static mingw protobuf
# cross-built from the release matching protoc, so tiktok.pb.cc and libprotobuf agree.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${BUILD_DIR:-$ROOT/build-win}
FFMPEG_URL=${FFMPEG_URL:-https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip}
# must match the default in third_party/ttlive-cpp/cmake/curl-impersonate.cmake
CURL_IMPERSONATE_VERSION=${CURL_IMPERSONATE_VERSION:-v2.0.0a5}
# the generated tiktok.pb.cc (host protoc) and the cross-built libprotobuf must be the same release
PROTOBUF_VERSION=${PROTOBUF_VERSION:-3.21.12}
RUN_TESTS=1
TIKTOK=1
for arg in "$@"; do
    case "$arg" in
        --no-tests) RUN_TESTS=0 ;;
        --no-tiktok) TIKTOK=0 ;;
        --clean) find "$BUILD" -mindepth 1 -maxdepth 1 ! -name deps -exec rm -rf {} + 2>/dev/null || true ;;
        -h|--help) sed -n '2,26p' "$0"; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

step() { printf '\n\033[1;36m== %s\033[0m\n' "$*"; }

# ---- 1. dependencies: a mingw FFmpeg (headers + import libs + DLLs)
FFMPEG_ROOT=$BUILD/deps/ffmpeg
if [ ! -f "$FFMPEG_ROOT/lib/libavcodec.dll.a" ]; then
    step "Downloading FFmpeg for Windows ($FFMPEG_URL)"
    mkdir -p "$BUILD/deps"
    curl -L --fail --progress-bar -o "$BUILD/deps/ffmpeg-win64.zip" "$FFMPEG_URL"
    rm -rf "$BUILD/deps/ffmpeg" "$BUILD/deps/ffmpeg-extract"
    mkdir -p "$BUILD/deps/ffmpeg-extract"
    unzip -q "$BUILD/deps/ffmpeg-win64.zip" -d "$BUILD/deps/ffmpeg-extract"
    mv "$BUILD/deps/ffmpeg-extract"/* "$FFMPEG_ROOT"
    rmdir "$BUILD/deps/ffmpeg-extract"
    rm -f "$BUILD/deps/ffmpeg-win64.zip"
fi
echo "FFmpeg: $FFMPEG_ROOT ($(ls "$FFMPEG_ROOT/bin" | grep -c '\.dll$') DLLs)"

# ---- 1b. NSIS (installer): system makensis, else the Debian packages extracted under deps/nsis
NSIS_ARGS=()
if command -v makensis >/dev/null 2>&1; then
    echo "NSIS: $(command -v makensis) ($(makensis -VERSION 2>/dev/null))"
else
    NSIS_ROOT=$BUILD/deps/nsis
    if [ ! -x "$NSIS_ROOT/usr/bin/makensis" ] && command -v apt-get >/dev/null 2>&1 && command -v dpkg-deb >/dev/null 2>&1; then
        step "Fetching NSIS (apt-get download nsis nsis-common, extracted locally)"
        rm -rf "$NSIS_ROOT" "$BUILD/deps/nsis-deb"
        mkdir -p "$NSIS_ROOT" "$BUILD/deps/nsis-deb"
        if (cd "$BUILD/deps/nsis-deb" && apt-get download nsis nsis-common); then
            for deb in "$BUILD"/deps/nsis-deb/*.deb; do dpkg-deb -x "$deb" "$NSIS_ROOT"; done
        else
            echo "could not download the nsis packages" >&2
        fi
        rm -rf "$BUILD/deps/nsis-deb"
    fi
    if [ -x "$NSIS_ROOT/usr/bin/makensis" ]; then
        # a wrapper that points the relocated makensis at its stubs/plugins, so plain `cpack`
        # runs (outside this script) work as well
        mkdir -p "$NSIS_ROOT/bin"
        cat > "$NSIS_ROOT/bin/makensis" <<EOF2
#!/bin/sh
export NSISDIR="$NSIS_ROOT/usr/share/nsis" NSISCONFDIR="$NSIS_ROOT/etc"
exec "$NSIS_ROOT/usr/bin/makensis" "\$@"
EOF2
        chmod +x "$NSIS_ROOT/bin/makensis"
        NSIS_ARGS=(-DEVOBOX_NSIS_ROOT="$NSIS_ROOT" -DCPACK_NSIS_EXECUTABLE="$NSIS_ROOT/bin/makensis")
        echo "NSIS: $NSIS_ROOT/bin/makensis ($("$NSIS_ROOT/bin/makensis" -VERSION 2>/dev/null))"
    else
        echo "NSIS: makensis not available - the zip is built, the installer is skipped (install the 'nsis' package)" >&2
    fi
fi

# ---- 1c. TikTok LIVE client (ttlive-cpp): curl-impersonate + protobuf for mingw
TIKTOK_ARGS=(-DEVOBOX_WITH_TIKTOK=OFF)
if [ "$TIKTOK" = 1 ]; then
    for tool in gendef x86_64-w64-mingw32-dlltool tar; do
        command -v "$tool" >/dev/null 2>&1 || { echo "$tool not found (mingw-w64-tools / binutils-mingw-w64) - use --no-tiktok to skip the TikTok client" >&2; exit 1; }
    done
    # curl-impersonate: the upstream Windows release is a self-contained libcurl-impersonate.dll
    # (static CRT, BoringSSL, nghttp2, brotli, zstd inside; imports only system DLLs) built with
    # clang-cl. GNU ld links it through an import library generated from the DLL's export table;
    # the MSVC .lib files of the archive are left out (the 2.6 MB "static" one lacks BoringSSL).
    CI_ROOT=$BUILD/deps/curl-impersonate
    if [ ! -f "$CI_ROOT/lib/libcurl-impersonate.dll.a" ] || [ ! -f "$CI_ROOT/include/curl/curl.h" ]; then
        step "Downloading curl-impersonate $CURL_IMPERSONATE_VERSION for Windows"
        rm -rf "$CI_ROOT"
        mkdir -p "$CI_ROOT"
        curl -L --fail --progress-bar -o "$CI_ROOT/libcurl-impersonate-win32.tar.gz" \
             "https://github.com/lexiforest/curl-impersonate/releases/download/$CURL_IMPERSONATE_VERSION/libcurl-impersonate-$CURL_IMPERSONATE_VERSION.x86_64-win32.tar.gz"
        tar xzf "$CI_ROOT/libcurl-impersonate-win32.tar.gz" -C "$CI_ROOT" lib/libcurl-impersonate.dll include
        rm -f "$CI_ROOT/libcurl-impersonate-win32.tar.gz"
        (cd "$CI_ROOT/lib" && gendef libcurl-impersonate.dll >/dev/null \
            && x86_64-w64-mingw32-dlltool -d libcurl-impersonate.def -D libcurl-impersonate.dll -l libcurl-impersonate.dll.a)
    fi
    # the DLL carries no CA store: ship Mozilla's bundle (ttlive and IconFetcher look for
    # cacert.pem next to the executable; Deploy/Package.cmake copy it from CURL_IMPERSONATE_ROOT)
    if [ ! -s "$CI_ROOT/cacert.pem" ]; then
        step "Downloading the Mozilla CA bundle (cacert.pem)"
        curl -L --fail --progress-bar -o "$CI_ROOT/cacert.pem" https://curl.se/ca/cacert.pem
    fi
    echo "curl-impersonate: $CI_ROOT ($(grep -c '^curl_' "$CI_ROOT/lib/libcurl-impersonate.def") exports)"

    # protobuf: a static mingw libprotobuf of the pinned release (no tests, no protoc, no zlib),
    # installed as a CMake prefix that ttlive-cpp's find_package(Protobuf CONFIG) picks up
    PB_ROOT=$BUILD/deps/protobuf
    PB_SRC=$BUILD/deps/src/protobuf-$PROTOBUF_VERSION
    PB_CMAKE_ARGS=(-G Ninja -DCMAKE_BUILD_TYPE=Release -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_BUILD_SHARED_LIBS=OFF
                   -Dprotobuf_WITH_ZLIB=OFF -Wno-dev)
    # CMake >= 4 refuses protobuf 3.21's cmake_minimum_required(VERSION 3.5) without this
    [ "$(cmake --version | sed -n 's/^cmake version \([0-9]*\).*/\1/p')" -ge 4 ] 2>/dev/null && PB_CMAKE_ARGS+=(-DCMAKE_POLICY_VERSION_MINIMUM=3.5)
    if [ ! -f "$PB_SRC/CMakeLists.txt" ]; then
        step "Downloading protobuf $PROTOBUF_VERSION sources"
        mkdir -p "$BUILD/deps/src"
        curl -L --fail --progress-bar -o "$BUILD/deps/src/protobuf-cpp-$PROTOBUF_VERSION.tar.gz" \
             "https://github.com/protocolbuffers/protobuf/releases/download/v${PROTOBUF_VERSION#3.}/protobuf-cpp-$PROTOBUF_VERSION.tar.gz"
        rm -rf "$PB_SRC"
        tar xzf "$BUILD/deps/src/protobuf-cpp-$PROTOBUF_VERSION.tar.gz" -C "$BUILD/deps/src"
        rm -f "$BUILD/deps/src/protobuf-cpp-$PROTOBUF_VERSION.tar.gz"
    fi
    if [ ! -f "$PB_ROOT/lib/cmake/protobuf/protobuf-config.cmake" ] || [ ! -f "$PB_ROOT/lib/libprotobuf.a" ]; then
        step "Cross-building protobuf $PROTOBUF_VERSION (static, mingw-w64)"
        rm -rf "$PB_ROOT" "$BUILD/deps/src/protobuf-build"
        cmake -S "$PB_SRC" -B "$BUILD/deps/src/protobuf-build" "${PB_CMAKE_ARGS[@]}" \
              -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchains/x86_64-w64-mingw32.cmake" \
              -DCMAKE_INSTALL_PREFIX="$PB_ROOT" -Dprotobuf_BUILD_PROTOC_BINARIES=OFF \
              -DCMAKE_CXX_FLAGS="-Wa,-mbig-obj"
        ninja -C "$BUILD/deps/src/protobuf-build" install >/dev/null
    fi
    # protoc runs on the host: the system one when it is the pinned release, else one built from
    # the same sources (ttlive-cpp's CMake takes TTLIVE_PROTOC before searching PATH)
    PROTOC_ARGS=()
    SYS_PROTOC_VERSION=$(protoc --version 2>/dev/null | awk '{print $2}' || true)
    if [ "$SYS_PROTOC_VERSION" != "$PROTOBUF_VERSION" ]; then
        HOST_PROTOC=$BUILD/deps/protobuf-host/bin/protoc
        if [ ! -x "$HOST_PROTOC" ]; then
            step "Building a host protoc $PROTOBUF_VERSION (installed: ${SYS_PROTOC_VERSION:-none})"
            rm -rf "$BUILD/deps/protobuf-host" "$BUILD/deps/src/protobuf-host-build"
            cmake -S "$PB_SRC" -B "$BUILD/deps/src/protobuf-host-build" "${PB_CMAKE_ARGS[@]}" \
                  -DCMAKE_INSTALL_PREFIX="$BUILD/deps/protobuf-host"
            ninja -C "$BUILD/deps/src/protobuf-host-build" install >/dev/null
        fi
        PROTOC_ARGS=(-DTTLIVE_PROTOC="$HOST_PROTOC")
        echo "protoc: $HOST_PROTOC"
    else
        echo "protoc: $(command -v protoc) ($SYS_PROTOC_VERSION)"
    fi
    echo "protobuf: $PB_ROOT"
    TIKTOK_ARGS=(-DEVOBOX_WITH_TIKTOK=ON
                 -DEVOBOX_DEPS_ROOT="$BUILD/deps"
                 -DCMAKE_PREFIX_PATH="$PB_ROOT"
                 -DCURL_IMPERSONATE_LOCAL_DIR="$CI_ROOT"
                 "${PROTOC_ARGS[@]}")
fi

# ---- 2. configure + build
step "Configuring (mingw-w64 toolchain, TikTok $([ "$TIKTOK" = 1 ] && echo ON || echo OFF))"
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchains/x86_64-w64-mingw32.cmake" \
      -DFFMPEG_ROOT="$FFMPEG_ROOT" \
      "${TIKTOK_ARGS[@]}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DEVOBOX_BUILD_TESTS=$([ "$RUN_TESTS" = 1 ] && echo ON || echo OFF) \
      "${NSIS_ARGS[@]}"
step "Building"
ninja -C "$BUILD"

# ---- 3. tests + smoke run through Wine
if [ "$RUN_TESTS" = 1 ]; then
    if command -v wine >/dev/null 2>&1; then
        export WINEDEBUG=${WINEDEBUG:--all}
        step "Unit tests under Wine"
        ctest --test-dir "$BUILD" --output-on-failure
        step "Smoke run under Wine (hidden window, 60 frames, screenshot)"
        rm -f "$BUILD/smoke.png"
        (cd "$BUILD" && wine ./evobox.exe --new --demo --no-audio --frames 60 --screenshot "Z:$BUILD/smoke.png")
        test -s "$BUILD/smoke.png" && echo "screenshot: $BUILD/smoke.png"
        if [ "$TIKTOK" = 1 ]; then
            step "Gift gallery smoke run under Wine (--demo-gifts: catalog, Simulate, OSC)"
            rm -f "$BUILD/smoke-gifts.png"
            (cd "$BUILD" && wine ./evobox.exe --new --demo --demo-gifts --no-audio --frames 150 --screenshot "Z:$BUILD/smoke-gifts.png")
            test -s "$BUILD/smoke-gifts.png" && echo "screenshot: $BUILD/smoke-gifts.png"
        fi
    else
        echo "wine not found: skipping tests and smoke run" >&2
    fi
fi

# ---- 4. package
step "Packaging"
rm -rf "$BUILD/dist"
cpack --config "$BUILD/CPackConfig.cmake" -B "$BUILD/dist" | grep -E "package:|Error|error" || true
ls -la "$BUILD"/dist/EvoMusicBox-*-win64.*
