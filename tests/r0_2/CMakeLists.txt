cmake_minimum_required(VERSION 3.25)
project(safevst3-r0-2-characterization VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "R0-2 HostedPlugin characterization is Windows-only")
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

smtg_add_vst3plugin(r0-2-hosted-plugin-fixture
    "${CMAKE_CURRENT_LIST_DIR}/vst3_hosted_plugin_fixture.cpp"
)
target_link_libraries(r0-2-hosted-plugin-fixture PRIVATE sdk)
target_compile_definitions(r0-2-hosted-plugin-fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(r0-2-hosted-plugin-characterization
    "${CMAKE_CURRENT_LIST_DIR}/hosted_plugin_characterization.cpp"
    "${SAFEVST3_ROOT}/src/host/hosted_plugin.cpp"
)
target_include_directories(r0-2-hosted-plugin-characterization PRIVATE "${SAFEVST3_ROOT}/src")
target_link_libraries(r0-2-hosted-plugin-characterization PRIVATE
    safevst3_vst3_hosting_runtime
    safevst3_lifecycle_policy
    safevst3_state_restore_policy
    safevst3_latency_restart_transaction
    safevst3_parameter_refresh_transaction
    safevst3_io_restart_transaction
    safevst3_reload_component_transaction
    safevst3_process_context_policy
)
target_compile_definitions(r0-2-hosted-plugin-characterization PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_dependencies(r0-2-hosted-plugin-characterization r0-2-hosted-plugin-fixture)

enable_testing()
add_test(
    NAME r0-2-hosted-plugin-characterization
    COMMAND $<TARGET_FILE:r0-2-hosted-plugin-characterization>
        $<TARGET_FILE:r0-2-hosted-plugin-fixture>
)
add_test(
    NAME r0-2-hosted-plugin-protocol-neutral-contract
    COMMAND ${CMAKE_COMMAND}
        -DHEADER=${SAFEVST3_ROOT}/src/host/hosted_plugin.hpp
        -DSOURCE=${SAFEVST3_ROOT}/src/host/hosted_plugin.cpp
        -P ${CMAKE_CURRENT_LIST_DIR}/hosted_plugin_protocol_neutral.cmake
)
