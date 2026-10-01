# FindFFmpeg.cmake — locates the FFmpeg libraries and creates imported targets
#   FFmpeg::avformat  FFmpeg::avcodec  FFmpeg::avutil  FFmpeg::swresample  FFmpeg::swscale
#
# Strategy:
#   1. pkg-config (Linux / macOS Homebrew / MSYS2)
#   2. vcpkg / manual: FFMPEG_ROOT hint, find_path + find_library per component
#
# Usage: find_package(FFmpeg REQUIRED COMPONENTS avformat avcodec avutil swresample swscale)

include(FindPackageHandleStandardArgs)

if(NOT FFmpeg_FIND_COMPONENTS)
    set(FFmpeg_FIND_COMPONENTS avformat avcodec avutil swresample swscale)
endif()

# pkg-config describes the HOST's FFmpeg; when cross-compiling only an explicit cross pkg-config
# (PKG_CONFIG_EXECUTABLE / PKG_CONFIG_LIBDIR) may be trusted, otherwise FFMPEG_ROOT is used.
if(NOT CMAKE_CROSSCOMPILING OR DEFINED ENV{PKG_CONFIG_LIBDIR} OR DEFINED PKG_CONFIG_EXECUTABLE)
    find_package(PkgConfig QUIET)
endif()

set(_ffmpeg_all_found TRUE)
set(_ffmpeg_libs)
foreach(_comp IN LISTS FFmpeg_FIND_COMPONENTS)
    set(_pc_name "lib${_comp}")
    set(_found FALSE)

    if(PKG_CONFIG_FOUND)
        pkg_check_modules(PC_${_comp} QUIET ${_pc_name})
    endif()

    if(PC_${_comp}_FOUND)
        find_path(FFmpeg_${_comp}_INCLUDE_DIR
            NAMES lib${_comp}/${_comp}.h
            HINTS ${PC_${_comp}_INCLUDE_DIRS} ${PC_${_comp}_INCLUDEDIR})
        find_library(FFmpeg_${_comp}_LIBRARY
            NAMES ${_comp}
            HINTS ${PC_${_comp}_LIBRARY_DIRS} ${PC_${_comp}_LIBDIR})
        set(FFmpeg_${_comp}_VERSION ${PC_${_comp}_VERSION})
    else()
        find_path(FFmpeg_${_comp}_INCLUDE_DIR
            NAMES lib${_comp}/${_comp}.h
            HINTS ${FFMPEG_ROOT} $ENV{FFMPEG_ROOT}
            PATH_SUFFIXES include)
        find_library(FFmpeg_${_comp}_LIBRARY
            NAMES ${_comp} lib${_comp}
            HINTS ${FFMPEG_ROOT} $ENV{FFMPEG_ROOT}
            PATH_SUFFIXES lib bin)
    endif()

    if(FFmpeg_${_comp}_INCLUDE_DIR AND FFmpeg_${_comp}_LIBRARY)
        set(_found TRUE)
        if(NOT TARGET FFmpeg::${_comp})
            add_library(FFmpeg::${_comp} UNKNOWN IMPORTED)
            set_target_properties(FFmpeg::${_comp} PROPERTIES
                IMPORTED_LOCATION "${FFmpeg_${_comp}_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_${_comp}_INCLUDE_DIR}")
            if(PC_${_comp}_FOUND AND PC_${_comp}_CFLAGS_OTHER)
                set_property(TARGET FFmpeg::${_comp} APPEND PROPERTY
                    INTERFACE_COMPILE_OPTIONS "${PC_${_comp}_CFLAGS_OTHER}")
            endif()
        endif()
        list(APPEND _ffmpeg_libs ${FFmpeg_${_comp}_LIBRARY})
    endif()

    set(FFmpeg_${_comp}_FOUND ${_found})
    if(NOT _found)
        set(_ffmpeg_all_found FALSE)
    endif()
    mark_as_advanced(FFmpeg_${_comp}_INCLUDE_DIR FFmpeg_${_comp}_LIBRARY)
endforeach()

# component inter-dependencies (link order for static builds)
if(TARGET FFmpeg::avformat AND TARGET FFmpeg::avcodec)
    set_property(TARGET FFmpeg::avformat APPEND PROPERTY INTERFACE_LINK_LIBRARIES FFmpeg::avcodec)
endif()
if(TARGET FFmpeg::avcodec AND TARGET FFmpeg::avutil)
    set_property(TARGET FFmpeg::avcodec APPEND PROPERTY INTERFACE_LINK_LIBRARIES FFmpeg::avutil)
endif()
if(TARGET FFmpeg::swresample AND TARGET FFmpeg::avutil)
    set_property(TARGET FFmpeg::swresample APPEND PROPERTY INTERFACE_LINK_LIBRARIES FFmpeg::avutil)
endif()
if(TARGET FFmpeg::swscale AND TARGET FFmpeg::avutil)
    set_property(TARGET FFmpeg::swscale APPEND PROPERTY INTERFACE_LINK_LIBRARIES FFmpeg::avutil)
endif()

set(FFmpeg_LIBRARIES ${_ffmpeg_libs})
if(FFmpeg_avutil_VERSION)
    set(FFmpeg_VERSION ${FFmpeg_avutil_VERSION})
endif()

find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS _ffmpeg_all_found
    VERSION_VAR FFmpeg_VERSION
    HANDLE_COMPONENTS
    FAIL_MESSAGE "FFmpeg libraries not found. Install libavformat/libavcodec/libavutil/libswresample/libswscale dev packages (pkg-config) or set FFMPEG_ROOT.")
