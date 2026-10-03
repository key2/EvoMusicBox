# EvoMusicBox — Handover

Read this first when picking the project up in a fresh session. It says what exists, how it is
built and verified (Linux and Windows), how the tricky parts work, what is deliberately
unfinished, and the traps that cost time. The design authority is `docs/architecture.md`
(system design, milestones M0–M6) and `docs/UI.md` (interface spec, numbered §1–§45);
`README.md` is the user-facing build/run guide. Where this file and a doc section written before
2026-09-28 disagree (effects, `.liv`, MP3 clips, merge), the code and this file win.

Last updated: 2026-10-01 (sessions of 2026-09-28, 2026-09-30 and 2026-10-01).

---

## 1. What this is

EvoMusicBox is a C++17 desktop app: a **soundboard** (tiles with rendered clips, music vs
stackable effects, trim/gain/fade/normalize clip editor) plus an **OSC trigger launcher** (every
sound has Start / End / Timer phases of OSC commands sent to named UDP targets), plus a **TikTok
LIVE gift gallery** where gifts and room events (like / follow / share / subscribe / join) carry
the same OSC actions. Shows are saved as one **`.liv`** file (a zip: `project.json` + MP3 clips +
picture stickers) that plays without the original media.

Stack: Dear ImGui (docking) + `imgui_organic` (Container/Parameter model, undo, selection, dock
manager, compiled-in miniaudio) + GLFW 3.5.1 (vendored) + FFmpeg (system on Linux, prebuilt DLLs
on Windows: decoding of any media, MP3 encoding of clips) + miniz (`.liv`) + Phosphor icons +
`ttlive-cpp` (optional TikTok client; Linux and, since 2026-10-01, the Windows build too).

State: all roadmap milestones M0–M6 implemented. Linux: builds warning-free, **11/11 test suites**
pass, end-to-end flows verified headlessly (import → tile at once → trim → play → OSC; gift →
Simulate → Start/Stop timer; video import → frame thumbnails → picture sticker → save `.liv` →
reopen; legacy WAV show → MP3 conversion; merge of two shows). Windows x64: cross-compiled with
mingw-w64 **with the TikTok client**, the same 11 suites pass under Wine, the `--demo-gifts` smoke
run shows the gift gallery / Live Monitor / Simulate / OSC working, NSIS installer + zip produced
and exercised under Wine (install / run / uninstall). A real TikTok LIVE room has **never** been
connected, from either OS (§7).

---

## 2. Quick start

### Linux (development machine)

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo   # once; -DEVOBOX_WITH_TIKTOK=OFF = light build
ninja -C build && ctest --test-dir build --output-on-failure      # build + 11 doctest suites

