cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW) # FFMPEG_ROOT is a search prefix for find_package(FFmpeg)
endif()
project(EvoMusicBox LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Must be set BEFORE add_subdirectory(ttlive-cpp): it forces Release otherwise.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

option(EVOBOX_WITH_TIKTOK "Build the TikTok LIVE trigger source (ttlive-cpp)" ON)
option(EVOBOX_WITH_EMOJI  "Colour emoji stickers via imgui_freetype"        OFF)
option(EVOBOX_BUILD_TESTS "Build unit tests"                                ON)
option(EVOBOX_WARNINGS    "Enable extra compiler warnings for app sources"  ON)
option(EVOBOX_VENDORED_GLFW "Build GLFW from third_party/glfw instead of the system library" ON)

# ---------------------------------------------------------------- repository preparation checks
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
if(NOT EXISTS ${IMGUI_DIR}/imgui.h)
    message(FATAL_ERROR "third_party/imgui is missing. See README.md (repository preparation).")
endif()
file(STRINGS ${IMGUI_DIR}/imgui.h _evobox_dock REGEX "ImGuiConfigFlags_DockingEnable")
if(_evobox_dock STREQUAL "")
    message(FATAL_ERROR
        "third_party/imgui is not on the *docking* branch (imgui_organic requires it).\n"
        "Run:\n"
        "    git -C third_party/imgui checkout 9b4eb24\n"
        "(or: git -C third_party/imgui checkout docking)")
endif()

# ---------------------------------------------------------------- 1. Dear ImGui (docking) — OUR target, created before imgui_organic
# NOTE: misc/cpp/imgui_stdlib.cpp is compiled by imgui_organic (it appends it to its own
# sources when the parent project provides the 'imgui' target), so it is NOT listed here.
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/misc/cpp ${IMGUI_DIR}/backends)
# IGFD forces IMGUI_DEFINE_MATH_OPERATORS; keep every TU consistent.
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
if(EVOBOX_WITH_EMOJI)
    find_package(Freetype REQUIRED)
    target_sources(imgui PRIVATE ${IMGUI_DIR}/misc/freetype/imgui_freetype.cpp)
    target_include_directories(imgui PUBLIC ${IMGUI_DIR}/misc/freetype)
    target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_FREETYPE IMGUI_USE_WCHAR32)
    target_link_libraries(imgui PUBLIC Freetype::Freetype)
endif()

# GLFW: the vendored copy (third_party/glfw, pinned to 3.5.1) is preferred over the system library.
# Distro GLFW 3.4 has a NULL dereference in its Wayland drag & drop "enter" handler (fixed upstream
# in 3.5.1 by 51b6434): dragging files across the window decorations segfaults the app.
set(EVOBOX_GLFW_VENDORED OFF)
if(EVOBOX_VENDORED_GLFW AND EXISTS ${CMAKE_SOURCE_DIR}/third_party/glfw/CMakeLists.txt)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    set(GLFW_LIBRARY_TYPE "STATIC" CACHE STRING "" FORCE)
    add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)
    set(EVOBOX_GLFW_VENDORED ON)
    message(STATUS "GLFW: vendored third_party/glfw (3.5.1)")
else()
    find_package(glfw3 3.3 QUIET)
    if(NOT glfw3_FOUND AND NOT TARGET glfw)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)
        add_library(glfw ALIAS PkgConfig::GLFW)
    endif()
    message(WARNING "GLFW: using the SYSTEM library. GLFW <= 3.4 crashes on Wayland file drops over the "
                    "window decorations; the app then falls back to X11 (XWayland) on Wayland sessions. "
                    "Clone https://github.com/glfw/glfw (tag 3.5.1) into third_party/glfw to build the fixed version.")
endif()
find_package(OpenGL REQUIRED)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

