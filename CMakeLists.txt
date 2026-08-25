cmake_minimum_required(VERSION 3.25)
project(obs-safe-vst3 VERSION 0.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SAFEVST3_BUILD_OBS_PLUGIN "Build the OBS module when libobs is available" ON)
option(SAFEVST3_BUILD_TESTS "Build protocol/layout tests" ON)
option(SAFEVST3_STATIC_MSVC_RUNTIME "Link Windows binaries with the static MSVC runtime" ON)

if(MSVC AND SAFEVST3_STATIC_MSVC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(NOT WIN32)
    message(STATUS "Stable runtime is Windows x86-64 only. Building portable tests only.")
endif()

add_library(safevst3_protocol INTERFACE)
target_include_directories(safevst3_protocol INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(safevst3_source_selection STATIC
    src/obs-plugin/vst3_source_selection.cpp
)
target_include_directories(safevst3_source_selection PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(safevst3_lifecycle_policy STATIC
    src/common/lifecycle_restart_policy.cpp
)
target_include_directories(safevst3_lifecycle_policy PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(safevst3_state_restore_policy STATIC
    src/common/state_restore_policy.cpp
)
target_include_directories(safevst3_state_restore_policy PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(safevst3_latency_restart_transaction STATIC
    src/common/latency_restart_transaction.cpp
)
target_include_directories(safevst3_latency_restart_transaction PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(safevst3_parameter_refresh_transaction STATIC
    src/common/parameter_refresh_transaction.cpp
)
target_include_directories(safevst3_parameter_refresh_transaction PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(safevst3_io_restart_transaction STATIC
    src/common/io_restart_transaction.cpp
)
target_include_directories(safevst3_io_restart_transaction PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(safevst3_reload_component_transaction STATIC
    src/common/reload_component_transaction.cpp
)
target_include_directories(safevst3_reload_component_transaction PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(safevst3_process_context_policy STATIC
    src/common/process_context_policy.cpp
)
target_include_directories(safevst3_process_context_policy PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(WIN32)
    include(FetchContent)
    set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_AUV2_BUILDS OFF CACHE BOOL "" FORCE)
    set(SMTG_USE_STATIC_CRT ${SAFEVST3_STATIC_MSVC_RUNTIME} CACHE BOOL "" FORCE)

    FetchContent_Declare(
        vst3sdk
        GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
        GIT_TAG v3.8.0_build_66
        GIT_SHALLOW FALSE
        GIT_SUBMODULES base cmake pluginterfaces public.sdk
        GIT_SUBMODULES_RECURSE TRUE
    )
    FetchContent_MakeAvailable(vst3sdk)

    # Steinberg exposes SMTG_USE_STATIC_CRT, but also pin the generated static
    # library targets to the same model so CMake and SDK runtime policies cannot
    # diverge if either side changes its defaults in a future SDK update.
    if(MSVC AND SAFEVST3_STATIC_MSVC_RUNTIME)
        foreach(_safevst3_sdk_target IN ITEMS base sdk_common sdk sdk_hosting)
            if(TARGET ${_safevst3_sdk_target})
                get_target_property(_safevst3_sdk_target_type ${_safevst3_sdk_target} TYPE)
                if(NOT _safevst3_sdk_target_type STREQUAL "INTERFACE_LIBRARY")
                    set_property(TARGET ${_safevst3_sdk_target} PROPERTY
                        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
                endif()
            endif()
        endforeach()
    endif()

    add_library(safevst3_vst3_hosting_runtime STATIC
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp
    )
    target_link_libraries(safevst3_vst3_hosting_runtime PUBLIC sdk_hosting sdk)
    target_compile_definitions(safevst3_vst3_hosting_runtime PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(safevst3_vst3_hosting_runtime PRIVATE /Zc:__cplusplus)
    endif()

    add_library(safevst3_win_ipc STATIC
        src/platform/windows/win_ipc.cpp
        src/platform/windows/editor_ipc.cpp
        src/platform/windows/state_ipc.cpp
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
    target_link_libraries(obs-safe-vst3-host PRIVATE
        safevst3_win_ipc safevst3_vst3_hosting_runtime safevst3_lifecycle_policy
        safevst3_state_restore_policy safevst3_latency_restart_transaction
        safevst3_parameter_refresh_transaction safevst3_io_restart_transaction
        safevst3_reload_component_transaction safevst3_process_context_policy Avrt)
    target_compile_definitions(obs-safe-vst3-host PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

    add_executable(obs-safe-vst3-scanner src/scanner/main.cpp)
    target_link_libraries(obs-safe-vst3-scanner PRIVATE safevst3_vst3_hosting_runtime Shell32)
    target_compile_definitions(obs-safe-vst3-scanner PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

    if(SAFEVST3_BUILD_OBS_PLUGIN)
        find_package(libobs CONFIG QUIET)
        if(libobs_FOUND)
            add_library(obs-safe-vst3 MODULE
                src/obs-plugin/plugin.cpp
                src/obs-plugin/parameter_controls.cpp
                src/obs-plugin/state_store.cpp
            )
            target_include_directories(obs-safe-vst3 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
            target_link_libraries(obs-safe-vst3 PRIVATE
                OBS::libobs safevst3_win_ipc safevst3_source_selection)
            target_compile_definitions(obs-safe-vst3 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
            if(MSVC)
                # Force the OBS compatibility floor header before obs-module.h
                # in every translation unit. OBS_DECLARE_MODULE() therefore
                # exports obs_module_ver() as 29.1 even when built with a newer
                # SDK, while all imported APIs remain within that audited floor.
                target_compile_options(obs-safe-vst3 PRIVATE
                    "/FI${CMAKE_CURRENT_SOURCE_DIR}/src/obs-plugin/obs_compat_floor.hpp")
            endif()
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

    add_executable(vst3-source-selection-test tests/vst3_source_selection.cpp)
    target_link_libraries(vst3-source-selection-test PRIVATE safevst3_source_selection)
    add_test(NAME vst3-source-selection COMMAND vst3-source-selection-test)

    add_executable(lifecycle-restart-policy-test tests/lifecycle_restart_policy.cpp)
    target_link_libraries(lifecycle-restart-policy-test PRIVATE safevst3_lifecycle_policy)
    add_test(NAME lifecycle-restart-policy COMMAND lifecycle-restart-policy-test)

    add_executable(state-restore-policy-test tests/state_restore_policy.cpp)
    target_link_libraries(state-restore-policy-test PRIVATE safevst3_state_restore_policy)
    add_test(NAME state-restore-policy COMMAND state-restore-policy-test)

    add_executable(latency-restart-transaction-test tests/latency_restart_transaction.cpp)
    target_link_libraries(latency-restart-transaction-test PRIVATE safevst3_latency_restart_transaction)
    add_test(NAME latency-restart-transaction COMMAND latency-restart-transaction-test)

    add_executable(parameter-refresh-transaction-test tests/parameter_refresh_transaction.cpp)
    target_link_libraries(parameter-refresh-transaction-test PRIVATE safevst3_parameter_refresh_transaction)
    add_test(NAME parameter-refresh-transaction COMMAND parameter-refresh-transaction-test)

    add_executable(io-restart-transaction-test tests/io_restart_transaction.cpp)
    target_link_libraries(io-restart-transaction-test PRIVATE safevst3_io_restart_transaction)
    add_test(NAME io-restart-transaction COMMAND io-restart-transaction-test)

    add_executable(reload-component-transaction-test tests/reload_component_transaction.cpp)
    target_link_libraries(reload-component-transaction-test PRIVATE safevst3_reload_component_transaction)
    add_test(NAME reload-component-transaction COMMAND reload-component-transaction-test)

    add_executable(process-context-policy-test tests/process_context_policy.cpp)
    target_link_libraries(process-context-policy-test PRIVATE safevst3_process_context_policy)
    add_test(NAME process-context-policy COMMAND process-context-policy-test)

    add_executable(startup-error-test tests/startup_error.cpp)
    target_include_directories(startup-error-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME startup-error COMMAND startup-error-test)

    add_test(
        NAME process-context-source-contract
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/src/host/vst3_engine.cpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/process_context_source_contract.cmake
    )

    add_test(
        NAME strict-lifecycle-source-contract
        COMMAND ${CMAKE_COMMAND}
            -DENGINE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/src/host/vst3_engine.cpp
            -DMAIN_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/src/host/main.cpp
            -DWIN_IPC_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows/win_ipc.cpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/strict_lifecycle_source_contract.cmake
    )

    add_test(
        NAME obs-properties-ownership
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/src/obs-plugin/plugin.cpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/obs_properties_ownership.cmake
    )

    add_executable(parameter-control-test tests/parameter_control.cpp)
    target_link_libraries(parameter-control-test PRIVATE safevst3_protocol)
    add_test(NAME parameter-control COMMAND parameter-control-test)

    add_executable(state-snapshot-test tests/state_snapshot.cpp)
    target_link_libraries(state-snapshot-test PRIVATE safevst3_protocol)
    add_test(NAME state-snapshot COMMAND state-snapshot-test)

    add_executable(recovery-policy-test tests/recovery_policy.cpp)
    target_link_libraries(recovery-policy-test PRIVATE safevst3_protocol)
    add_test(NAME recovery-policy COMMAND recovery-policy-test)

    add_executable(spsc-ring-test tests/spsc_ring.cpp)
    target_link_libraries(spsc-ring-test PRIVATE safevst3_protocol)
    add_test(NAME spsc-ring COMMAND spsc-ring-test)

    if(WIN32)
        add_test(NAME scanner-discovery COMMAND $<TARGET_FILE:obs-safe-vst3-scanner> --self-test)
        add_test(
            NAME scanner-discovery-cli
            COMMAND ${CMAKE_COMMAND}
                -DSCANNER=$<TARGET_FILE:obs-safe-vst3-scanner>
                -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/scanner-discovery-cli
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/scanner_discovery_cli.cmake
        )

        add_executable(fake-hung-host tests/fake_hung_host.cpp)
        target_include_directories(fake-hung-host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(fake-hung-host PRIVATE safevst3_win_ipc)
        target_compile_definitions(fake-hung-host PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

        add_executable(hang-watchdog-test tests/hang_watchdog.cpp)
        target_include_directories(hang-watchdog-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(hang-watchdog-test PRIVATE safevst3_win_ipc)
        target_compile_definitions(hang-watchdog-test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        add_dependencies(hang-watchdog-test fake-hung-host)
        add_test(NAME hang-watchdog COMMAND hang-watchdog-test $<TARGET_FILE:fake-hung-host>)

        add_executable(fake-control-stall-host tests/fake_control_stall_host.cpp)
        target_include_directories(fake-control-stall-host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(fake-control-stall-host PRIVATE safevst3_win_ipc)
        target_compile_definitions(fake-control-stall-host PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

        add_executable(control-stall-dsp-test tests/control_stall_dsp.cpp)
        target_include_directories(control-stall-dsp-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(control-stall-dsp-test PRIVATE safevst3_win_ipc)
        target_compile_definitions(control-stall-dsp-test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        add_dependencies(control-stall-dsp-test fake-control-stall-host)
        add_test(NAME control-stall-dsp COMMAND control-stall-dsp-test $<TARGET_FILE:fake-control-stall-host>)

        add_executable(fake-stateful-host tests/fake_stateful_host.cpp)
        target_include_directories(fake-stateful-host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(fake-stateful-host PRIVATE safevst3_win_ipc)
        target_compile_definitions(fake-stateful-host PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

        add_executable(recovery-checkpoint-test tests/recovery_checkpoint.cpp)
        target_include_directories(recovery-checkpoint-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(recovery-checkpoint-test PRIVATE safevst3_win_ipc)
        target_compile_definitions(recovery-checkpoint-test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        add_dependencies(recovery-checkpoint-test fake-stateful-host)
        add_test(NAME recovery-checkpoint COMMAND recovery-checkpoint-test $<TARGET_FILE:fake-stateful-host>)
    endif()

    add_executable(protocol-layout-test tests/protocol_layout.cpp)
    target_link_libraries(protocol-layout-test PRIVATE safevst3_protocol)
    add_dependencies(protocol-layout-test
        vst3-source-selection-test lifecycle-restart-policy-test state-restore-policy-test
        latency-restart-transaction-test parameter-refresh-transaction-test
        io-restart-transaction-test reload-component-transaction-test
        process-context-policy-test startup-error-test parameter-control-test state-snapshot-test
        recovery-policy-test spsc-ring-test)
    if(WIN32)
        add_dependencies(protocol-layout-test
            obs-safe-vst3-scanner
            hang-watchdog-test fake-hung-host
            control-stall-dsp-test fake-control-stall-host
            recovery-checkpoint-test fake-stateful-host)
    endif()
    add_test(NAME protocol-layout COMMAND protocol-layout-test)
endif()
