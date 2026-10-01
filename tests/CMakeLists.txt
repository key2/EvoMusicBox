# Headless unit tests (doctest). Each test is one executable linked against evobox_core.
add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE ${CMAKE_SOURCE_DIR}/third_party/doctest)

# Cross-compiled Windows tests run through Wine (CMAKE_CROSSCOMPILING_EMULATOR): the scratch dir
# must be a Windows path (Z: maps the Linux root) and the DLLs staged next to evobox.exe must be
# on the DLL search path.
set(EVOBOX_TEST_SCRATCH "${CMAKE_CURRENT_BINARY_DIR}")
if(CMAKE_CROSSCOMPILING AND WIN32)
    set(EVOBOX_TEST_SCRATCH "Z:${CMAKE_CURRENT_BINARY_DIR}")
endif()

function(evobox_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE evobox_core doctest)
    target_compile_definitions(${name} PRIVATE EVOBOX_TEST_DIR="${EVOBOX_TEST_SCRATCH}")
    add_test(NAME ${name} COMMAND ${name})
    if(CMAKE_CROSSCOMPILING AND WIN32)
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "WINEPATH=Z:${CMAKE_BINARY_DIR};WINEDEBUG=-all")
    endif()
endfunction()

evobox_add_test(osc_parser_test osc_parser_test.cpp)
evobox_add_test(model_roundtrip_test model_roundtrip_test.cpp)
evobox_add_test(trigger_controller_test trigger_controller_test.cpp)
evobox_add_test(live_router_test live_router_test.cpp)
evobox_add_test(scheduler_test scheduler_test.cpp)
evobox_add_test(decoder_smoke_test decoder_smoke_test.cpp)
evobox_add_test(clip_renderer_test clip_renderer_test.cpp)
evobox_add_test(audio_engine_test audio_engine_test.cpp)
evobox_add_test(playback_policy_test playback_policy_test.cpp)
evobox_add_test(project_io_test project_io_test.cpp)
evobox_add_test(project_merge_test project_merge_test.cpp)