# ---------------------------------------------------------------- 2. imgui_organic -> organic::organic
set(ORGANIC_IMGUI_DIR ${IMGUI_DIR} CACHE PATH "" FORCE)
set(ORGANIC_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(ORGANIC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/imgui_organic)

# ---------------------------------------------------------------- 3. ImGuiFileDialog (links OUR imgui)
# src/ui/IGFDGlue.cpp translates the dialog's texts at draw time (the config routes every button
# and string through it); it depends on ImGui only, so it lives in this target and avoids a
# static-library cycle with evobox_core, which installs the translator (ui/I18n.cpp)
add_library(ImGuiFileDialog STATIC third_party/ImGuiFileDialog/ImGuiFileDialog.cpp src/ui/IGFDGlue.cpp)
target_include_directories(ImGuiFileDialog PUBLIC third_party/ImGuiFileDialog)
target_compile_definitions(ImGuiFileDialog PUBLIC
    CUSTOM_IMGUIFILEDIALOG_CONFIG="${CMAKE_SOURCE_DIR}/src/ui/IGFDConfig.h")
target_include_directories(ImGuiFileDialog PRIVATE ${CMAKE_BINARY_DIR}/generated ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ImGuiFileDialog PUBLIC imgui)

# ---------------------------------------------------------------- 4. FFmpeg (system, dynamic)
find_package(FFmpeg REQUIRED COMPONENTS avformat avcodec avutil swresample swscale)

# ---------------------------------------------------------------- 5. OSC
# A small in-tree OSC 1.0 encoder + UDP sender (src/osc) — no external library needed.

# ---------------------------------------------------------------- 5b. miniz (zip container for .liv show files)
add_library(miniz STATIC third_party/miniz/miniz.c)
target_include_directories(miniz PUBLIC third_party/miniz)
set_target_properties(miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------- 6. TikTok (optional)
if(EVOBOX_WITH_TIKTOK)
    if(NOT EXISTS ${CMAKE_SOURCE_DIR}/third_party/ttlive-cpp/third_party/quickjs/quickjs.c)
        message(FATAL_ERROR
            "ttlive-cpp's QuickJS submodule is empty. Run:\n"
            "    git -C third_party/ttlive-cpp submodule update --init --recursive\n"
            "or configure with -DEVOBOX_WITH_TIKTOK=OFF")
    endif()
    add_subdirectory(third_party/ttlive-cpp)
    if(TARGET ttlive_demo)
        set_target_properties(ttlive_demo PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
    if(MINGW)
        # Windows cross build (cmake/toolchains/x86_64-w64-mingw32.cmake, tools/windows/build.sh):
        # unoptimised, the generated tiktok.pb.cc (656 messages) has ~77 K COMDAT sections, past
        # the 32 K limit of a plain COFF object ("too many sections"; -O3 inlines them down to ~100),
        # and QuickJS expects mingw's C99 printf family (upstream's CONFIG_WIN32 build sets the same
        # define; its CMake only adds _GNU_SOURCE on non-Windows).
        target_compile_options(ttlive PRIVATE -Wa,-mbig-obj)
        target_compile_definitions(quickjs PRIVATE __USE_MINGW_ANSI_STDIO=1)
    endif()
endif()

# ---------------------------------------------------------------- 7. Generated Phosphor icon header
include(Phosphor)
add_dependencies(ImGuiFileDialog evobox_phosphor_header) # IGFDConfig.h includes IconsPhosphor.h

# ---------------------------------------------------------------- 8. Application library + executable
# Everything except main.cpp goes into a static library so tests can link it.
file(GLOB_RECURSE EVOBOX_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/app/*.cpp
    ${CMAKE_SOURCE_DIR}/src/model/*.cpp
    ${CMAKE_SOURCE_DIR}/src/media/*.cpp
    ${CMAKE_SOURCE_DIR}/src/audio/*.cpp
    ${CMAKE_SOURCE_DIR}/src/osc/*.cpp
    ${CMAKE_SOURCE_DIR}/src/live/*.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/*.cpp
    ${CMAKE_SOURCE_DIR}/src/util/*.cpp)
if(NOT EVOBOX_WITH_TIKTOK)
    list(FILTER EVOBOX_SOURCES EXCLUDE REGEX ".*/live/TikTokLiveService\\.cpp$")
    list(FILTER EVOBOX_SOURCES EXCLUDE REGEX ".*/live/IconFetcher\\.cpp$")
    list(FILTER EVOBOX_SOURCES EXCLUDE REGEX ".*/ui/panels/GiftGalleryTab\\.cpp$")
    list(FILTER EVOBOX_SOURCES EXCLUDE REGEX ".*/ui/panels/LiveMonitorPanel\\.cpp$")
    list(FILTER EVOBOX_SOURCES EXCLUDE REGEX ".*/ui/widgets/GiftTile\\.cpp$")
endif()

add_library(evobox_core STATIC ${EVOBOX_SOURCES})
target_include_directories(evobox_core PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated)
target_include_directories(evobox_core PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb) # stb_image_write (media/ImageWriter.cpp)
target_link_libraries(evobox_core PUBLIC
    organic::organic ImGuiFileDialog miniz
    FFmpeg::avformat FFmpeg::avcodec FFmpeg::avutil FFmpeg::swresample FFmpeg::swscale)
set(EVOBOX_VERSION 0.1.0)
target_compile_definitions(evobox_core PUBLIC
    EVOBOX_VERSION="${EVOBOX_VERSION}"
    EVOBOX_GLFW_VENDORED=$<BOOL:${EVOBOX_GLFW_VENDORED}>
    EVOBOX_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    EVOBOX_IMGUI_FONT_DIR="${IMGUI_DIR}/misc/fonts")
add_dependencies(evobox_core evobox_phosphor_header)
if(EVOBOX_WITH_TIKTOK)
    # ttlive for the live client; curl_impersonate::curl_impersonate (GLOBAL imported target created by
    # ttlive-cpp's cmake, exposes <curl/curl.h>) for IconFetcher — never link a second libcurl.
    target_link_libraries(evobox_core PUBLIC ttlive curl_impersonate::curl_impersonate)
    target_compile_definitions(evobox_core PUBLIC EVOBOX_WITH_TIKTOK=1)
endif()
if(EVOBOX_WITH_EMOJI)
    target_compile_definitions(evobox_core PUBLIC EVOBOX_WITH_EMOJI=1)
endif()
if(WIN32)
    # ws2_32: OSC UDP sockets; gdi32: static GLFW's WGL/monitor code (its CMake only puts
    # -lgdi32 into the pkg-config file); mingw links kernel32/user32/shell32 by default
    target_link_libraries(evobox_core PUBLIC ws2_32 gdi32)
endif()
if(EVOBOX_WARNINGS AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    target_compile_options(evobox_core PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

add_executable(evobox src/main.cpp)
target_link_libraries(evobox PRIVATE evobox_core)
target_include_directories(evobox PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
if(EVOBOX_WARNINGS AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    target_compile_options(evobox PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
if(WIN32)
    # GUI subsystem (no console window; main.cpp re-attaches to a parent console for the smoke
    # flags) + version info and the UTF-8 active code page manifest (windows/evobox.rc.in)
    set_target_properties(evobox PROPERTIES WIN32_EXECUTABLE ON)
    string(REPLACE "." "," EVOBOX_VERSION_RC "${EVOBOX_VERSION}")
    set(EVOBOX_VERSION_RC "${EVOBOX_VERSION_RC},0")
    set(EVOBOX_RC_MANIFEST "${CMAKE_BINARY_DIR}/generated/windows/evobox.manifest")
    set(EVOBOX_ICON "${CMAKE_SOURCE_DIR}/musicbox.ico" CACHE FILEPATH "Application icon (.ico) for the exe, the installer and .liv files")
    if(EXISTS "${EVOBOX_ICON}")
        set(EVOBOX_RC_ICON_LINE "GLFW_ICON ICON \"${EVOBOX_ICON}\"")
    else()
        set(EVOBOX_RC_ICON_LINE "// (no icon: ${EVOBOX_ICON} not found)")
        message(WARNING "EVOBOX_ICON ${EVOBOX_ICON} not found: building without an application icon")
    endif()
    configure_file(${CMAKE_SOURCE_DIR}/windows/evobox.manifest ${EVOBOX_RC_MANIFEST} @ONLY)
    configure_file(${CMAKE_SOURCE_DIR}/windows/evobox.rc.in ${CMAKE_BINARY_DIR}/generated/windows/evobox.rc @ONLY)
    target_sources(evobox PRIVATE ${CMAKE_BINARY_DIR}/generated/windows/evobox.rc)
endif()

include(Deploy)
include(Package)

if(EVOBOX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
