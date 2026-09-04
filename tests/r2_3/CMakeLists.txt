cmake_minimum_required(VERSION 3.25)
project(safevst3-r2-3-preset-library VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "R2-3 Rack Preset Library integration is Windows-only")
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

set(STATEFUL_FIXTURE_SOURCE "${SAFEVST3_ROOT}/tests/r0_2/vst3_hosted_plugin_fixture.cpp")

smtg_add_vst3plugin(r2-3-stateful-a-fixture "${STATEFUL_FIXTURE_SOURCE}")
target_link_libraries(r2-3-stateful-a-fixture PRIVATE sdk)
target_compile_definitions(r2-3-stateful-a-fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

smtg_add_vst3plugin(r2-3-stateful-b-fixture "${STATEFUL_FIXTURE_SOURCE}")
target_link_libraries(r2-3-stateful-b-fixture PRIVATE sdk)
target_compile_definitions(r2-3-stateful-b-fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(r2-3-rack-preset-library-integration
    "${CMAKE_CURRENT_LIST_DIR}/rack_preset_library_integration.cpp"
    "${SAFEVST3_ROOT}/src/rack/rack_preset_library.cpp"
    "${SAFEVST3_ROOT}/src/rack/rack_session_snapshot.cpp"
    "${SAFEVST3_ROOT}/src/host/hosted_plugin.cpp"
)
target_include_directories(r2-3-rack-preset-library-integration PRIVATE "${SAFEVST3_ROOT}/src")
target_link_libraries(r2-3-rack-preset-library-integration PRIVATE
    ole32
    safevst3_vst3_hosting_runtime
    safevst3_lifecycle_policy
    safevst3_state_restore_policy
    safevst3_latency_restart_transaction
    safevst3_parameter_refresh_transaction
    safevst3_io_restart_transaction
    safevst3_reload_component_transaction
    safevst3_process_context_policy
)
target_compile_definitions(r2-3-rack-preset-library-integration PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_dependencies(r2-3-rack-preset-library-integration
    r2-3-stateful-a-fixture
    r2-3-stateful-b-fixture
)

enable_testing()
add_test(
    NAME r2-3-rack-preset-library-integration
    COMMAND $<TARGET_FILE:r2-3-rack-preset-library-integration>
        $<TARGET_FILE:r2-3-stateful-a-fixture>
        $<TARGET_FILE:r2-3-stateful-b-fixture>
)
