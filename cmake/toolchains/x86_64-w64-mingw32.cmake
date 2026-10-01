# x86_64-w64-mingw32.cmake — cross-compile EvoMusicBox for Windows x64 from Linux with mingw-w64.
#
#   cmake -S . -B build-win -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/x86_64-w64-mingw32.cmake \
#         -DFFMPEG_ROOT=$PWD/build-win/deps/ffmpeg \
#         -DEVOBOX_DEPS_ROOT=$PWD/build-win/deps -DCMAKE_PREFIX_PATH=$PWD/build-win/deps/protobuf \
#         -DCURL_IMPERSONATE_LOCAL_DIR=$PWD/build-win/deps/curl-impersonate      # (TikTok ON)
#
# tools/windows/build.sh runs the whole thing (dependencies, build, tests under Wine, zip) and
# prepares the TikTok dependencies under build-win/deps (or pass -DEVOBOX_WITH_TIKTOK=OFF).
#
# The *-posix thread variant is required (std::thread / std::mutex via winpthreads). Tests and
# the executable can be run from Linux through Wine: CMAKE_CROSSCOMPILING_EMULATOR picks up
# `wine` when it is installed (override with -DEVOBOX_WINE=... or -DEVOBOX_WINE=OFF).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_evobox_triplet x86_64-w64-mingw32)
find_program(CMAKE_C_COMPILER   NAMES ${_evobox_triplet}-gcc-posix ${_evobox_triplet}-gcc REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES ${_evobox_triplet}-g++-posix ${_evobox_triplet}-g++ REQUIRED)
find_program(CMAKE_RC_COMPILER  NAMES ${_evobox_triplet}-windres REQUIRED)
find_program(CMAKE_STRIP        NAMES ${_evobox_triplet}-strip)
find_program(CMAKE_AR           NAMES ${_evobox_triplet}-gcc-ar ${_evobox_triplet}-ar)
find_program(CMAKE_RANLIB       NAMES ${_evobox_triplet}-gcc-ranlib ${_evobox_triplet}-ranlib)

# where the target's headers/libraries live (the Debian/Ubuntu/Arch/Fedora mingw-w64 sysroot), plus
# the prefixes of downloaded / cross-built dependencies: FFMPEG_ROOT, and EVOBOX_DEPS_ROOT
# (tools/windows/build.sh: build-win/deps, holding the protobuf prefix and the curl-impersonate
# directory of the TikTok client). Search paths below one of these roots are used as they are.
set(CMAKE_FIND_ROOT_PATH /usr/${_evobox_triplet} /usr/${_evobox_triplet}/sys-root/mingw)
foreach(_root FFMPEG_ROOT EVOBOX_DEPS_ROOT)
    if(DEFINED ${_root})
        list(APPEND CMAKE_FIND_ROOT_PATH ${${_root}})
    endif()
endforeach()
# programs (python for the icon header, protoc...) come from the host, libraries from the target
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# zlib (ttlive gunzips the WebSocket payloads): the sysroot's static libz.a, so no zlib1.dll ships
set(ZLIB_USE_STATIC_LIBS ON)

# run test binaries through Wine (ctest honours this)
if(NOT DEFINED EVOBOX_WINE)
    find_program(EVOBOX_WINE NAMES wine64 wine)
endif()
if(EVOBOX_WINE)
    set(CMAKE_CROSSCOMPILING_EMULATOR ${EVOBOX_WINE})
endif()
