# Deploy.cmake — post-build copies next to the executable:
#   assets/              (stickers.json, fonts)
#   fonts/Phosphor.ttf, fonts/Phosphor-Fill.ttf
#   tiktok-js/           (ttlive-cpp SDK scripts, loaded at runtime by the QuickJS signer)
#   cacert.pem           (Windows only: curl CA bundle beside the exe)

set(_evobox_bin_dir $<TARGET_FILE_DIR:evobox>)

add_custom_command(TARGET evobox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_evobox_bin_dir}/assets ${_evobox_bin_dir}/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets ${_evobox_bin_dir}/assets
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/third_party/web/src/regular/Phosphor.ttf
            ${_evobox_bin_dir}/fonts/Phosphor.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/third_party/web/src/fill/Phosphor-Fill.ttf
            ${_evobox_bin_dir}/fonts/Phosphor-Fill.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${IMGUI_DIR}/misc/fonts/Roboto-Medium.ttf
            ${_evobox_bin_dir}/fonts/Roboto-Medium.ttf
    COMMENT "Deploying assets and fonts next to evobox"
    VERBATIM)

if(EVOBOX_WITH_TIKTOK)
    add_custom_command(TARGET evobox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/third_party/ttlive-cpp/js ${_evobox_bin_dir}/tiktok-js
        COMMENT "Deploying TikTok SDK scripts (tiktok-js/)"
        VERBATIM)
    if(WIN32 AND DEFINED CURL_IMPERSONATE_ROOT AND EXISTS ${CURL_IMPERSONATE_ROOT}/cacert.pem)
        add_custom_command(TARGET evobox POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CURL_IMPERSONATE_ROOT}/cacert.pem ${_evobox_bin_dir}/cacert.pem
            VERBATIM)
    endif()
endif()
