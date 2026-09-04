cmake_minimum_required(VERSION 3.25)
project(safevst3-r5-1-stable-lock VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "R5-1 stable Rack lock is Windows-only")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(SAFEVST3_BUILD_OBS_PLUGIN OFF CACHE BOOL "" FORCE)
set(SAFEVST3_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SAFEVST3_STATIC_MSVC_RUNTIME ON CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_MODULE_INFO OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)

get_filename_component(SAFEVST3_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_subdirectory("${SAFEVST3_ROOT}" safevst3-root)
add_subdirectory("${SAFEVST3_ROOT}/src/rack" rack-target)

set(STATEFUL_FIXTURE_SOURCE "${SAFEVST3_ROOT}/tests/r0_2/vst3_hosted_plugin_fixture.cpp")
smtg_add_vst3plugin(r5-1-stateful-fixture "${STATEFUL_FIXTURE_SOURCE}")
target_link_libraries(r5-1-stateful-fixture PRIVATE sdk)
target_compile_definitions(r5-1-stateful-fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(r5-1-stable-rack-integration
    "${CMAKE_CURRENT_LIST_DIR}/stable_rack_integration.cpp"
    "${SAFEVST3_ROOT}/src/rack/rack_session_snapshot.cpp"
    "${SAFEVST3_ROOT}/src/host/hosted_plugin.cpp"
)
target_include_directories(r5-1-stable-rack-integration PRIVATE "${SAFEVST3_ROOT}/src")
target_link_libraries(r5-1-stable-rack-integration PRIVATE
    safevst3_win_rack_bridge
    safevst3_vst3_hosting_runtime
    safevst3_lifecycle_policy
    safevst3_state_restore_policy
    safevst3_latency_restart_transaction
    safevst3_parameter_refresh_transaction
    safevst3_io_restart_transaction
    safevst3_reload_component_transaction
    safevst3_process_context_policy
)
target_compile_definitions(r5-1-stable-rack-integration PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_dependencies(r5-1-stable-rack-integration
    obs-safe-vst3-rack-host
    r5-1-stateful-fixture
)

enable_testing()
add_test(
    NAME r5-1-stable-rack-integration
    COMMAND $<TARGET_FILE:r5-1-stable-rack-integration>
        $<TARGET_FILE:obs-safe-vst3-rack-host>
        $<TARGET_FILE:r5-1-stateful-fixture>
)
add_test(
    NAME r5-1-stable-rack-source-contract
    COMMAND ${CMAKE_COMMAND}
        -DRACK_FILTER=${SAFEVST3_ROOT}/src/obs-plugin/rack_filter.cpp
        -DRACK_BRIDGE_HEADER=${SAFEVST3_ROOT}/src/platform/windows/win_rack_bridge.hpp
        -DRACK_BRIDGE_SOURCE=${SAFEVST3_ROOT}/src/platform/windows/win_rack_bridge.cpp
        -DRACK_STABLE_MAIN=${SAFEVST3_ROOT}/src/rack/main_r3_5.cpp
        -DRACK_HOSTED=${SAFEVST3_ROOT}/src/rack/rack_hosted_plugin.hpp
        -DRACK_SHIPPING_ENTRY=${SAFEVST3_ROOT}/src/rack/main_r3_2_hosted.cpp
        -P ${CMAKE_CURRENT_LIST_DIR}/stable_rack_source_contract.cmake
)
