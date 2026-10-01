# Package.cmake — install rules + CPack: the distributable.
#
#   Windows (mingw cross build, see cmake/toolchains/x86_64-w64-mingw32.cmake):
#     EvoMusicBox-<version>-win64.zip
#     └── EvoMusicBox-<version>-win64/
#         ├── evobox.exe                 GUI subsystem, UTF-8 manifest
#         ├── avcodec-62.dll ...         the FFmpeg DLLs behind the import libraries we link
#         ├── libcurl-impersonate.dll    TikTok client transport (with TikTok)
#         ├── libstdc++-6.dll, libgcc_s_seh-1.dll, libwinpthread-1.dll   GCC runtime
#         ├── assets/  fonts/  (tiktok-js/ cacert.pem with TikTok)
#         └── README.md  README.zh-CN.md  LICENSES.txt
#   Linux: the same layout as a .tar.gz (cpack -G TGZ), without the DLL rules.
#
# `cmake --install build-win --prefix dist/EvoMusicBox --strip` or `cpack --config
# build-win/CPackConfig.cmake` (tools/windows/build.sh does the whole thing). The runtime DLLs are
# also copied next to the build-tree executable so `wine build-win/evobox.exe` and the tests work
# without installing.

install(TARGETS evobox RUNTIME DESTINATION .)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/ DESTINATION assets)
install(FILES
    ${CMAKE_SOURCE_DIR}/third_party/web/src/regular/Phosphor.ttf
    ${CMAKE_SOURCE_DIR}/third_party/web/src/fill/Phosphor-Fill.ttf
    ${IMGUI_DIR}/misc/fonts/Roboto-Medium.ttf
    DESTINATION fonts)
install(FILES ${CMAKE_SOURCE_DIR}/README.md ${CMAKE_SOURCE_DIR}/README.zh-CN.md DESTINATION .)
if(EXISTS ${CMAKE_SOURCE_DIR}/windows/LICENSES.txt)
    install(FILES ${CMAKE_SOURCE_DIR}/windows/LICENSES.txt DESTINATION .)
endif()
if(EVOBOX_WITH_TIKTOK)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/third_party/ttlive-cpp/js/ DESTINATION tiktok-js)
    if(WIN32 AND DEFINED CURL_IMPERSONATE_ROOT AND EXISTS ${CURL_IMPERSONATE_ROOT}/cacert.pem)
        install(FILES ${CURL_IMPERSONATE_ROOT}/cacert.pem DESTINATION .)
    endif()
endif()

set(EVOBOX_RUNTIME_DLLS "")
if(WIN32)
    # FFmpeg: the DLL of each component we link, found next to its import library
    # (<root>/lib/libavcodec.dll.a -> <root>/bin/avcodec-62.dll)
    foreach(_comp avformat avcodec avutil swresample swscale)
        get_target_property(_implib FFmpeg::${_comp} IMPORTED_LOCATION)
        if(_implib)
            get_filename_component(_libdir ${_implib} DIRECTORY)
            file(GLOB _dll ${_libdir}/../bin/${_comp}-*.dll ${_libdir}/${_comp}-*.dll ${_libdir}/../bin/lib${_comp}-*.dll)
            if(_dll)
                list(GET _dll 0 _dll)
                list(APPEND EVOBOX_RUNTIME_DLLS ${_dll})
            else()
                message(WARNING "Package: no DLL found for FFmpeg::${_comp} near ${_implib}")
            endif()
        endif()
    endforeach()
    # TikTok: the curl-impersonate DLL behind the import library ttlive and IconFetcher link
    # (<dir>/libcurl-impersonate.dll.a -> <dir>/libcurl-impersonate.dll, see tools/windows/build.sh)
    if(EVOBOX_WITH_TIKTOK AND TARGET curl_impersonate::curl_impersonate)
        get_target_property(_implib curl_impersonate::curl_impersonate IMPORTED_LOCATION)
        if(_implib AND _implib MATCHES "\\.(dll\\.a|lib)$")
            get_filename_component(_libdir ${_implib} DIRECTORY)
            file(GLOB _dll ${_libdir}/libcurl-impersonate*.dll ${_libdir}/../bin/libcurl-impersonate*.dll)
            if(_dll)
                list(GET _dll 0 _dll)
                list(APPEND EVOBOX_RUNTIME_DLLS ${_dll})
            else()
                message(WARNING "Package: no libcurl-impersonate*.dll found near ${_implib}")
            endif()
        endif()
    endif()
    # GCC runtime (mingw-w64 posix threads): asked from the compiler, so the matching versions ship
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        foreach(_dll libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
            execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=${_dll}
                            OUTPUT_VARIABLE _path OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(_path AND IS_ABSOLUTE "${_path}" AND EXISTS "${_path}")
                list(APPEND EVOBOX_RUNTIME_DLLS ${_path})
            else()
                message(WARNING "Package: ${_dll} not found via ${CMAKE_CXX_COMPILER} -print-file-name")
            endif()
        endforeach()
    endif()
    if(EVOBOX_RUNTIME_DLLS)
        install(FILES ${EVOBOX_RUNTIME_DLLS} DESTINATION .)
        add_custom_command(TARGET evobox POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EVOBOX_RUNTIME_DLLS} $<TARGET_FILE_DIR:evobox>
            COMMENT "Staging runtime DLLs next to evobox.exe"
            VERBATIM)
    endif()