./build/evobox                                   # normal run (reopens the last project)
./build/evobox --verbose                         # organic Logger mirrored to stderr
./build/evobox --new --demo --no-audio --frames 150 --screenshot /tmp/ui.png   # headless smoke
./build/evobox --new --demo --no-audio --frames 300 --import clip.mp4 --save-as /tmp/V.liv
./build/evobox /tmp/V.liv --no-audio --frames 120 --screenshot /tmp/v2.png     # reopen a show
./build/evobox Demo2.liv --merge V.liv --no-audio --frames 120 --save-as /tmp/Merged.liv
./build/evobox --demo --demo-gifts --frames 150 --no-audio --screenshot /tmp/gifts.png
python3 -u tools/osc_listen.py 8000              # see OSC arriving (run alongside the app)
```

`build/` is a configured Ninja tree (RelWithDebInfo, TikTok ON, ~1.8 GB with `_deps`); the first
configure needs network once (ttlive's CMake downloads curl-impersonate into `build/_deps`).

### Windows x64 (cross-compiled from Linux, tested with Wine)

```bash
sudo apt install g++-mingw-w64-x86-64-posix mingw-w64-tools protobuf-compiler wine   # wine only for tests
tools/windows/build.sh                              # deps + configure + build + ctest (Wine) + packages
#   build-win/dist/EvoMusicBox-0.1.0-win64.exe        NSIS installer (53 MB)
#   build-win/dist/EvoMusicBox-0.1.0-win64.zip        unzip-and-run (71 MB)
tools/windows/build.sh --no-tiktok                  # soundboard + OSC only (EVOBOX_WITH_TIKTOK=OFF)
WINEDEBUG=-all wine build-win/evobox.exe --new --demo --no-audio --frames 60 --screenshot Z:$PWD/shot.png
WINEDEBUG=-all wine build-win/evobox.exe --new --demo --demo-gifts --no-audio --frames 150 --screenshot Z:$PWD/gifts.png
```

The script downloads BtbN's `ffmpeg-n8.1-latest-win64-lgpl-shared` into `build-win/deps/ffmpeg`
(FFmpeg 8.1 / lavc 62 with `libmp3lame`; our source compiles against FFmpeg 7.1 on Linux and 8.1
on Windows), fetches NSIS when `makensis` is missing (`apt-get download nsis nsis-common` +
`dpkg-deb -x` into `build-win/deps/nsis`, no root; a wrapper script exports `NSISDIR`/
`NSISCONFDIR`), prepares the TikTok client's dependencies (next paragraph), configures with
`cmake/toolchains/x86_64-w64-mingw32.cmake`, builds, runs `ctest` through Wine, does a 60-frame
smoke run and a `--demo-gifts` one, then `cpack` (ZIP + NSIS). `--no-tests`, `--no-tiktok` and
`--clean` exist (`--clean` keeps `build-win/deps`). `build-win/` and `build/` are ignored by
`.gitignore` (`/build*/`).

**TikTok dependencies for mingw** (`build-win/deps`, downloaded / built once — ~20 s on the dev
machine): (1) `curl-impersonate/`: the upstream Windows release
`libcurl-impersonate-v2.0.0a5.x86_64-win32.tar.gz` (lexiforest) — a self-contained
`libcurl-impersonate.dll` built with clang-cl (static CRT, BoringSSL, nghttp2, brotli, zstd inside,
imports only `WS2_32 CRYPT32 IPHLPAPI Normaliz KERNEL32`; exports `curl_easy_impersonate` and the
`curl_ws_*` API). GNU ld links it through `lib/libcurl-impersonate.dll.a`, generated from the DLL's
export table with `gendef` + `x86_64-w64-mingw32-dlltool` (the archive's MSVC `.lib` files are not
extracted: the 2.6 MB "static" one lacks BoringSSL). `cacert.pem` (Mozilla bundle from curl.se) is
downloaded next to it because the DLL has no certificate store; `Deploy/Package.cmake` ship it
from `CURL_IMPERSONATE_ROOT`. (2) `protobuf/`: a static protobuf **3.21.12** cross-built from
`protobuf-cpp-3.21.12.tar.gz` (`protobuf_BUILD_PROTOC_BINARIES=OFF`, no tests, no zlib, 8 s) and
installed as a CMake prefix; ttlive's `find_package(Protobuf CONFIG)` finds it via
`CMAKE_PREFIX_PATH`, and since the prefix has no `protobuf::protoc` target its CMake falls through
to the **host** `protoc`. The pinned version equals the dev machine's `protoc` (3.21.12); when the
installed `protoc` differs the script builds a host `protoc` from the same tarball
(`deps/protobuf-host`) and passes `-DTTLIVE_PROTOC`. (3) zlib: the sysroot's static `libz.a`
(`ZLIB_USE_STATIC_LIBS` in the toolchain file). OpenSSL is not needed — see §9. The toolchain adds
`EVOBOX_DEPS_ROOT` (= `build-win/deps`) to `CMAKE_FIND_ROOT_PATH`, so paths below it survive the
`ONLY` re-rooting. Manual configure: see the comment at the top of the toolchain file.

### Smoke-run facts

`--frames N` paces frames at 16 ms (hidden window, no vsync) and exits; `--screenshot` captures
the last frame; `--save-as` saves at the end; `--import <file>` adds sounds after startup;
`--drop` simulates an OS drop on frame 10; `--merge <show>` merges on frame 10; `--new` skips
reopening the last project; `--no-audio` = null backend; `--demo` imports 3 generated WAVs and at
`frames/2` trims sound 0, adds OSC rows and plays it (with an `--import`ed video it also waits
for the frame thumbnails, picks the middle one as the picture and flags the sound as an effect);
`--demo-gifts` seeds an offline catalog and at `2/3 frames` configures the Galaxy gift and
simulates events. Drivers live in `src/app/Demo.cpp`. Headless Linux runs print
`GLFW error 65548: Wayland: ... window position` (harmless); Wine runs print `libEGL`/`ZINK`
noise (harmless).

### Runtime paths

Linux: `~/.config/EvoMusicBox/{prefs.json,imgui.ini,layouts/}`,
`~/.cache/EvoMusicBox/{gift-catalog.json,gift-icons/,demo/,scratch/,liv/}`. Windows:
`%APPDATA%\EvoMusicBox`, `%LOCALAPPDATA%\EvoMusicBox`. `liv/` holds the working folders of
opened `.liv` files (`<stem>-<sha1 of the absolute path>.evobox`, never pruned); `scratch/
session-<epoch>/` holds clips of untitled projects. Autosave: `<bundle>/autosave/project.json`.

---

## 3. Repository

Public GitHub repository: **https://github.com/key2/EvoMusicBox** (branch `main`, since
2026-10-01). The six library checkouts under `third_party/` are **git submodules** pinned to the
revisions below (`git clone --recursive`, or `git submodule update --init --recursive`); doctest,
miniz and stb are plain vendored files. Two submodules are this author's own public repos —
`key2/imgui_organic` and `key2/ttlive-cpp` (the mingw CMake patch of §9 lives there as a commit on
`master`); the other four point at upstream (`glfw/glfw`, `ocornut/imgui` *docking*,
`aiekick/ImGuiFileDialog`, `phosphor-icons/web`). Bumping a submodule = commit inside it (or fetch
upstream), then commit the new pointer here. Not in the repository (`.gitignore`): `build*/`,
`out/` (downloaded music), `soundpacks/` (the 335 MB Pixabay pack + its `.liv`), `__pycache__`,
runtime files. There is **no `LICENSE` file yet** — the licence of EvoMusicBox's own code is the
author's decision (README says so); third-party terms are in `windows/LICENSES.txt`. Both READMEs
(`README.md`, `README.zh-CN.md` — keep them in sync) ship in the packages.

Third-party revisions: GLFW **3.5.1** (`third_party/glfw`, built in-tree — see §9 "GLFW Wayland
crash"), imgui docking 9b4eb24ce (1.92.9), imgui_organic 665c85e, ttlive-cpp 86b6da7 + the mingw
CMake commit (+ QuickJS submodule 04be246), ImGuiFileDialog d0e97b2, doctest 2.4.11,
stb_image_write (implementation in `media/ImageWriter.cpp`), miniz 3.0.2, Phosphor web package
(`third_party/web` 3d40a3ea).

### Map

```
src/util    ThreadSafeQueue, WorkerPool(+CancelToken), SpscRing, Hash (sha1/fnv/content hash, 64-bit
            seeks), Paths (config/cache/exe/resources, replaceFile), Strings, TimeFormat, ScopeExit,
            ZipFile (miniz: writeDirectory/list/readEntry/extractTo/looksLikeZip), CrashHandler (POSIX)
src/model   Project (root Container + StructureListener → revision/dirty, cross-ref repair,
            deleteSounds/Category/TargetUndoable, migrate() v1→v2, bundleDir + archivePath),
            Sound (+isEffectP, clipFile, rt runtime block), Category, GiftAction, RoomEventAction
            (5 fixed kinds), OscTarget(+Manager: Localhost builtin, one default), OscActions →
            OscPhase(Start/End/Timer, delayP) → OscCommandManager → OscCommand, Triggerable
            (KIND-TAGGED ids: makeTriggerableUid), Settings (PlaybackPolicy), MediaRef/MediaAsset,
            InspectorHooks (UI injects drawing; the model never includes UI)
src/osc     OscMessage (OSC 1.0 + bundles), OscCommandParser, OscEndpoint (getaddrinfo), OscSender
            (UDP, winsock branch), OscScheduler (thread, due-time multimap, cancel/reschedule, events)
            behind IOscScheduler, OscService
src/media   FFmpegDecoder (probe/decodeAll/decodeRange, budget 20 min/1 GB, log sink, retry with the
            extension's demuxer when probing fails), ClipRenderer (trim/gain/normalize/fades,
            +saveWavFloat32 fallback), ClipEncoder (MP3 via libavcodec/libmp3lame + swresample +
            AVAudioFifo; clipExtension(), isLegacyClip(), mp3SampleRate()), ImageDecoder,
            ImageWriter (PNG via stb, swscale resize), VideoFrames (thumbnails), MediaLibrary
            (path→asset LRU), MediaService (2 workers: Decode/ClipLoad/Render/ClipEncode/Image/
            Frames → MediaEvent queue drained on the main thread), IMediaSource
