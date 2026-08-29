cmake_minimum_required(VERSION 3.25)
project(safevst3-r0-1-characterization VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "R0-1 real-VST3 characterization is Windows-only")
endif()

# The repository deliberately builds its Steinberg hosting/runtime targets with
# the static MSVC runtime. Keep the standalone fixture/test targets on the same
# runtime so the characterization is about VST3 behavior, not CRT mixing.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(SAFEVST3_BUILD_OBS_PLUGIN OFF CACHE BOOL "" FORCE)
set(SAFEVST3_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SAFEVST3_STATIC_MSVC_RUNTIME ON CACHE BOOL "" FORCE)

# These are Steinberg-supported build options. The R0-1 fixture exists only to
# exercise the production engine seam inside CI, so do not run the SDK's own
# post-build validator/module-info generation or create a per-user VST3 link.
# Production builds keep the repository defaults; these settings are local to
# this standalone test project before it includes the repository root.
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_MODULE_INFO OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)

get_filename_component(SAFEVST3_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_subdirectory("${SAFEVST3_ROOT}" safevst3-root)

set(_fixture_source "${CMAKE_CURRENT_LIST_DIR}/vst3_process_fixture.cpp")

smtg_add_vst3plugin(r0-1-fixture-mono "${_fixture_source}")
target_link_libraries(r0-1-fixture-mono PRIVATE sdk)
target_compile_definitions(r0-1-fixture-mono PRIVATE
    SAFEVST3_FIXTURE_CHANNELS=1
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

smtg_add_vst3plugin(r0-1-fixture-stereo "${_fixture_source}")
target_link_libraries(r0-1-fixture-stereo PRIVATE sdk)
target_compile_definitions(r0-1-fixture-stereo PRIVATE
    SAFEVST3_FIXTURE_CHANNELS=2
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

add_executable(r0-1-vst3-engine-process-test
    "${CMAKE_CURRENT_LIST_DIR}/vst3_engine_process_characterization.cpp"
    "${SAFEVST3_ROOT}/src/host/hosted_plugin.cpp"
    "${SAFEVST3_ROOT}/src/host/vst3_engine.cpp"
)
target_include_directories(r0-1-vst3-engine-process-test PRIVATE
    "${SAFEVST3_ROOT}/src"
)
target_link_libraries(r0-1-vst3-engine-process-test PRIVATE
    safevst3_vst3_hosting_runtime
    safevst3_lifecycle_policy
    safevst3_state_restore_policy
    safevst3_latency_restart_transaction
    safevst3_parameter_refresh_transaction
    safevst3_io_restart_transaction
    safevst3_reload_component_transaction
    safevst3_process_context_policy
)
target_compile_definitions(r0-1-vst3-engine-process-test PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
add_dependencies(r0-1-vst3-engine-process-test
    r0-1-fixture-mono
    r0-1-fixture-stereo
)

enable_testing()
add_test(
    NAME r0-1-vst3-engine-process-characterization
    COMMAND $<TARGET_FILE:r0-1-vst3-engine-process-test>
        $<TARGET_FILE:r0-1-fixture-mono>
        $<TARGET_FILE:r0-1-fixture-stereo>
)
