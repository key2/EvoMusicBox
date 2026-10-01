# EvoMusicBox

[English](README.md) | **简体中文**

一款音效板（soundboard）应用：内置轻量的片段裁剪器与 OSC 触发启动器，并带有 TikTok LIVE 礼物画廊 ——
每一个礼物（以及每一种直播间事件：点赞、关注、分享、订阅、进入直播间）都可以绑定各自的 OSC 动作。
这是一个基于 Dear ImGui（docking 分支）、`imgui_organic`、FFmpeg、miniaudio 与 Phosphor 图标构建的
C++17 桌面应用。设计文档见 `docs/architecture.md`（架构）与 `docs/UI.md`（界面规格）；`HANDOVER.md`
是维护者手册；本 README 只介绍构建与运行。

```
Navigator ─ 分类 / 礼物筛选              Workspace ─ Sounds | Gifts 标签页      Inspector ─ 所选对象的 OSC
                                        Clip Editor / Live Monitor（底部）
```

**下载**：预编译的 Windows x64 安装包 —— NSIS 安装程序与便携 zip —— 发布在
[Releases 页面](https://github.com/key2/EvoMusicBox/releases)。Linux 请从源码构建（见下文）。

## 获取源码

依赖以 git 子模块的形式放在 `third_party/` 下，因此请递归克隆：

```bash
git clone --recursive https://github.com/key2/EvoMusicBox.git
cd EvoMusicBox
# 已有的非递归克隆：
git submodule update --init --recursive
```

子模块：Dear ImGui（*docking* 分支 —— `imgui_organic` 需要它）、`imgui_organic`（自带 `implot`、`json`、
`miniaudio` 子模块）、ImGuiFileDialog、GLFW 3.5.1、`ttlive-cpp`（自带 QuickJS 子模块 —— 启用 TikTok
支持时需要，默认启用）以及 Phosphor 图标的 web 包。doctest、miniz 与 stb 以普通文件的形式直接内置。

为什么要自带 GLFW：GLFW 3.4 的 Wayland 拖放 "enter" 处理函数在拖动跨过非 GLFW 表面（窗口装饰）时会解引用
一个空的 window 指针 —— 从文件管理器把媒体文件拖到应用上时，在 GNOME/Wayland 下会崩溃（内核日志：
`segfault ... in libglfw.so.3.4`）。上游已在 3.5.1（`51b6434`）修复。没有 `third_party/glfw` 时，CMake
会回退到系统库（`-DEVOBOX_VENDORED_GLFW=OFF` 可强制如此），此时应用在 Wayland 会话下会优先使用
X11/XWayland；`EVOBOX_PLATFORM=wayland|x11` 可覆盖平台选择。

## 依赖

| 依赖 | 用途 | 来源 |
|---|---|---|
| Dear ImGui（docking）+ ImPlot | 界面 | `third_party/imgui`、`third_party/imgui_organic/implot` |
| imgui_organic | Container/Parameter 模型、撤销、选择、停靠管理器、日志、音频缓冲 | `third_party/imgui_organic`（同时编译 miniaudio） |
| ImGuiFileDialog | 文件对话框 | `third_party/ImGuiFileDialog` |
| FFmpeg 6/7（`libavformat libavcodec libavutil libswresample libswscale`） | 解码任意媒体、礼物图标（WebP） | **系统**（`pkg-config`） |
| GLFW 3.5.1、OpenGL | 窗口 / 渲染 | `third_party/glfw`（源码内构建；系统 GLFW 作为回退） |
| ttlive-cpp（+ QuickJS、protobuf、zlib、curl-impersonate） | TikTok LIVE 事件 + 礼物目录 | `third_party/ttlive-cpp`；curl-impersonate 由 CMake 下载（不需要 OpenSSL：TLS 在 curl-impersonate 内部） |
| Phosphor 图标 | 所有图标 / 贴纸 | `third_party/web`（`Phosphor.ttf`、`Phosphor-Fill.ttf`） |
| doctest | 单元测试 | `third_party/doctest` |

Ubuntu/Debian 软件包：

```bash
sudo apt install build-essential cmake ninja-build pkg-config python3 libgl-dev \
     libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libswscale-dev \
     libprotobuf-dev protobuf-compiler zlib1g-dev \
     libwayland-dev wayland-protocols libxkbcommon-dev libdecor-0-dev \
     libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev   # GLFW（Wayland + X11）
```

（只有在使用系统 GLFW 而非 `third_party/glfw` 构建时才需要 `libglfw3-dev`。）

macOS：`brew install cmake ninja glfw ffmpeg protobuf`。Windows：在 Linux 上用 mingw-w64 交叉编译
（见下文 "Windows 构建"）；原生 MSVC/vcpkg 构建理论上也可行（`cmake/FindFFmpeg.cmake` 支持 `FFMPEG_ROOT`），
但未经验证。

## 构建

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
ctest --test-dir build --output-on-failure     # 单元测试
./build/evobox                                  # 运行
```

CMake 选项：

| 选项 | 默认值 | 作用 |
|---|---|---|
| `EVOBOX_WITH_TIKTOK` | `ON` | 构建 TikTok LIVE 触发源（需要 QuickJS 子模块，并在 configure 时联网下载 curl-impersonate）。`OFF` 则构建纯音效板 + OSC 启动器。 |
| `EVOBOX_WITH_EMOJI` | `OFF` | 通过 `imgui_freetype` 支持彩色 emoji 贴纸（需要 FreeType）。 |
| `EVOBOX_BUILD_TESTS` | `ON` | 在 `build/tests` 下生成 doctest 可执行文件。 |
| `EVOBOX_WINE` | 自动 | 仅交叉编译时：`ctest` 用作 `CMAKE_CROSSCOMPILING_EMULATOR` 的 Wine 程序（`OFF` 禁用）。 |

构建会把 `assets/`、`fonts/`（Roboto + Phosphor）与 `tiktok-js/` 部署到可执行文件旁边。
`build/generated/IconsPhosphor.h` 由 `tools/gen_phosphor_icons.py` 根据 Phosphor 的 `style.css` 生成
（没有 Python 时 CMake 会回退到纯 CMake 的生成器）。

## Windows 构建（在 Linux 上交叉编译：安装程序 + zip）

现成的安装包发布在 [Releases 页面](https://github.com/key2/EvoMusicBox/releases)（二进制文件未做代码签名，
首次运行时 SmartScreen 会要求确认）。如需自行构建：

```bash
sudo apt install g++-mingw-w64-x86-64-posix mingw-w64-tools protobuf-compiler wine   # Debian/Ubuntu；wine 仅用于测试
tools/windows/build.sh                 # 加 --no-tiktok 可只构建音效板 + OSC
#   -> build-win/dist/EvoMusicBox-<version>-win64.exe   NSIS 安装程序
#   -> build-win/dist/EvoMusicBox-<version>-win64.zip   解压即用
```

脚本会把 Windows 版 FFmpeg（BtbN 的 `win64-lgpl-shared` 构建，FFmpeg 8.1，含 `libmp3lame`）下载到
`build-win/deps/ffmpeg`；在未安装 `makensis` 时获取 NSIS（Debian 的 `nsis` 软件包解压到
`build-win/deps/nsis`，无需 root）；在 `build-win/deps` 下准备 TikTok 客户端的依赖（见下文）；用
`cmake/toolchains/x86_64-w64-mingw32.cmake`（GCC posix 线程变体，`FFMPEG_ROOT`、`EVOBOX_DEPS_ROOT`）
configure；构建；通过 Wine 运行 11 个单元测试、一次 60 帧的冒烟运行与一次 `--demo-gifts` 冒烟运行；
最后用 CPack（`cmake/Package.cmake`）打包两种产物。

Windows 上的 TikTok（`EVOBOX_WITH_TIKTOK=ON`，脚本默认）：`ttlive-cpp` 需要目标平台的 curl-impersonate
与 protobuf。脚本会下载 curl-impersonate 官方的 Windows 发行包（`libcurl-impersonate-<tag>.x86_64-win32.tar.gz`：
一个自包含的 `libcurl-impersonate.dll` —— 静态 CRT，内含 BoringSSL、nghttp2、brotli、zstd，由 clang-cl
构建），并根据该 DLL 的导出表生成 mingw 导入库（`gendef` + `x86_64-w64-mingw32-dlltool`）；下载 Mozilla 的
`cacert.pem`（该 DLL 没有证书存储）；交叉编译一份静态 protobuf 3.21.12（`build-win/deps/protobuf`，一个 CMake
前缀），版本与宿主机的 `protoc` 一致 —— 当已安装的 `protoc` 是其他版本时，会用同一份源码构建匹配的宿主机
`protoc`。zlib 来自 mingw 的 sysroot（静态 `libz.a`）。所有内容只会落到 `build-win/deps` 一次；`--clean`
会保留它。不需要 OpenSSL。

内容：`evobox.exe`（GUI 子系统，`musicbox.ico` 作为应用 / 窗口图标，UTF-8 代码页 + 按显示器 DPI 的
manifest，版本信息）、`avcodec-62.dll avformat-62.dll avutil-60.dll swresample-6.dll swscale-9.dll`、
`libcurl-impersonate.dll`、`libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll`、`cacert.pem`、
`assets/`、`fonts/`、`tiktok-js/`、`README.md`、`README.zh-CN.md`、`LICENSES.txt`。

**安装程序**（NSIS 3，Modern UI，solid LZMA）安装到 `%ProgramFiles%\EvoMusicBox`，创建开始菜单项
（以及可选的桌面快捷方式），注册 `.liv` 演出文件使双击即可在 EvoMusicBox 中打开，在"添加/删除程序"中
添加条目并附带卸载程序（会移除它添加的一切 —— 文件关联仅在仍指向 EvoMusicBox 时移除），完成页提供
"运行 EvoMusicBox"。`EvoMusicBox-<version>-win64.exe /S` 为静默安装。**zip** 无需安装程序也不写注册表：
解压到任意位置即可运行。配置位于 `%APPDATA%\EvoMusicBox`，缓存 / 工作目录位于 `%LOCALAPPDATA%\EvoMusicBox`。
冒烟运行的参数可在控制台使用（`evobox.exe --new --demo --frames 60 --screenshot shot.png`），在 `wine`
下同样可用。`-DEVOBOX_ICON=path.ico` 可更换图标（含 16/32/48/256 px 多尺寸的 .ico 在资源管理器中比当前
的 32 px 图标更清晰）。

如果你偏好手动步骤：

```bash
# 不带 TikTok（无额外依赖）；或先运行一次 tools/windows/build.sh 再指向它准备好的依赖：
#   -DEVOBOX_WITH_TIKTOK=ON -DEVOBOX_DEPS_ROOT=$PWD/build-win/deps \
#   -DCMAKE_PREFIX_PATH=$PWD/build-win/deps/protobuf -DCURL_IMPERSONATE_LOCAL_DIR=$PWD/build-win/deps/curl-impersonate
cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/x86_64-w64-mingw32.cmake \
      -DFFMPEG_ROOT=$PWD/build-win/deps/ffmpeg -DEVOBOX_WITH_TIKTOK=OFF -DCMAKE_BUILD_TYPE=Release
ninja -C build-win && ctest --test-dir build-win          # 测试通过 wine 运行
cpack --config build-win/CPackConfig.cmake -B build-win/dist   # zip + 安装程序（makensis 需在 PATH 中）
```

Windows 上尚未实现：崩溃报告（`CrashHandler` 仅支持 POSIX）。TikTok 客户端可以构建，礼物画廊也能工作
（Simulate、OSC），但尚未从 Windows 连接过真实的 TikTok LIVE 直播间 —— 见 `HANDOVER.md` §7.1。

## 运行

```bash
./build/evobox                       # 空项目（或重新打开上一个项目）
./build/evobox MyShow.liv            # 打开演出文件（旧式的 MyShow.evobox 文件夹同样可用）
./build/evobox --demo                # 生成演示音频并导入（首次运行导览）
./build/evobox --new --import song.mp3 --import clip.mp4   # 从空项目开始并添加声音
```

常用参数：`--no-audio`（空音频后端）、`--demo-gifts`（离线礼物目录）、`--verbose`（把日志镜像到
stderr）、`--frames N --screenshot out.png`（无头冒烟运行，以 60 Hz 步进）、`--save-as Show.liv`
（冒烟运行结束时保存）、`--new`、`--import <file>`、`--performance`。

键盘：`Space` 切换所选 tile（再次触发音效）、`Enter` 播放、`Esc` 停止一切并取消待执行的 OSC 定时器、
`Ctrl+Space` 模拟所选礼物 / 直播间事件（或从 Clip Editor 播放片段选区 —— 与 tile 使用同一个 voice）、
`F11` 演出模式、`Ctrl+G` 切换 Sounds/Gifts、`Ctrl+F` 搜索、`Ctrl+Z/Shift+Z` 撤销/重做、
`Ctrl+S/Shift+S/O/N/I` 保存/另存为/打开/新建/导入、`Ctrl+Shift+O` 合并另一个演出、`Ctrl+1..9` 已保存的
布局、方向键移动 tile 选择、`Delete` 删除。

### 声音：音乐与音效

拖入或导入的媒体会立刻成为一个 tile（整个文件；之后可在 Clip Editor 中裁剪）。每个声音要么是
**音乐**（默认：启动它会停止正在播放的音乐），要么是**音效**（Inspector 中的复选框 / tile 右键菜单：
叠加在一切之上并可堆叠 —— 按三次就听到三次）。`Settings > Playback policy` 可切换为 "Overlap everything"
或 "Stop others"。tile 可以拖到 Navigator 的分类行上以移动它；tile 的贴纸可以是图片：任意图片文件，
或从导入视频中提取的一帧。

### 演出文件

`Save` 会写出一个 **`MyShow.liv`** 文件 —— 一个 zip 容器，包含在任何地方重新加载演出所需的一切：
`project.json`（分类、声音及其 OSC 命令、礼物动作、直播间事件、OSC 目标、设置）、`clips/*.mp3`
（渲染后的声音）与 `icons/*.png`（图片贴纸）。源媒体与视频永远不会被包含。打开 `.liv` 时会把它解压到
缓存目录下的一个工作文件夹；保存时再把该文件夹打包回文件。

**File → Merge show…** 把另一个 `.liv`（或 `.evobox`）加入当前演出：其声音及其片段、图片与分类，以及 ——
如果在对话框中选择 "Sounds and OSC" —— 其 OSC 目标、声音的命令、礼物与直播间事件动作。已存在的分类与目标
（同名 / 同一 host:port）会被复用，已配置的礼物保留原有动作；整个合并是一步撤销。

渲染后的片段是 **MP3**（通过系统 FFmpeg 的 libmp3lame，VBR 质量 2 ≈ 190 kbit/s，无缝（gapless）以保证
片段精确的长度）：一首 3 分钟的立体声歌曲在演出文件中约占 4 MB，而非约 70 MB 的 float PCM。早期版本保存的
演出包含 float32 的 `clips/*.wav`；它们可以正常打开，加载演出时每个片段会在后台转换为 MP3（不需要源媒体），
下一次 Save 会写出更小的文件。如果所用的 FFmpeg 没有 MP3 编码器，片段会回退为 float32 WAV。

```
MyShow.liv  (zip)                 MyShow.evobox/   （旧式文件夹布局，仍可读取）
├── project.json                  ├── project.json
├── clips/000012.mp3              ├── clips/000012.mp3      渲染后的片段（MP3；旧演出：float32 WAV）
└── icons/3f9a…c1.png             ├── icons/…               图片贴纸
                                  ├── media/                可选的源文件副本（Settings）
                                  └── autosave/project.json 轮转的自动保存
```

与机器相关的设置（`prefs.json`、`imgui.ini`、布局、礼物目录 + 图标缓存、`.liv` 工作文件夹）位于用户的
配置/缓存目录（Linux 上为 `~/.config/EvoMusicBox`、`~/.cache/EvoMusicBox`）。

### OSC

命令是纯文本：`/address arg arg ...` —— 整数成为 `i`，小数成为 `f`，`true/false` 成为 `T/F`，其余均为
`s`（含空格的字符串请加引号）；`i: f: s: b: h: d:` 前缀可强制指定类型。每条命令从地址簿中选择一个目标
（Localhost `127.0.0.1:8000` 始终存在且为默认）。可用 `python3 tools/osc_listen.py 8000` 测试。

## 工具

- `tools/liv_export_mp3.py Show.liv [-o DIR]` —— 每个声音导出一个 MP3，**只包含为按钮选中的那部分**
  （渲染后的片段：裁剪、增益、归一化、淡入淡出）。选项：`--numbered`（`01 - Name.mp3`）、
  `--by-category`、`--music-only` / `--effects-only`、`--category NAME`、`--bitrate 256k` 或 `--vbr 2`、
  `--format mp3|m4a|ogg|flac|wav`、`--dry-run`。需要 `ffmpeg` 命令行工具。文件会写入标签（标题、
  专辑 = 演出名、流派 = 分类、曲目号 = tile 顺序）。MP3 片段导出为 MP3 时原样复制（不做第二次有损编码），
  除非指定 `--reencode`。演出中缺少片段的声音，会在其原始媒体仍然可用时从中裁出。
- `tools/osc_listen.py [port]` —— 打印收到的 OSC 消息。
- `tools/fake_live_feed.py` —— 用于脚本化的合成直播事件流（JSON 行）。
- `tools/make_test_media.sh [dir]` —— 通过 ffmpeg 命令行生成 wav/mp3/mp4/webp 测试文件。
- `tools/gen_phosphor_icons.py` —— 重新生成图标头文件（由 CMake 调用）。

## 目录结构

```
src/app     Application、各控制器（Trigger、Playback、Import）、Prefs、ProjectIO、Demo
src/model   Project、Sound、Category、GiftAction、RoomEventAction、OscActions/Phase/Command、OscTarget
src/media   FFmpegDecoder、ImageDecoder、ImageWriter、VideoFrames、ClipRenderer、ClipEncoder（MP3）、MediaLibrary、MediaService（工作线程池）
src/audio   AudioEngine（ma_engine voices）+ PreviewPlayer
src/osc     OSC 1.0 编码器、命令解析器、端点、UDP 发送器、调度线程、OscService
src/live    LiveEvent、GiftCatalog、LiveEventRouter、TikTokLiveService、IconFetcher
src/ui      Theme、Fonts、Icons、Shell（菜单/状态栏/对话框/快捷键）、panels/、widgets/
src/util    队列、工作线程池、SPSC 环形缓冲、哈希、路径、字符串、时间格式化、zip（miniz）
tests/      doctest 测试套件（osc parser、model round trip、trigger controller、live router、
            scheduler、decoder smoke、clip renderer、audio engine、playback policy、project io）
```

与 `docs/architecture.md` 的偏差：OSC 使用源码内的一个小型 OSC 1.0 编码器 + `sendto`
（`src/osc/OscMessage.*`，文档允许的一种方案），而没有内置 oscpack。

## 许可

EvoMusicBox 自身代码的许可证尚未选定（没有 `LICENSE` 文件：在添加之前保留所有权利）。第三方组件保持各自的
许可证 —— 它们及其条款列于 `windows/LICENSES.txt`（也随每个安装包一同发布）。
