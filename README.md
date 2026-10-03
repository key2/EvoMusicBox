# EvoMusicBox

**English** | [简体中文](README.zh-CN.md)

A soundboard with a lightweight clip trimmer and an OSC trigger launcher, plus a TikTok LIVE gift
gallery where every gift (and every room event: like, follow, share, subscribe, join) can carry its
own OSC actions. Desktop C++17 application built on Dear ImGui (docking), `imgui_organic`,
FFmpeg, miniaudio and Phosphor icons. The design lives in `docs/architecture.md` (architecture)
and `docs/UI.md` (interface spec); `HANDOVER.md` is the maintainer's handbook; this README covers
building and running.

```
Navigator ─ categories / gift filters      Workspace ─ Sounds | Gifts tab      Inspector ─ OSC of the selection
                                        Clip Editor / Live Monitor (bottom)
```

**Downloads**: prebuilt Windows x64 packages — the NSIS installer and a portable zip — are on the
[Releases page](https://github.com/key2/EvoMusicBox/releases). Linux builds from source (below).

## Getting the source

The dependencies live under `third_party/` as git submodules, so clone recursively:

```bash
git clone --recursive https://github.com/key2/EvoMusicBox.git
cd EvoMusicBox
# an existing non-recursive clone:
git submodule update --init --recursive
```

Submodules: Dear ImGui (*docking* branch — `imgui_organic` requires it), `imgui_organic` (with its
own `implot`, `json`, `miniaudio`), ImGuiFileDialog, GLFW 3.5.1, `ttlive-cpp` (with its QuickJS
submodule — needed with TikTok support, the default) and the Phosphor icons web package. doctest,
miniz and stb are vendored as plain files.

Why a vendored GLFW: GLFW 3.4's Wayland drag & drop "enter" handler dereferences a NULL window
when a drag crosses a non-GLFW surface (the window decorations) — dragging media files onto the
app from a file manager crashed it on GNOME/Wayland (kernel log: `segfault ... in libglfw.so.3.4`).
Upstream fixed it in 3.5.1 (`51b6434`). Without `third_party/glfw` CMake falls back to the system
library (`-DEVOBOX_VENDORED_GLFW=OFF` forces that) and the app then prefers X11/XWayland on
Wayland sessions; `EVOBOX_PLATFORM=wayland|x11` overrides the platform choice.

## Dependencies

| Dependency | How it is used | Provided by |
|---|---|---|
| Dear ImGui (docking) + ImPlot | UI | `third_party/imgui`, `third_party/imgui_organic/implot` |
| imgui_organic | Container/Parameter model, undo, selection, dock manager, logger, audio buffers | `third_party/imgui_organic` (also compiles miniaudio) |
| ImGuiFileDialog | file dialogs | `third_party/ImGuiFileDialog` |
| FFmpeg 6/7 (`libavformat libavcodec libavutil libswresample libswscale`) | decode any media, gift icons (WebP) | **system** (`pkg-config`) |
| GLFW 3.5.1, OpenGL | window / rendering | `third_party/glfw` (built in-tree; system GLFW as fallback) |
| ttlive-cpp (+ QuickJS, protobuf, zlib, curl-impersonate) | TikTok LIVE events + gift catalog | `third_party/ttlive-cpp`; curl-impersonate is downloaded by CMake (OpenSSL is not needed: TLS lives inside curl-impersonate) |
| Phosphor icons | every icon / sticker | `third_party/web` (`Phosphor.ttf`, `Phosphor-Fill.ttf`) |
| doctest | unit tests | `third_party/doctest` |

Ubuntu/Debian packages:

```bash
sudo apt install build-essential cmake ninja-build pkg-config python3 libgl-dev \
     libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libswscale-dev \
     libprotobuf-dev protobuf-compiler zlib1g-dev \
     libwayland-dev wayland-protocols libxkbcommon-dev libdecor-0-dev \
     libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev   # GLFW (Wayland + X11)
```

(`libglfw3-dev` is only needed when building against the system GLFW instead of `third_party/glfw`.)

macOS: `brew install cmake ninja glfw ffmpeg protobuf`. Windows: cross-compiled from Linux
with mingw-w64 (see "Windows build" below); a native MSVC/vcpkg build should also work
(`cmake/FindFFmpeg.cmake` honours `FFMPEG_ROOT`) but is not exercised.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
ctest --test-dir build --output-on-failure     # unit tests
./build/evobox                                  # run
```

CMake options:

| Option | Default | Effect |
|---|---|---|
| `EVOBOX_WITH_TIKTOK` | `ON` | Build the TikTok LIVE trigger source (needs the QuickJS submodule + network at configure time to fetch curl-impersonate). `OFF` builds a pure soundboard + OSC launcher. |
| `EVOBOX_WITH_EMOJI` | `OFF` | Colour emoji stickers through `imgui_freetype` (needs FreeType). |
| `EVOBOX_BUILD_TESTS` | `ON` | doctest executables under `build/tests`. |
| `EVOBOX_WINE` | auto | Cross builds only: the Wine binary used as `CMAKE_CROSSCOMPILING_EMULATOR` for `ctest` (`OFF` to disable). |

The build deploys `assets/`, `fonts/` (Roboto + Phosphor) and `tiktok-js/` next to the executable.
`build/generated/IconsPhosphor.h` is generated from Phosphor's `style.css` by
`tools/gen_phosphor_icons.py` (CMake falls back to a pure-CMake generator without Python).

## Languages / translations

The UI ships in English, Russian and Chinese, switchable at runtime from the **Language** menu (or
Settings ▸ Interface). The choice is saved in prefs (`language`) and restored on launch.

Translations are **not** hard-coded: every UI string lives in a flat JSON catalogue under
`assets/lang/<code>.json` (`en.json`, `ru.json`, `zh.json`), keyed by stable dotted identifiers like
`menu.file.save`. The code looks each one up with `TR("key")` / `trFmt("key", arg)` (see
`src/ui/I18n.*`); a missing key falls back to English and then to the key itself, so nothing ever
crashes on an incomplete translation. Text that is born below the UI layer — default names of new
items, undo-history labels, status-bar and error messages — goes through `LTR("key", "English")`
(`src/util/Localize.h`): the English stays in the code as the fallback and the UI installs the
translator at start-up. Parameters drawn by the generic widgets (Settings, clip and gift settings)
get their labels through `LocalizeParam()`, and the vendored file dialog's texts are translated at
draw time (`src/ui/IGFDGlue.*`).

To **update wording**, edit the value in the relevant `*.json` — no rebuild of logic needed, the
file is read at startup (it is copied next to the executable / into the macOS bundle's
`Contents/Resources/` by the normal asset deploy).

To **add a language**, drop a new `assets/lang/<code>.json` next to the others (copy `en.json`, keep
the keys, translate the values, set `"language.native"` to the language's own name). It appears in
the Language menu automatically. Keep the printf specifiers (`%s`, `%d`, `%.1f`, …) intact and in a
form the string allows; the format arguments are positional.

Fonts: English/Russian render from Roboto, Chinese (and Cyrillic fallback) from the bundled
`NotoSansCJKsc-Regular.otf`, merged on top of Roboto in `src/ui/Fonts.cpp` (ImGui 1.92 rasterises
glyphs on demand, so the merge only costs atlas space for glyphs actually drawn).

Identifiers that live in the show files stay English on disk and are translated for display only:
the OSC phase names (`At play (start)`, `On gift`, `Stop`, …), the room-event kinds and the default
categories (a new show seeds its categories in the UI language; they are renamable user data from
then on, and the custom one is found by a persisted key, not by its name). Still English by
design: data such as gift names, audio device names and OSC addresses, the `Localhost` target, and
low-level diagnostics (decoder and network errors, undo labels generated inside `imgui_organic`).

## macOS build (self-contained .app)

The plain `cmake`/`ninja` build above works on macOS (GLFW builds its Cocoa backend; the app asks
for an OpenGL 3.2 Core profile, which is all macOS exposes). `tools/macos/build.sh` wraps it to
produce a **relocatable** `EvoMusicBox.app` that runs on a clean Mac without Homebrew:

```bash
brew install cmake ninja ffmpeg protobuf           # glfw is vendored
tools/macos/build.sh                                # configure + build + ctest + bundle + zip
#   -> build-mac/dist/EvoMusicBox-<version>-macos-<arch>.zip
tools/macos/build.sh --no-tiktok                    # soundboard + OSC only
```

The script copies every non-system dylib the binary links (FFmpeg, protobuf, abseil, brotli, zstd,
libidn2, …) into `EvoMusicBox.app/Contents/Frameworks`, rewrites their install names to `@rpath`
with `install_name_tool`, stages `assets/`, `fonts/` and `tiktok-js/` under `Contents/Resources`,
writes an `Info.plist` (with the `.liv` document type), converts `musicbox.ico` to an `.icns`, and
ad-hoc signs the bundle. The build is arm64 (Apple Silicon) on an arm64 host, x86_64 on an Intel
host; there is no universal binary yet. The `.app` is **not** notarized, so Gatekeeper asks on first
launch — right-click → Open, or `xattr -dr com.apple.quarantine EvoMusicBox.app`.

## Windows build (cross-compiled from Linux: installer + zip)

Ready-made packages are published on the [Releases page](https://github.com/key2/EvoMusicBox/releases)
(the binaries are not code-signed, so SmartScreen asks for confirmation on first run). To build
them yourself:

```bash
sudo apt install g++-mingw-w64-x86-64-posix mingw-w64-tools protobuf-compiler wine   # Debian/Ubuntu; wine only for the tests
tools/windows/build.sh                 # add --no-tiktok for a soundboard + OSC only build
#   -> build-win/dist/EvoMusicBox-<version>-win64.exe   NSIS installer
#   -> build-win/dist/EvoMusicBox-<version>-win64.zip   unzip-and-run
```

The script downloads a Windows FFmpeg (BtbN `win64-lgpl-shared` build of FFmpeg 8.1, with
`libmp3lame`) into `build-win/deps/ffmpeg`, fetches NSIS when `makensis` is not installed (the
Debian `nsis` packages extracted under `build-win/deps/nsis`, no root needed), prepares the
TikTok client's dependencies under `build-win/deps` (see below), configures with
`cmake/toolchains/x86_64-w64-mingw32.cmake` (GCC posix-threads variant, `FFMPEG_ROOT`,
`EVOBOX_DEPS_ROOT`), builds, runs the 11 unit tests, a 60-frame smoke run and a `--demo-gifts`
smoke run through Wine, and packs both artifacts with CPack (`cmake/Package.cmake`).

TikTok on Windows (`EVOBOX_WITH_TIKTOK=ON`, the default of the script): `ttlive-cpp` needs
curl-impersonate and protobuf for the target. The script downloads curl-impersonate's own Windows
release (`libcurl-impersonate-<tag>.x86_64-win32.tar.gz`: a self-contained
`libcurl-impersonate.dll` — static CRT, BoringSSL, nghttp2, brotli, zstd inside — built with
clang-cl) and generates a mingw import library from the DLL's export table (`gendef` +
`x86_64-w64-mingw32-dlltool`), downloads Mozilla's `cacert.pem` (the DLL has no certificate
store), and cross-builds a static protobuf 3.21.12 (`build-win/deps/protobuf`, a CMake prefix) of
the same release as the host `protoc` — when the installed `protoc` is another version a matching
host `protoc` is built from the same sources. zlib comes from the mingw sysroot (static `libz.a`).
Everything lands in `build-win/deps` once; `--clean` keeps it. OpenSSL is not needed.

Contents: `evobox.exe` (GUI subsystem, `musicbox.ico` as the application / window icon, UTF-8
code page + per-monitor DPI manifest, version info), `avcodec-62.dll avformat-62.dll
avutil-60.dll swresample-6.dll swscale-9.dll`, `libcurl-impersonate.dll`, `libstdc++-6.dll
libgcc_s_seh-1.dll libwinpthread-1.dll`, `cacert.pem`, `assets/`, `fonts/`, `tiktok-js/`,
`README.md`, `README.zh-CN.md`, `LICENSES.txt`.

The **installer** (NSIS 3, Modern UI, solid LZMA) installs to `%ProgramFiles%\EvoMusicBox`,
creates a Start Menu entry (and optionally a desktop shortcut), registers `.liv` show files so a
double-click opens them in EvoMusicBox, adds an Add/Remove Programs entry with an uninstaller
that removes everything it added (the file association only if it still points at EvoMusicBox),
and offers "Run EvoMusicBox" on the finish page. `EvoMusicBox-<version>-win64.exe /S` installs
silently. The **zip** needs no installer and no registry: unzip anywhere and run. Config lives in
`%APPDATA%\EvoMusicBox`, caches / working folders in `%LOCALAPPDATA%\EvoMusicBox`. The smoke
flags work from a console (`evobox.exe --new --demo --frames 60 --screenshot shot.png`), also
under `wine`. `-DEVOBOX_ICON=path.ico` swaps the icon (a multi-size .ico with 16/32/48/256 px
images looks crisper in Explorer than the current 32 px one).

Manual steps, if you prefer them:

```bash
# without TikTok (no extra dependencies), or run tools/windows/build.sh once and point at its deps:
#   -DEVOBOX_WITH_TIKTOK=ON -DEVOBOX_DEPS_ROOT=$PWD/build-win/deps \
#   -DCMAKE_PREFIX_PATH=$PWD/build-win/deps/protobuf -DCURL_IMPERSONATE_LOCAL_DIR=$PWD/build-win/deps/curl-impersonate
cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/x86_64-w64-mingw32.cmake \
      -DFFMPEG_ROOT=$PWD/build-win/deps/ffmpeg -DEVOBOX_WITH_TIKTOK=OFF -DCMAKE_BUILD_TYPE=Release
ninja -C build-win && ctest --test-dir build-win          # tests run through wine
cpack --config build-win/CPackConfig.cmake -B build-win/dist   # zip + installer (makensis on PATH)
```

Not yet on Windows: crash reports (`CrashHandler` is POSIX-only). The TikTok client builds and the
gift gallery works (Simulate, OSC), but a real TikTok LIVE room has not been connected from Windows
either — see `HANDOVER.md` §7.1.

## Running

```bash
./build/evobox                       # empty project (or reopens the last one)
./build/evobox MyShow.liv            # open a show file (a legacy MyShow.evobox folder works too)
./build/evobox --demo                # generate demo audio and import it (first-run tour)
./build/evobox --new --import song.mp3 --import clip.mp4   # start empty and add sounds
```

Useful flags: `--no-audio` (null audio backend), `--demo-gifts` (offline gift catalog),
`--verbose` (mirror the log to stderr), `--frames N --screenshot out.png` (headless smoke run,
paced at 60 Hz), `--save-as Show.liv` (save at the end of a smoke run), `--new`, `--import <file>`,
`--open-panel <name>` (show a dock panel such as `Settings` in a smoke run), `--performance`.

Keyboard: `Space` toggles the selected tile (fires an effect again), `Enter` plays it, `Esc` stops
everything and cancels pending OSC timers, `Ctrl+Space` simulates the selected gift / room event
(or plays the clip selection from the Clip Editor — the same voice as the tile), `F11` performance mode, `Ctrl+G` switches Sounds/Gifts,
`Ctrl+F` search, `Ctrl+Z/Shift+Z` undo/redo, `Ctrl+S/Shift+S/O/N/I` save/save as/open/new/import, `Ctrl+Shift+O` merge another show,
`Ctrl+1..9` saved layouts, arrows move the tile selection, `Delete` deletes it.

### Sounds: music vs effects

Dropped or imported media becomes a tile immediately (the whole file; trim it afterwards in the
Clip Editor). Each sound is either **music** (default: starting it stops the music that was
playing) or an **effect** (Inspector checkbox / tile context menu: plays on top of everything
and stacks — press three times, hear it three times). `Settings > Playback policy` can switch to
"Overlap everything" or "Stop others". Tiles can be dragged onto a category row of the Navigator
to move them, and a tile's sticker can be a picture: any image file, or one of the frames
extracted from an imported video.

### Show file

`Save` writes one **`MyShow.liv`** file — a zip container holding everything needed to reload the
show anywhere: `project.json` (categories, sounds and their OSC commands, gift actions, room
events, OSC targets, settings), `clips/*.mp3` (the rendered sounds) and `icons/*.png` (picture
stickers). Source media and videos are never included. A `.liv` is opened by extracting it into a
working folder under the cache dir; saving packs that folder back into the file.

**File → Merge show…** adds another `.liv` (or `.evobox`) to the current show: its sounds with their
clips, pictures and categories, and — if you choose "Sounds and OSC" in the dialog — its OSC
targets, the sounds' commands, gift and room-event actions. Categories and targets that already
exist (same name / same host:port) are reused, gifts already configured keep their action; the whole
merge is one undo step.

Rendered clips are **MP3** (libmp3lame through the system FFmpeg, VBR quality 2 ≈ 190 kbit/s,
gapless so a clip keeps its exact length): a 3-minute stereo song costs ~4 MB in the show file
instead of ~70 MB of float PCM. Shows saved by earlier versions hold float32 `clips/*.wav`; they
open normally, every clip is converted to MP3 in the background when the show is loaded (no
source media needed) and the next Save writes the smaller file. If the FFmpeg build has no MP3
encoder, clips fall back to float32 WAV.

```
MyShow.liv  (zip)                 MyShow.evobox/   (legacy folder layout, still readable)
├── project.json                  ├── project.json
├── clips/000012.mp3              ├── clips/000012.mp3      rendered clips (MP3; older shows: float32 WAV)
└── icons/3f9a…c1.png             ├── icons/…               picture stickers
                                  ├── media/                optional source copies (Settings)
                                  └── autosave/project.json rotating autosave
```

Machine-specific settings (`prefs.json`, `imgui.ini`, layouts, gift catalog + icon cache, `.liv`
working folders) live in the user config/cache directories (`~/.config/EvoMusicBox`,
`~/.cache/EvoMusicBox` on Linux).

### OSC

Commands are plain text: `/address arg arg ...` — integers become `i`, decimals `f`, `true/false`
`T/F`, everything else `s` (quote strings with spaces); `i: f: s: b: h: d:` prefixes force a type.
Each command picks a target from the address book (Localhost `127.0.0.1:8000` is always present
and default). Test with `python3 tools/osc_listen.py 8000`.

## Tools

- `tools/liv_export_mp3.py Show.liv [-o DIR]` – one MP3 per sound, containing **only the part
  selected for the button** (the rendered clip: trim, gain, normalize, fades). Options:
  `--numbered` (`01 - Name.mp3`), `--by-category`, `--music-only` / `--effects-only`,
  `--category NAME`, `--bitrate 256k` or `--vbr 2`, `--format mp3|m4a|ogg|flac|wav`, `--dry-run`.
  Needs the `ffmpeg` CLI. Files are tagged (title, album = show name, genre = category, track =
  tile order). MP3 clips exported as MP3 are copied unchanged (no second lossy generation) unless
  `--reencode` is given. A sound whose clip is missing from the show is cut from its original
  media when that file is still available.
- `tools/osc_listen.py [port]` – prints incoming OSC messages.
- `tools/fake_live_feed.py` – synthetic live event stream (JSON lines) for scripting.
- `tools/make_test_media.sh [dir]` – wav/mp3/mp4/webp test files via the ffmpeg CLI.
- `tools/gen_phosphor_icons.py` – regenerates the icon header (run by CMake).

## Layout

```
src/app     Application, controllers (Trigger, Playback, Import), Prefs, ProjectIO, Demo
src/model   Project, Sound, Category, GiftAction, RoomEventAction, OscActions/Phase/Command, OscTarget
src/media   FFmpegDecoder, ImageDecoder, ImageWriter, VideoFrames, ClipRenderer, ClipEncoder (MP3), MediaLibrary, MediaService (worker pool)
src/audio   AudioEngine (ma_engine voices) + PreviewPlayer
src/osc     OSC 1.0 encoder, command parser, endpoint, UDP sender, scheduler thread, OscService
src/live    LiveEvent, GiftCatalog, LiveEventRouter, TikTokLiveService, IconFetcher
src/ui      Theme, Fonts, Icons, Shell (menu/status/dialogs/shortcuts), panels/, widgets/
src/util    queues, worker pool, SPSC ring, hashing, paths, strings, time formatting, zip (miniz)
tests/      doctest suites (osc parser, model round trip, trigger controller, live router,
            scheduler, decoder smoke, clip renderer, audio engine, playback policy, project io)
```

Deviation from `docs/architecture.md`: OSC uses a small in-tree OSC 1.0 encoder + `sendto`
(`src/osc/OscMessage.*`, an option the document allows) instead of vendoring oscpack.

## Licence

The licence of EvoMusicBox's own code has not been chosen yet (no `LICENSE` file: all rights
reserved until one is added). The third-party components keep their own licences — they are listed
with their terms in `windows/LICENSES.txt` (also shipped in every package).