src/audio   AudioEngine (ma_engine facade, 32 voices over ma_audio_buffer_ref, PlayParams{gain,
            fadeIn, loop, startFrame/endFrame (sub-range), seekFrame (position in the whole clip)},
            end callback → SpscRing, steal oldest, fade stop, null backend), PreviewPlayer (kept,
            unused by the UI)
src/live    LiveEvent, GiftCatalog, LiveEventRouter (gift/like/room policies, streak tracking),
            TikTokLiveService (thread, ttlive callbacks → queue), IconFetcher (curl thread)
src/app     Application (owns everything: drainAll()/tick(), project new/open/save/merge, clip
            load → render → encode pipeline, legacy-clip conversion queue, transient gift promotion,
            relink, sticker textures, frame candidates, media request generations),
            TriggerController (OSC sessions), PlaybackController (voices ↔ sessions, music-vs-effect
            policy, PlayOptions{loop,startSec}, per-voice progress), ImportController (decode →
            createSound at once; show files diverted to openRequestPath), ProjectIO (bundle/.liv
            save/load, scratch dir, GC, converted-clip adoption, autosave), ProjectMerge (File >
            Merge show), Prefs, FileDropQueue, Demo
src/ui      Theme, Fonts, Icons (sticker scheme "ph:name"/"emoji:x"/"img:icons/x.png"), IGFDConfig.h,
            Shell (dock panels, menubar, status bar, IGFD dialogs: Open/Save/Merge/Import/StickerImage,
            unsaved prompt, autosave recovery, Merge options modal, shortcuts, performance mode),
            panels/ (Navigator, Workspace → SoundboardTab + GiftGalleryTab, Inspector, ClipEditor,
            LiveMonitor, Settings), widgets/ (Common, Tiles, WaveformEditor (multi-playhead,
            app-independent), TrimFields, StickerPicker, OscEditor, PhaseTimeline)
src/main.cpp GLFW/GL3 window, ImGui + ImPlot, drop callback, frame loop (drainAll → NewFrame → tick →
            shell.frame → Render), smoke flags, Windows console re-attach
cmake/      FindFFmpeg (pkg-config or FFMPEG_ROOT), Deploy (assets/fonts beside the exe), Package
            (install + CPack ZIP/TGZ/NSIS + runtime DLL staging), Phosphor (icon header from style.css),
            toolchains/x86_64-w64-mingw32.cmake
windows/    evobox.rc.in + evobox.manifest (version info, GLFW_ICON, UTF-8 code page, long paths,
            PerMonitorV2 DPI), LICENSES.txt (shipped + installer licence page). musicbox.ico = root.
tools/      gen_phosphor_icons.py (run by CMake), osc_listen.py, fake_live_feed.py, make_test_media.sh,
            liv_export_mp3.py (.liv → one MP3 per sound, button region only), windows/build.sh
tests/      doctest: osc_parser, model_roundtrip, trigger_controller (FakeScheduler.h), live_router,
            scheduler, decoder_smoke (MediaService render/load/encode; mp3/mp4/png cases need the
            ffmpeg CLI), clip_renderer (+ClipEncoder gapless/resample/probe regressions, replaceFile),
            audio_engine (null backend), playback_policy (music vs effects, stop button, editor
            PlayOptions), project_io (zip, .liv round trip, GC/adoption rules, load ordering),
            project_merge
