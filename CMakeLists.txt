cmake_minimum_required(VERSION 3.25)
project(obs-safe-vst3 VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SAFEVST3_BUILD_OBS_PLUGIN "Build the OBS module when libobs is available" ON)
option(SAFEVST3_BUILD_TESTS "Build protocol/layout tests" ON)

if(NOT WIN32)
    message(STATUS "Public-trial runtime is Windows x86-64 only. Building portable tests only.")
endif()

add_library(safevst3_protocol INTERFACE)
target_include_directories(safevst3_protocol INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(WIN32)
    include(FetchContent)
    set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_AUV2_BUILDS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        vst3sdk
        GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
        GIT_TAG v3.8.0_build_66
        GIT_SHALLOW FALSE
        GIT_SUBMODULES base cmake pluginterfaces public.sdk
        GIT_SUBMODULES_RECURSE TRUE
    )
    FetchContent_MakeAvailable(vst3sdk)

    add_library(safevst3_vst3_hosting_runtime STATIC
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp
    )
    target_link_libraries(safevst3_vst3_hosting_runtime PUBLIC sdk_hosting sdk)
    target_compile_definitions(safevst3_vst3_hosting_runtime PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(safevst3_vst3_hosting_runtime PRIVATE /Zc:__cplusplus)
    endif()

    add_library(safevst3_win_ipc STATIC
        src/platform/windows/win_ipc.cpp
        src/platform/windows/editor_ipc.cpp
    )
    target_include_directories(safevst3_win_ipc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(safevst3_win_ipc PUBLIC safevst3_protocol)
    target_compile_definitions(safevst3_win_ipc PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

    add_executable(obs-safe-vst3-host
        src/host/main.cpp
        src/host/native_editor.cpp
        src/host/vst3_engine.cpp
    )
    target_include_directories(obs-safe-vst3-host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(obs-safe-vst3-host PRIVATE safevst3_win_ipc safevst3_vst3_hosting_runtime Avrt)
    target_compile_definitions(obs-safe-vst3-host PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

    add_executable(obs-safe-vst3-scanner src/scanner/main.cpp)
    target_link_libraries(obs-safe-vst3-scanner PRIVATE safevst3_vst3_hosting_runtime)
    target_compile_definitions(obs-safe-vst3-scanner PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

    if(SAFEVST3_BUILD_OBS_PLUGIN)
        find_package(libobs CONFIG QUIET)
        if(libobs_FOUND)
            add_library(obs-safe-vst3 MODULE
                src/obs-plugin/plugin.cpp
                src/obs-plugin/parameter_controls.cpp
            )
            target_include_directories(obs-safe-vst3 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
            target_link_libraries(obs-safe-vst3 PRIVATE OBS::libobs safevst3_win_ipc)
            target_compile_definitions(obs-safe-vst3 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
            set_target_properties(obs-safe-vst3 PROPERTIES PREFIX "" OUTPUT_NAME "obs-safe-vst3")
            add_custom_command(TARGET obs-safe-vst3 POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:obs-safe-vst3-host>
                    $<TARGET_FILE_DIR:obs-safe-vst3>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:obs-safe-vst3-scanner>
                    $<TARGET_FILE_DIR:obs-safe-vst3>
            )
        else()
            message(WARNING "libobs CMake package not found; helper/scanner will build, OBS module will be skipped. Set libobs_DIR to OBS's CMake package directory.")
        endif()
    endif()
endif()

if(SAFEVST3_BUILD_TESTS)
    enable_testing()

    add_executable(parameter-control-test tests/parameter_control.cpp)
    target_link_libraries(parameter-control-test PRIVATE safevst3_protocol)
    add_test(NAME parameter-control COMMAND parameter-control-test)

    add_executable(state-snapshot-test tests/state_snapshot.cpp)
    target_link_libraries(state-snapshot-test PRIVATE safevst3_protocol)
    add_test(NAME state-snapshot COMMAND state-snapshot-test)

    add_executable(protocol-layout-test tests/protocol_layout.cpp)
    target_link_libraries(protocol-layout-test PRIVATE safevst3_protocol)
    add_dependencies(protocol-layout-test parameter-control-test state-snapshot-test)
    add_test(NAME protocol-layout COMMAND protocol-layout-test)
endif()