cmake_minimum_required(VERSION 3.25)
project(safevst3-r3-4-preset-ux-contract VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "R3-4 preset UX contract is Windows-only")
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

add_executable(r3-4-preset-ux-contract
    "${CMAKE_CURRENT_LIST_DIR}/rack_preset_ux_contract.cpp"
)
target_include_directories(r3-4-preset-ux-contract PRIVATE "${SAFEVST3_ROOT}/src")
target_compile_definitions(r3-4-preset-ux-contract PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(r3-4-preset-management-contract
    "${CMAKE_CURRENT_LIST_DIR}/rack_preset_management_contract.cpp"
    "${SAFEVST3_ROOT}/src/rack/rack_preset_management.cpp"
    "${SAFEVST3_ROOT}/src/rack/rack_preset_library.cpp"
    "${SAFEVST3_ROOT}/src/rack/rack_session_snapshot.cpp"
    "${SAFEVST3_ROOT}/src/host/hosted_plugin.cpp"
)
target_include_directories(r3-4-preset-management-contract PRIVATE "${SAFEVST3_ROOT}/src")
target_compile_definitions(r3-4-preset-management-contract PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(r3-4-preset-management-contract PRIVATE
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

enable_testing()
add_test(NAME r3-4-preset-ux-contract COMMAND $<TARGET_FILE:r3-4-preset-ux-contract>)
add_test(NAME r3-4-preset-management-contract COMMAND $<TARGET_FILE:r3-4-preset-management-contract>)
