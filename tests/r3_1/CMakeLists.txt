cmake_minimum_required(VERSION 3.25)
project(safevst3-r3-1-obs-rack-launcher VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "R3-1 OBS Rack launcher integration is Windows-only")
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

add_executable(r3-1-rack-obs-bridge
    "${CMAKE_CURRENT_LIST_DIR}/rack_obs_bridge_integration.cpp"
    "${SAFEVST3_ROOT}/src/platform/windows/win_rack_bridge.cpp"
)
target_include_directories(r3-1-rack-obs-bridge PRIVATE "${SAFEVST3_ROOT}/src")
target_compile_definitions(r3-1-rack-obs-bridge PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_dependencies(r3-1-rack-obs-bridge obs-safe-vst3-rack-host)

enable_testing()
add_test(
    NAME r3-1-rack-obs-bridge
    COMMAND $<TARGET_FILE:r3-1-rack-obs-bridge>
        $<TARGET_FILE:obs-safe-vst3-rack-host>
)
add_test(
    NAME r3-1-obs-source-contract
    COMMAND ${CMAKE_COMMAND}
        -DROOT=${SAFEVST3_ROOT}
        -P ${CMAKE_CURRENT_LIST_DIR}/rack_obs_source_contract.cmake
)