endif()
set(EVOBOX_RUNTIME_DLLS ${EVOBOX_RUNTIME_DLLS} CACHE INTERNAL "runtime DLLs shipped with evobox.exe")

# ---- CPack
set(CPACK_PACKAGE_NAME "EvoMusicBox")
set(CPACK_PACKAGE_VENDOR "EvoMusicBox")
set(CPACK_PACKAGE_VERSION "${EVOBOX_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Live soundboard with OSC triggers")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "EvoMusicBox")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/windows/LICENSES.txt")
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_SYSTEM_NAME "win64")
    # NSIS installer (EvoMusicBox-<version>-win64.exe) when makensis is available. On Linux the
    # Debian "nsis" package works, also extracted locally (tools/windows/build.sh does that and
    # points CPACK_NSIS_EXECUTABLE / NSISDIR at it).
    find_program(CPACK_NSIS_EXECUTABLE NAMES makensis HINTS ${EVOBOX_NSIS_ROOT}/usr/bin ${EVOBOX_NSIS_ROOT}/bin)
    if(CPACK_NSIS_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "NSIS")
        message(STATUS "Package: NSIS installer via ${CPACK_NSIS_EXECUTABLE}")
    else()
        message(STATUS "Package: makensis not found, zip only (install the nsis package for an installer)")
    endif()
    set(CPACK_NSIS_PACKAGE_NAME "EvoMusicBox")
    set(CPACK_NSIS_DISPLAY_NAME "EvoMusicBox ${EVOBOX_VERSION}")
    set(CPACK_NSIS_UNINSTALL_NAME "Uninstall EvoMusicBox")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")              # evobox.exe sits at the install root
    set(CPACK_PACKAGE_EXECUTABLES "evobox" "EvoMusicBox")  # Start Menu shortcut
    set(CPACK_CREATE_DESKTOP_LINKS "evobox")               # optional desktop shortcut (checkbox)
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "evobox.exe")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_MANIFEST_DPI_AWARE ON)
    set(CPACK_NSIS_COMPRESSOR "/SOLID lzma")
    if(EXISTS "${EVOBOX_ICON}")
        set(CPACK_NSIS_MUI_ICON "${EVOBOX_ICON}")
        set(CPACK_NSIS_MUI_UNIICON "${EVOBOX_ICON}")
        set(CPACK_NSIS_INSTALLED_ICON_NAME "evobox.exe")   # Add/Remove Programs entry
    endif()
    # .liv show files: double-click opens them in EvoMusicBox (the app takes the path as argv[1]);
    # the association is removed on uninstall only if it still points at this ProgId
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
  WriteRegStr HKCR '.liv' '' 'EvoMusicBox.Show'
  WriteRegStr HKCR '.liv' 'Content Type' 'application/zip'
  WriteRegStr HKCR 'EvoMusicBox.Show' '' 'EvoMusicBox show'
  WriteRegStr HKCR 'EvoMusicBox.Show\\DefaultIcon' '' '$INSTDIR\\evobox.exe,0'
  WriteRegStr HKCR 'EvoMusicBox.Show\\shell\\open\\command' '' '\"$INSTDIR\\evobox.exe\" \"%1\"'
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
  ReadRegStr $0 HKCR '.liv' ''
  StrCmp $0 'EvoMusicBox.Show' 0 +2
    DeleteRegKey HKCR '.liv'
  DeleteRegKey HKCR 'EvoMusicBox.Show'
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'")
else()
    set(CPACK_GENERATOR "TGZ")
    string(TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" CPACK_SYSTEM_NAME)
endif()
set(CPACK_PACKAGE_FILE_NAME "EvoMusicBox-${EVOBOX_VERSION}-${CPACK_SYSTEM_NAME}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON) # the zip / tgz unpack into one folder...
set(CPACK_STRIP_FILES ON)
set(CPACK_VERBATIM_VARIABLES ON)
# ...while the installer puts the files straight into $INSTDIR (per-generator setting)
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_BINARY_DIR}/CPackProjectConfig.cmake")
file(WRITE ${CPACK_PROJECT_CONFIG_FILE}
     "if(CPACK_GENERATOR STREQUAL \"NSIS\")\n  set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)\nendif()\n")
include(CPack)
