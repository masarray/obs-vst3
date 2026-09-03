cmake_minimum_required(VERSION 3.25)
project(safevst3-r3-2-rack-slot-browser VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "R3-2 Rack slot/browser contract is Windows-only")
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

add_executable(r3-2-rack-slot-workflow
    "${CMAKE_CURRENT_LIST_DIR}/rack_slot_workflow_contract.cpp"
)
target_include_directories(r3-2-rack-slot-workflow PRIVATE "${SAFEVST3_ROOT}/src")
target_compile_definitions(r3-2-rack-slot-workflow PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_dependencies(r3-2-rack-slot-workflow obs-safe-vst3-rack-host obs-safe-vst3-scanner)

# Two independently packaged real VST3 bundles reuse the proven R0-2 stateful
# processor. Their distinct bundle paths provide deterministic catalog/Replace
# identity while keeping the DSP oracle fixed at gain=0.5, latency=64 samples.
smtg_add_vst3plugin(r3-2-browser-a-fixture
    "${SAFEVST3_ROOT}/tests/r0_2/vst3_hosted_plugin_fixture.cpp"
)
target_link_libraries(r3-2-browser-a-fixture PRIVATE sdk)
target_compile_definitions(r3-2-browser-a-fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

smtg_add_vst3plugin(r3-2-browser-b-fixture
    "${SAFEVST3_ROOT}/tests/r0_2/vst3_hosted_plugin_fixture.cpp"
)
target_link_libraries(r3-2-browser-b-fixture PRIVATE sdk)
target_compile_definitions(r3-2-browser-b-fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# The integration translation unit includes main_r3_2.cpp directly so it can
# drive the exact internal command executor and generation store. Force the same
# Rack-only HostedPlugin wrapper used by the shipping helper before that include.
set_source_files_properties(
    "${CMAKE_CURRENT_LIST_DIR}/rack_product_integration.cpp"
    PROPERTIES COMPILE_OPTIONS "/FI${SAFEVST3_ROOT}/src/rack/rack_hosted_plugin.hpp")

add_executable(r3-2-rack-product-integration
    "${CMAKE_CURRENT_LIST_DIR}/rack_product_integration.cpp"
    "${SAFEVST3_ROOT}/src/rack/rack_editor_window.cpp"
    "${SAFEVST3_ROOT}/src/rack/rack_plugin_catalog.cpp"
    "${SAFEVST3_ROOT}/src/host/hosted_plugin.cpp"
)
target_include_directories(r3-2-rack-product-integration PRIVATE "${SAFEVST3_ROOT}/src")
target_compile_definitions(r3-2-rack-product-integration PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(r3-2-rack-product-integration PRIVATE
    safevst3_rack_imgui
    safevst3_vst3_hosting_runtime
    safevst3_lifecycle_policy
    safevst3_state_restore_policy
    safevst3_latency_restart_transaction
    safevst3_parameter_refresh_transaction
    safevst3_io_restart_transaction
    safevst3_reload_component_transaction
    safevst3_process_context_policy
)
add_dependencies(r3-2-rack-product-integration
    obs-safe-vst3-scanner
    r3-2-browser-a-fixture
    r3-2-browser-b-fixture
)

enable_testing()
add_test(NAME r3-2-rack-slot-workflow COMMAND $<TARGET_FILE:r3-2-rack-slot-workflow>)
add_test(
    NAME r3-2-rack-product-integration
    COMMAND $<TARGET_FILE:r3-2-rack-product-integration>
        $<TARGET_FILE:obs-safe-vst3-scanner>
        $<TARGET_FILE:r3-2-browser-a-fixture>
        $<TARGET_FILE:r3-2-browser-b-fixture>
)
add_test(
    NAME r3-2-rack-slot-source-contract
    COMMAND ${CMAKE_COMMAND}
        -DROOT=${SAFEVST3_ROOT}
        -P ${CMAKE_CURRENT_LIST_DIR}/rack_slot_source_contract.cmake
)