assets/     stickers.json;  docs/  architecture.md, UI.md;  README.md
```

---

## 4. How the important parts work

### 4.1 Per-frame flow (main thread)

`Application::drainAll()` drains MediaService, the TikTok service and IconFetcher, then
`TriggerController::tick` (OSC events → pulses, `rt.active`) and `PlaybackController::tick`
(VoiceEnded → `TriggerController::end` → After-play; `rt.progress01` = latest voice,
`rt.voiceProgress` = every voice: clip-relative progress + position in source seconds).
`Application::tick()` schedules debounced re-renders
(`Sound::rt.clipDirty` + 350 ms), pumps the legacy-clip conversion queue, promotes an edited
transient gift, evicts the source cache, autosaves. UI panels only talk to `Application`.

### 4.2 Clips: render → encode → load (the intricate state machine)

- A tile plays `Sound::rt.clip`, an in-memory float buffer that **is** the trim range (trim, gain,
  normalize, fades applied by `ClipRenderer::render`). Progress 0..1 therefore spans the selected
  region, never the whole source.
- On disk the clip is **`clips/<uid 6 digits>.mp3`** (`ClipEncoder`: libmp3lame VBR q2, input
  resampled to an MPEG rate ≤ 48 kHz and downmixed to ≤ 2 ch, FFmpeg's mp3 muxer writes the
  LAME gapless tag so a decoded clip has exactly the rendered frame count). `.wav` (float32) is the
  fallback when the FFmpeg build has no MP3 encoder (`ClipEncoder::clipExtension()` decides the
  name via `ProjectIO::clipFileName`). Files are written to `<path>.<n>.tmp` and moved into place
  with `paths::replaceFile` (never removes an existing file unless the replacement is intact).
- `MediaService::requestRender` posts **two events**: `Rendered` with the buffer as soon as it
  exists (the tile switches at once) and `ClipEncoded` when the file is on disk.
  `rt.renderPending` = a render (or an initial clip load) is in flight; `rt.encodePending` = a
  file write is in flight (render write or legacy conversion). **One writer per clip file**:
  `Application::requestRender` defers (`clipDirty`) while either flag is set; `onClipEncoded`
  re-requests a deferred render; `scheduleDirtyRenders` skips sounds with a pending write.
- Media requests carry a **per-open generation** in the request id (`soundRequestId` /
  `soundForRequest`): sound uids restart at 1 in every project and jobs are never cancelled on
  open, so events of the previous show are dropped instead of landing on a same-uid sound.
- **Legacy shows** (float32 `.wav` clips from before 2026-09-30) load as before (the decoder is
  container-agnostic). `onClipLoaded` queues them (`queueLegacyConversion` → `conversionQueue_`),
  `pumpConversions` re-encodes **one at a time** to `clips/<uid>.mp3` from the decoded buffer (no
  source needed). The model is **not** touched and the project stays clean: `ProjectIO::
  convertedClipFor` adopts a converted file only when it exists and is *strictly newer* than the
  persisted one — in `save()` (which then garbage-collects the `.wav`) and in `load()`
  (`adoptConvertedClips` rewrites the JSON before `Project::load`, for conversions done in an
  earlier unsaved session; miniz restores zip mtimes on extraction so this works for `.liv`). The
  status line says "Converted N clip(s) to MP3 - save the show to shrink the file". The same
  rule moves `.mp3` clips back to `.wav` on an encoder-less machine when they are re-rendered.
- `ProjectIO::save()` writes `project.json` **first**, then garbage-collects `clips/` — deleting
  only clip files (`.mp3`/`.wav`) and `.tmp` files that belong to no live sound (protected: each
  sound's `clipFile`, its `clipFileName()` and `<name>.<n>.tmp` variants) — then `icons/*.png`.
- `FFmpegDecoder::openInput` retries `avformat_open_input` with the demuxer named after the file
  extension when probing fails: FFmpeg cannot probe MP3s shorter than ~7 frames (tie with the raw
  h263 probe → `AVERROR_INVALIDDATA`, `ffprobe` fails the same way).

### 4.3 Playback, tiles, Clip Editor

- `Sound::isEffect()`: effects stack (press again = another voice) and ignore `MusicExclusive`;
  music is one voice at a time (a new music, or the same music again, stops the previous one;
  `PlaybackPolicy` also offers `Overlap` / `StopOthers`). `PlaybackController::stop(uid)` fades
  **every** voice of a sound.
- Tiles (`ui/widgets/Tiles.cpp`): music shows ▶ ↔ ■ in the bottom-right circle and, while
  playing, a thin progress line in the row gap under the box; effects keep ▶ (stack), get a
  stop circle bottom-left while any voice plays (stops all), a `xN` count, and a short progress
  bar inside the tile (latest voice). The list view mirrors this (▶ + ■ for playing effects).
- The Clip Editor's **Play/Stop is the tile's playback**: its button, `Ctrl+Space` and a click in
  the waveform all call `PlaybackController::play(s, src, PlayOptions{loop, startSec})` — the same
  voice (rendered clip, OSC Start phase fires). There is no separate preview voice any more, so a
  sound is never heard twice. The waveform draws one playhead per running voice in **source
  seconds, measured against the clip that voice actually plays** — never against the live trim
  handles: `Sound::rt.clipSourceStart` records where `rt.clip`'s first frame sits in the source
  (the trim start the render/load was requested with, echoed back in `MediaEvent::sourceStartSec`
  because the handles may have moved meanwhile), `PlaybackController::ActiveVoice` captures the
  buffer + that start at `play()`, and `tick()` publishes `rt.voiceProgress[i].sourceSec =
  clipSourceStart + cursor / sampleRate`. So dragging a handle while a song plays leaves the
  playhead on the audio being heard, and a voice started before a re-render landed keeps tracking
  its old region (possibly outside the new selection — that is what is playing). A click in the
  waveform seeks relative to `rt.clipSourceStart` too. Colours: music in the theme playhead yellow,
  stacked effects in yellow/orange/sky/pink/green/violet. Loop applies only to voices started from
  the editor; `PlayParams::seekFrame` positions a voice inside the whole clip (the older
  `startFrame/endFrame` define a sub-range whose cursor/length/loop are range-relative — unusable
  for this).
- OSC sessions: `TriggerController::begin` schedules Start (+ Timer) immediately, `end()`
  schedules End; Timer phases are always scheduled so `rt.active` reflects the configured Stop
  time. Triggerable identities are kind-tagged (`Triggerable.h`) — never compare a
  `triggerableUid()` with a BaseItem `uid`.

### 4.4 Projects: bundles, `.liv`, merge

- `Project::bundleDir` always points at a real folder (an `.evobox` bundle or the `.liv` working
  folder under the cache dir); `archivePath` only tells Save where to pack. `saveArchive` =
  `save()` into the working folder + zip of `project.json`, `clips/` (stored, not deflated),
  `icons/`; `loadArchive` extracts over that folder and loads. Sources/videos are never stored.
- `ProjectIO::load` sets `bundleDir` **before** `Project::load(j)`, because `Project::load` ends
  with `onAnyChange` → `Application::onProjectChanged` → lazy `loadClipFor` for every new sound;
  `loadAllClips()` afterwards only requests sounds with `!rt.loadAttempted` and never resets a
  sound's `rt`. Format version is 2 (`migrate()` remaps the v1 playback-policy enum); MP3 clips
  did not need a bump (`clipFile` stays a relative path; older builds still open new files).
- **File → Merge show…** (`Ctrl+Shift+O`, `app/ProjectMerge`): reads the other show's
  `project.json` as plain JSON (**never through a `Project` object** — constructing one clears
  the undo history), copies clip/picture files first (`clips/<newUid><old ext>`; icons are
  content-addressed), then applies one `UndoManager::perform("Merge show")`. Categories match
  by name, OSC targets by builtin → host:port → name, sounds always get fresh uids (`nextUid++`
  pre-assigned in the JSON) with `categoryUid`/`targetUid` remapped; "Sounds only" erases the
  sounds' actions; gift actions are skipped when the gift is already configured; room events
  (fixed set) get the commands appended and the sound adopted if empty. Items are added
  construct → `load(j)` → `addItem` so the lazy clip loader sees complete sounds. Settings and
  `media/` copies are not merged. Smoke flag `--merge`.

### 4.5 Windows specifics

`main.cpp`: `windows.h` (lean, `NOMINMAX`) before GLFW, `GL_CLAMP_TO_EDGE` fallback (mingw's
`GL/gl.h` is GL 1.1), `WIN32_EXECUTABLE` + `attachParentConsole()` so smoke flags print from a
console. Link `ws2_32 gdi32` (static GLFW's WGL code needs gdi32; its CMake only puts `-lgdi32`
in the .pc). `windows/evobox.rc.in`: version info, `GLFW_ICON` (GLFW uses that resource name for
the window icon, Explorer for the file), manifest with **`activeCodePage UTF-8`** (every narrow
`fopen`/`ifstream` takes UTF-8 paths — verified with `Démo ü.liv`), `longPathAware`, PerMonitorV2.
`cmake/Package.cmake`: install rules, FFmpeg DLLs located from the import libs we link, the
curl-impersonate DLL located next to its `.dll.a`, GCC runtime DLLs via `g++ -print-file-name`,
all staged beside `build-win/evobox.exe` (POST_BUILD) and installed, plus `tiktok-js/` and
`cacert.pem` with TikTok; CPack ZIP + NSIS (Program Files, Start Menu + optional desktop shortcut,
`.liv` association via `CPACK_NSIS_EXTRA_INSTALL_COMMANDS`, uninstaller that removes the
association only if it still points at `EvoMusicBox.Show`, finish-page Run, `/SOLID lzma`). Tests
under Wine: `EVOBOX_TEST_DIR` = `Z:<dir>`, `WINEPATH=Z:<build-win>` (that is how the test
executables find `libcurl-impersonate.dll` and the FFmpeg DLLs staged there),
`CMAKE_CROSSCOMPILING_EMULATOR=wine` (toolchain file; `-DEVOBOX_WINE=OFF` disables). TikTok on
Windows: the mingw build of `ttlive-cpp` (§2 "TikTok dependencies for mingw"); the root
`CMakeLists.txt` adds `-Wa,-mbig-obj` to `ttlive` (unoptimised, `tiktok.pb.cc` has ~77 K COMDAT
sections, over the 32 K COFF limit) and `__USE_MINGW_ANSI_STDIO=1` to `quickjs` (upstream's
`CONFIG_WIN32` does the same). `IconFetcher` sets `CURLOPT_CAINFO` to the `cacert.pem` /
`curl-ca-bundle.crt` beside the exe (or `CURL_CA_BUNDLE` / `SSL_CERT_FILE`) — the same lookup
ttlive's `web_defaults::ca_bundle_path()` does — and `CURLSSLOPT_NATIVE_CA` on Windows; without a
bundle the BoringSSL DLL would fail every TLS verification. Not on Windows: crash reports
(`CrashHandler` is POSIX-only).

### 4.6 Translations (English / Russian / Chinese)

One flat catalogue per language, `assets/lang/<code>.json` (deployed next to the exe like the other
assets; 566 keys, identical key sets — `tools`-free check: the script that added the 2026-10-03
batch asserted parity and matching printf specifiers). `src/ui/I18n.*`: `TR("key")` → `const char*`
valid for the frame, `trFmt("key", oneArg)` for a single `%s`/`%d`, `snprintf(buf, TR(fmt), ...)`
for several arguments. Missing key → English → the key itself (visible, never fatal). The Language
menu switches at runtime (prefs `language`); ImGui labels change text, so widgets whose identity
matters carry `##id` / `###id` suffixes.

Four mechanisms feed everything else into the same catalogues:
- **`LTR("key", "English")`** (`src/util/Localize.h`): text born below the UI — default item names
  (`sound.newName`, `category.default.*`, `gift.fallbackNamePrefix`), undo-history labels
  (`undo.*`, shown by Edit ▸ Undo and the status bar), status messages (`status.*`), error texts of
  ProjectIO / ProjectMerge / TikTok (`error.*`, `live.error.*`), the merge summary
  (`merge.summary.*`, format strings so Russian can write "звуки: %d"), import filter names. The
  English in the call is the fallback; `I18n::init()` installs the translator, the **tests install
  nothing** and keep asserting on English (`findByName("Music")`, `"nothing to merge"`).
- **`LocalizeParam(param, labelKey, descKey, enumPrefix)`** (`ui/widgets/Common.*`): sets organic's
  `displayName` / `displayDescription` / `enumLabels` every frame before `DrawParamWidget` /
  `inspectorGui()` — used for the Settings container (`settings.param.*`, the "Playback policy"
  case), the Sound clip parameters (`param.gain` …) and the gift / room-event parameters. JSON short
  names stay the English ones.
- **Display helpers** for identities that stay English on disk: `phaseLabel()` (the fixed phase
  names are match keys when a show is loaded or merged), `roomEventLabel()` (persisted kind keys),
  `liveEventLabel()` (the feed line; `LiveEvent::summary()` stays English for the logger),
  `agoLabel()`, `oscErrorLabel()` (parser messages). Default categories are **seeded in the UI
  language** and are user data afterwards; `Category::key` ("music" … "custom", persisted, derived
  from the English name for older shows) replaces the name lookup in `customCategory()`. The
  Application constructor seeds its project before `I18n::init()` (members first), so `main.cpp`
  calls `project.resetToDefaults()` once more right after the catalogue is loaded.
- **File dialog** (`src/ui/IGFDGlue.*`, compiled into the ImGuiFileDialog target): the vendored
  library's texts are config macros, some concatenated with `"##id"` literals, so they cannot
  expand to calls. `IGFDConfig.h` routes every button through `igfd::button()` /
  `igfd::toggleButton()` (translates the part before `##`) and plain strings through
  `igfd::text()`; `I18n::init()` installs the English→key map (`fileDialog.*`). The overwrite
  modal's title is literal-concatenated too, so it is a warning glyph.

`SectionHeader` upper-cases Cyrillic as well as ASCII (CJK has no case). Still English by design:
data (gift names, device names, OSC addresses, demo content), the `Localhost` target, decoder /
network diagnostics, and undo labels generated inside `imgui_organic` ("Set Gain", "Remove Items" —
would need a hook in that library). Verifying a language headlessly: write `{"language":"zh"}` into
a scratch `XDG_CONFIG_HOME` and run with `--screenshot`; `--open-panel Settings` shows a hidden
panel, `--import-dialog <dir>` the file dialog.

---

## 5. Design decisions worth knowing

- **OSC transport is in-tree** (`src/osc/OscMessage.*`), not oscpack.
- **Model ↔ UI decoupling**: `Sound/GiftAction/RoomEventAction::inspectorGui()` call
  `InspectorHooks`; `InspectorPanel::installHooks()` sets them; tests get organic's auto form.
- **Selection scopes**: sub-managers (`OscCommandManager`, `OscTargetManager`, `CategoryManager`)
  use private `selectionScopeName`s so organic's `undoableAdd/Duplicate/Paste` never steal the
  main selection.
- **Structure notifications**: `notifyStructureChanged(container)` walks `Container::parent` to
  every `StructureListener` (only `Project`) → revision bump → dirty + `onAnyChange`.
- **Edits**: plain fields via `setFieldUndoable()`; Parameters via organic's `setUndoable` /
  `recordEdit` / `DrawParamWidget`; multi-step edits are one `UndoManager::perform`.
  Dirty tracking is revision-based (undo back to the saved state still reads dirty).
- **Transient gift action**: selecting an unconfigured gift creates a `GiftAction` owned by
  `Application`; `promoteTransientIfEdited()` moves it into the manager on the first edit.
- **Negative delays**: representable (`OscPhase::delayP` min −600 000) but the UI clamps to ≥ 0.
- **Picture stickers**: `img:icons/<sha1-16>.png`, content-addressed, copied + GC'd by `save()`;
  `Application::stickerTexture()` decodes lazily on the worker pool (request ids set bit 63 so
  they never collide with gift ids; frame requests bit 62); `setStickerFromImage()` scales to
  ≤ 256 px. Video imports fetch 16 frame candidates for the Clip Editor's thumbnail strip.
- **Streaks**: `LiveEventRouter::streakNewUnits()` tracks each combo per gift + sender; the
  TikTok summary message (`repeat_end = 1`, ~3 s after the last tap) adds 0 units so "Every event"
  does not re-fire and counts are not doubled.
- **Performance mode** is session-only (F11); it saves a layout named `editing` and reloads it.
- **Default dock ratios** come from organic's `DockManager::buildDefaultLayout` (bottom 24 %).
- **MP3 quality** is a constant (`kVbrQuality = 2` in `ClipEncoder.cpp`); no user setting yet.

---

## 6. Change log (newest first, with the reasons)

**2026-10-03**
- **Translation completeness** (§4.6): the i18n commit left ~280 English strings outside the
  catalogues — the whole Settings panel and the clip parameters (organic's generic widgets drew the
  model's English names), phase and room-event names, default categories, confirm buttons, status
  bar and error messages, undo labels, the live feed lines, relative times, the file dialog. Added
  `LTR()` (`util/Localize.h`) for text born below the UI, a shared `LocalizeParam()`, display
  helpers for on-disk identifiers, `Category::key`, the ImGuiFileDialog glue, Cyrillic-aware
  section headers, and 160 keys in each of en/ru/zh. `--open-panel <name>` smoke flag. 11/11 tests
  (they see the English fallbacks). README (EN + ZH, now in sync) describe the layers.
- Pulled `2d71dfa` (macOS: 3.2 Core GL context, `tools/macos/build.sh` self-contained `.app`) and
  `88e2b2f` (i18n: Language menu English / Russian / Chinese, `assets/lang/<code>.json` catalogues,
  `src/ui/I18n.*`, Noto Sans CJK merged into the UI font, `imgui_organic` → 98fa479 for the
  translator hooks). `src/ui/I18n.h` used `std::shared_ptr` without `<memory>` — fine on the
  toolchain it was written on, a hard error with GCC 14 / mingw-w64 (`'shared_ptr' in namespace
  'std' does not name a template type`): include added. Linux and Windows rebuilt from `main`:
  11/11 suites on both, Chinese UI verified headlessly on both (prefs `language` = `zh`); the
  16 MB CJK font grows the packages to 22 MB (Linux tgz), 64 MB installer / 84 MB zip. The
  **v0.1.0 release assets were replaced** with this build and the `v0.1.0` tag moved onto this
  commit (`git tag -f` + `git push --force origin v0.1.0`, `gh release upload --clobber`) — the
  release was hours old with no downloads; from now on a changed build gets a new version + tag
  (bump `EVOBOX_VERSION`). Not yet done: this file has no §4 section on the macOS packaging
  (`tools/macos/build.sh`) — README.md's "macOS build" section is the reference for now.

**2026-10-01**
- **Release v0.1.0**: annotated tag on `main`, GitHub Release with the Windows x64 artifacts built by
  `tools/windows/build.sh` from that commit (`EvoMusicBox-0.1.0-win64.exe` installer + `.zip`,
  SHA-256 in the notes). Releasing = bump `EVOBOX_VERSION` in `CMakeLists.txt`, run the script,
  `git tag -a vX.Y.Z`, `gh release create vX.Y.Z build-win/dist/EvoMusicBox-X.Y.Z-win64.{exe,zip}`.
  Binaries are unsigned (SmartScreen prompt) and Wine-verified only. READMEs point at the Releases
  page.
- **Published**: `git init`, the six `third_party` checkouts registered as submodules, first commit
  pushed to the public repo `github.com/key2/EvoMusicBox` (§3). `.gitignore` grew `soundpacks/`
  and `__pycache__`; the ttlive-cpp mingw patch was committed to `key2/ttlive-cpp`. README got a
  "Getting the source" section (recursive clone), a licence note, and a Simplified Chinese twin
  `README.zh-CN.md` (both installed by `Package.cmake`).
- **TikTok on Windows**: the Windows package was built with `EVOBOX_WITH_TIKTOK=OFF`, which
  compiles out the Gifts tab, the gift navigator, the Live menu, the Live Monitor and the Settings
  section — the whole gift feature was invisible there. `tools/windows/build.sh` now prepares the
  mingw dependencies under `build-win/deps` (curl-impersonate's Windows DLL + a generated import
  library + `cacert.pem`; a cross-built static protobuf 3.21.12 matching the host `protoc`; the
  sysroot's static zlib), configures with TikTok ON (`--no-tiktok` to opt out), stages/installs
  `libcurl-impersonate.dll`, `cacert.pem` and `tiktok-js/`, and adds a `--demo-gifts` smoke run
  under Wine. Vendored `ttlive-cpp`: dead OpenSSL dependency dropped (§9), Windows-aware shared-lib
  message. `IconFetcher` sets `CURLOPT_CAINFO` to the shipped bundle (+ `CURLSSLOPT_NATIVE_CA` on
  Windows). Root `CMakeLists.txt`: `-Wa,-mbig-obj` for `ttlive`, `__USE_MINGW_ANSI_STDIO` for
  `quickjs` on MinGW. 11/11 suites pass under Wine with the TikTok build; `LICENSES.txt` lists the
  new components. Installer 53 MB, zip 71 MB.
- Clip Editor playhead moved with the trim handles: it was mapped as `trimStart + p·(trimEnd −
  trimStart)` from the *live* parameters while `p` belongs to the buffer the voice actually plays
  (rendered from the trim at render time) — dragging a handle rescaled the line although the audio
  had not changed, and it stayed wrong for every voice started before a re-render landed. Now each
  clip remembers its source start (`rt.clipSourceStart`, echoed by the media job), each voice
  carries its own buffer + start, and `rt.voiceProgress` reports the position in source seconds
  (§4.3). Regression test in `playback_policy_test`.
- Windows x64: mingw-w64 cross build, NSIS installer + zip, icon (`musicbox.ico`), Wine-verified
  (§2, §4.5). Found by Wine: Windows `stat()` has 1 s mtime resolution → the converted-clip rule
  became *strictly newer* and its test sets explicit mtimes.
- Clip Editor Play/Stop linked to the tile's voice; per-voice coloured playheads; `PreviewPlayer`
  unused (§4.3). Lost on purpose: live audition while dragging handles with Loop on (the clip
  re-renders after the debounce; the next press plays the new region).
- Music tiles: thin progress line under the box (effects keep the inner bar).
- File → Merge show… with the "Sounds only / Sounds and OSC" dialog (§4.4).

**2026-09-30**
- Effect tiles: stop circle (bottom-left) stopping every voice; list view gets ■ next to ▶.
- Clips are MP3; legacy `.wav` shows convert on load; `.liv` ~15× smaller (§4.2). A six-track
  review of that change fixed, before shipping: GC racing in-flight writers (now protected set +
  JSON written first), a destructive rename fallback (`paths::replaceFile`), stale cross-project
  events (request generations), the `Rendered` event waiting for the encode (now split), the
  conversion flooding the FIFO pool (one at a time), vacuous `.tmp` test assertions, and tests
  assuming libmp3lame (gated on `mp3EncoderName()`).
- Pre-existing bug fixed: every clip was requested twice on open because `Project::load`
  notified before `bundleDir` was set and `loadAllClips()` reset in-flight state (§4.4).
- `FFmpegDecoder::openInput` extension retry for tiny MP3s; `liv_export_mp3.py` stream-copies
  MP3 clips unless `--reencode`.
- GLFW Wayland crash on a 666-file drag & drop → vendored GLFW 3.5.1 (§9).

**2026-09-28**
- Gift OSC stopped firing after the first event: `triggerableUid()` collided across managers →
  kind-tagged identities (`makeTriggerableUid`), `findTriggerable` decodes the tag,
  `LiveEventRouter::sessionStillActive()` trusts the controller. Tests in `live_router_test`.
- Flaky SIGSEGV constructing `Project`: managers notify during construction → `loading_` and
  `onAnyChange` are declared before the managers, `loading_` starts `true`.
- Imported media becomes a Sound immediately (no Save Clip step); the Clip Editor edits in place.
- Music vs effects (`Sound::isEffectP`, `PlaybackPolicy` MusicExclusive/Overlap/StopOthers,
  format v2 migration).
- Tile drag & drop was dead: `ImGui::SetWindowFocus` while the mouse button is held clears the
  active item → focus requests are deferred to mouse release (`Shell::focusBottomPanelIfRequested`).
- Picture stickers (file or video frame), video frame candidates, `.liv` show container (miniz),
  `--save-as/--import/--new` smoke flags, `tools/liv_export_mp3.py`.

---

## 7. Known gaps and roadmap

1. **Real TikTok LIVE session** — `TikTokLiveService` compiles and is wired on both platforms
   (Connect button, `Live` menu, prefs username/polling/auto-connect) but has never run against a
   live room. Expect to need a `ttwid` cookie (`LiveOptions::cookies`, not exposed in the UI) and
   to check `TikTokLiveService::defaultJsDir()` (`<exe>/tiktok-js`, else the source tree).
   IconFetcher → ImageDecoder → GL upload is unexercised with real URLs. On Windows, the first real
   connection also validates the shipped `cacert.pem` path (§4.5) and that Wine/Windows accept the
   clang-cl-built `libcurl-impersonate.dll` for WSS (the probe under Wine — `curl_version_info`,
   `ws`/`wss` protocols, `curl_easy_impersonate("chrome131")` — passed).
2. **Platforms** — Linux from source; Windows cross-built with TikTok, Wine-verified only (no
   real-hardware audio/HiDPI check yet; crash reports missing — §4.5); macOS untested (`Paths.cpp`,
   `Socket.h` have branches). The Windows TikTok build pins curl-impersonate `v2.0.0a5` and
   protobuf `3.21.12` in `tools/windows/build.sh` (`CURL_IMPERSONATE_VERSION` / `PROTOBUF_VERSION`
   env overrides): bumping ttlive's curl-impersonate tag means bumping the script too, and the
   protobuf release must stay the one `tiktok.pb.cc` is generated with (the script builds a host
   `protoc` when the installed one differs).
3. **Tests missing**: `ImportController` flow (covered by the `--import` smoke only), a fake
   TikTok client, organic's own `model_test`, UI smoke in CI (the `--frames/--screenshot` run is
   ready). Not exercised with a real mouse: tile drag & drop onto Navigator categories, an edit
   during a pending legacy conversion, the Merge dialog, the NSIS installer UI (silent mode was).
4. **Multi-select tiles** (Shift/Ctrl-click, marquee) — single-select today; `InspectorPanel::
   drawMulti` already handles a homogeneous multi-selection.
5. **Copy/paste of whole sounds** (Ctrl+C/V) — not wired (organic's clipboard exists); OSC
   actions copy/paste between sounds and gifts is implemented.
6. **Later**: per-tile hotkeys (`Sound::hotkeyP` stored, not bound), negative delays UI,
   exclusive groups/fades, streaming long media (`IMediaSource` is the seam), emoji stickers
   (`EVOBOX_WITH_EMOJI` compiles, not built), comment-keyword actions, OSC input/OSCQuery, audio
   device hot-plug, a user-facing clip quality setting, picture stickers for categories, a
   "recently used pictures" row, a multi-size `musicbox.ico` (16/32/48/256 — the current file is
   32×32 only), a Windows crash handler (`SetUnhandledExceptionFilter`), pruning of `liv/`
   working folders.
7. Minor: `OscTargetManager::removeTargetUndoable` undo does not restore command references
   (commented in code); the default dock split cannot be tuned without touching organic.

---

## 8. Suggested next steps

1. Pick a licence for EvoMusicBox's own code and add the `LICENSE` file (README §Licence).
2. Try the Windows package on a real Windows 10/11 machine: WASAPI output, HiDPI, the installer
   UI, `.liv` double-click, the Gifts tab + Live Monitor.
3. Real TikTok LIVE connection test with `--verbose` (§7.1), on Linux and on Windows.
4. CI: `ctest` + the headless screenshot run on Linux, `tools/windows/build.sh --no-tests` for
   the Windows artifacts (Wine tests optional).
5. Then the "Later" list (§7.6).

---

## 9. Gotchas learned the hard way

Build / libraries
- `imgui_organic` compiles `misc/cpp/imgui_stdlib.cpp` itself → do **not** add it to our `imgui`
  target (duplicate symbols).
- Anything including `miniaudio.h` must define `MA_NO_ENCODING` first; never define
  `MINIAUDIO_IMPLEMENTATION` again (organic compiles the implementation).
- IGFD forces `IMGUI_DEFINE_MATH_OPERATORS` (PUBLIC on `imgui`); `IGFDConfig.h` includes the
  generated icon header → `ImGuiFileDialog` depends on `evobox_phosphor_header`.
- Phosphor has an icon named `table`, so the generated name table is `ICON_PH_NAME_TABLE`.
- `stb_image_write` has exactly one implementation TU (`media/ImageWriter.cpp`).
- FFmpeg 7 decoder names can be `mp3float`; compare with `find("mp3")`. FFmpeg 8 (Windows build)
  removed the `AVCodec::sample_fmts`-style arrays: use `avcodec_get_supported_config` (guarded
  on lavc ≥ 61.13 in `ClipEncoder.cpp`).
- `FindFFmpeg.cmake` trusts pkg-config only when not cross-compiling (the host's FFmpeg would be
  reported); `FFMPEG_ROOT` is a find prefix (CMP0144 NEW).
- **ttlive-cpp for mingw** (local patch of the vendored tree, keep when updating it): its
  `CMakeLists.txt` required OpenSSL, yet no source includes it (TLS is BoringSSL inside
  curl-impersonate, the signature code runs in QuickJS) — `find_package(OpenSSL REQUIRED)` and the
  `OpenSSL::SSL/Crypto` links were removed, which is what made the cross build possible without a
  mingw OpenSSL (the Linux binary lost a dead `libssl` dependency; libav still pulls TLS libs
  transitively). `cmake/curl-impersonate.cmake` now prints a status instead of the libunwind
  warning on `WIN32` (the shared-lib caveat is about the zig-built Linux `.so`). With the
  toolchain's `CMAKE_FIND_ROOT_PATH_MODE_* ONLY`, every `find_*` path is re-rooted under the roots
  **unless it already lies below one** — hence `EVOBOX_DEPS_ROOT` in the toolchain so that
  `CMAKE_PREFIX_PATH=deps/protobuf` and `CURL_IMPERSONATE_LOCAL_DIR=deps/curl-impersonate` work.
  MinGW `find_library` prefers `.dll.a` over `.a` over `.lib`, so the generated import library wins
  even if MSVC `.lib`s were present, and `ZLIB` would pick `libz.dll.a` (→ `zlib1.dll` to ship)
  without `ZLIB_USE_STATIC_LIBS`. An MSVC-built DLL with a static CRT links fine from GNU ld through
  a `gendef`/`dlltool` import library — no CRT or dependency DLLs to ship.
- CPack NSIS from Linux: `CPACK_NSIS_BRANDING_TEXT` emits `/TRIM*` (unsupported by the Linux
  makensis) → leave it unset; the NSIS generator rejects `CPACK_INCLUDE_TOPLEVEL_DIRECTORY` → a
  `CPACK_PROJECT_CONFIG_FILE` turns it off for NSIS only; NSIS installers are 32-bit, so the
  Add/Remove entry lands under `HKLM\Software\WOW6432Node\...\Uninstall\EvoMusicBox`.
- **GLFW Wayland crash**: importing a 666-file sound pack by drag & drop segfaulted inside GLFW
  3.4's `dataDeviceHandleEnter` (`window == NULL` when the drag enters a non-GLFW surface such as
  the libdecor title bar). Fixed upstream in 3.5.1 → vendored (`EVOBOX_VENDORED_GLFW=ON`); with
  the system library the app falls back to X11 on Wayland (`choosePlatform()`,
  `EVOBOX_PLATFORM=wayland|x11`). Wayland cannot report window positions (`GLFW error 65548`,
  harmless; init the outputs of `glfwGetWindowPos`).

organic / model
- `BaseManager::addItemFromJson` calls `addItem` (allocates a uid **and notifies**) before
  `load()` restores the saved uid → `nextUid` drifts (`Project::loadManager` re-derives it) and
  listeners see an incomplete item. Items created from JSON need `"uid": 0` for a fresh uid; when
  the item must be complete at notification time (lazy clip loading), do construct → `load(j)` →
  `addItem` (ProjectMerge does).
- `BaseManager::undoableAdd` selects the new item in the manager's scope → sub-managers use
  private scopes or their own `add*Undoable`.
- `Project`'s member managers notify the project *during its construction*; anything they touch
  must be declared before the managers. Constructing a `Project` (or `load()`) clears the undo
  history — never build a temporary one to read another show.
- organic uids are per-manager: same `uid` for two kinds is normal; cross-kind lookups need the
  kind (`Triggerable.h`). `MediaEvent::Image` ids double as gift ids (bit 63/62 for stickers/frames).
- `Project::load` fires `onAnyChange`: set everything the listener resolves (paths!) before
  `p.load(j)`; never reset a sound's `rt` while a job is in flight (two writers of one clip file
  race). Count requests with gdb breakpoints on `MediaService::request*` when in doubt.
- `ImGui::PushID(nullptr)` crashes. `ImGui::SetWindowFocus` while a mouse button is held clears
  the active item (kills drag sources) — defer to mouse release.
- ImGui 1.92 fonts: `PushFont(font, size)`; size from `GetStyle().FontSizeBase * k`, never
  `GetFontSize()`.

Media / audio / files
- With `--frames`, the 16 ms pacing is what lets worker jobs finish; keep it or raise `--frames`.
- `ma_sound_stop_with_fade_in_milliseconds` leaves the voice "playing" until the fade ends; the
  slot is released by the safety net in `AudioEngine::drainEvents`.
- FFmpeg refuses to *probe* MP3 files of fewer than ~7 frames (`ffprobe` too); open them with an
  explicit demuxer (`FFmpegDecoder::openInput` retries by extension; `liv_export_mp3.py` passes
  `-f mp3`). `ffprobe`'s `format=duration` includes the encoder padding; decoded length is exact.
- Windows `stat()` mtime resolution is 1 s (libstdc++ `last_write_time` uses `_wstat64`): compare
  file times strictly and never rely on sub-second ordering.
- `std::error_code` pattern: `fs::remove(tmp, ec)` in an error path overwrites the `ec` you were
  about to report ("...: Success") — copy `ec.message()` first.
- Two unsaved `Project`s in one process share the scratch clips folder (tests save the target to
  its own bundle first).
- The `.liv` working folder is keyed by the *absolute archive path* (sha1): moving the file gives
  a fresh folder; the demo catalog (`roomUser == "demo"`) is never persisted — delete
  `~/.cache/EvoMusicBox/gift-catalog.json` if a stale one confuses testing.
- The soundboard handles 686 tiles at ~38 fps; importing 666 MP3s peaks at ~3.5 GB RSS (sources
  stay decoded in memory; `MediaLibrary::evict` only drops the library's own references).
