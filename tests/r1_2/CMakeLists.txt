cmake_minimum_required(VERSION 3.25)
project(safevst3-r1-2-safety VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "R1-2 Rack safety integration is Windows-only")
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

smtg_add_vst3plugin(r1-2-gain-a-fixture
    "${CMAKE_CURRENT_LIST_DIR}/vst3_gain_a_latency_fixture.cpp"
)
target_link_libraries(r1-2-gain-a-fixture PRIVATE sdk)
target_compile_definitions(r1-2-gain-a-fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

smtg_add_vst3plugin(r1-2-gain-b-fixture
    "${CMAKE_CURRENT_LIST_DIR}/vst3_gain_b_latency_fail_fixture.cpp"
)
target_link_libraries(r1-2-gain-b-fixture PRIVATE sdk)
target_compile_definitions(r1-2-gain-b-fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(r1-2-rack-safety-integration
    "${CMAKE_CURRENT_LIST_DIR}/rack_safety_integration.cpp"
)
target_include_directories(r1-2-rack-safety-integration PRIVATE "${SAFEVST3_ROOT}/src")
target_compile_definitions(r1-2-rack-safety-integration PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_dependencies(r1-2-rack-safety-integration
    obs-safe-vst3-rack-host
    r1-2-gain-a-fixture
    r1-2-gain-b-fixture
)

enable_testing()
add_test(
    NAME r1-2-rack-safety-integration
    COMMAND $<TARGET_FILE:r1-2-rack-safety-integration>
        $<TARGET_FILE:obs-safe-vst3-rack-host>
        $<TARGET_FILE:r1-2-gain-a-fixture>
        $<TARGET_FILE:r1-2-gain-b-fixture>
)
add_test(
    NAME r1-2-rack-source-contract
    COMMAND ${CMAKE_COMMAND}
        -DRACK_PROTOCOL=${SAFEVST3_ROOT}/src/rack/rack_protocol.hpp
        -DRACK_SOURCE=${SAFEVST3_ROOT}/src/rack/main.cpp
        -P ${CMAKE_CURRENT_LIST_DIR}/rack_source_contract.cmake
)
