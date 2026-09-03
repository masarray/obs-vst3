cmake_minimum_required(VERSION 3.25)
project(safevst3-r3-3-vendor-editor-contract VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "R3-3 vendor editor contract is Windows-only")
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

add_executable(r3-3-vendor-editor-contract
    "${CMAKE_CURRENT_LIST_DIR}/rack_vendor_editor_contract.cpp"
)
target_include_directories(r3-3-vendor-editor-contract PRIVATE "${SAFEVST3_ROOT}/src")
target_compile_definitions(r3-3-vendor-editor-contract PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

smtg_add_vst3plugin(r3-3-vendor-editor-fixture
    "${CMAKE_CURRENT_LIST_DIR}/vst3_vendor_editor_fixture.cpp"
)
target_link_libraries(r3-3-vendor-editor-fixture PRIVATE sdk)
target_compile_definitions(r3-3-vendor-editor-fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(r3-3-vendor-editor-integration
    "${CMAKE_CURRENT_LIST_DIR}/rack_vendor_editor_integration.cpp"
    "${SAFEVST3_ROOT}/src/rack/rack_vendor_editor_manager.cpp"
    "${SAFEVST3_ROOT}/src/host/native_editor.cpp"
    "${SAFEVST3_ROOT}/src/host/hosted_plugin.cpp"
)
target_include_directories(r3-3-vendor-editor-integration PRIVATE "${SAFEVST3_ROOT}/src")
target_compile_definitions(r3-3-vendor-editor-integration PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(r3-3-vendor-editor-integration PRIVATE
    safevst3_vst3_hosting_runtime
    safevst3_lifecycle_policy
    safevst3_state_restore_policy
    safevst3_latency_restart_transaction
    safevst3_parameter_refresh_transaction
    safevst3_io_restart_transaction
    safevst3_reload_component_transaction
    safevst3_process_context_policy
)
add_dependencies(r3-3-vendor-editor-integration r3-3-vendor-editor-fixture)

enable_testing()
add_test(NAME r3-3-vendor-editor-contract COMMAND $<TARGET_FILE:r3-3-vendor-editor-contract>)
add_test(
    NAME r3-3-vendor-editor-integration
    COMMAND $<TARGET_FILE:r3-3-vendor-editor-integration>
        $<TARGET_FILE:r3-3-vendor-editor-fixture>
)
