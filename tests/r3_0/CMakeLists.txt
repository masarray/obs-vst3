cmake_minimum_required(VERSION 3.25)
project(safevst3-r3-0-editor-bridge VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "R3-0 Rack editor integration is Windows-only")
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

add_executable(r3-0-rack-editor-contract
    "${CMAKE_CURRENT_LIST_DIR}/rack_editor_contract.cpp"
)
target_include_directories(r3-0-rack-editor-contract PRIVATE "${SAFEVST3_ROOT}/src")
target_compile_definitions(r3-0-rack-editor-contract PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

smtg_add_vst3plugin(r3-0-gain-a-fixture
    "${SAFEVST3_ROOT}/tests/r1_1/vst3_gain_a_fixture.cpp"
)
target_link_libraries(r3-0-gain-a-fixture PRIVATE sdk)
target_compile_definitions(r3-0-gain-a-fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

smtg_add_vst3plugin(r3-0-gain-b-fixture
    "${SAFEVST3_ROOT}/tests/r1_1/vst3_gain_b_fixture.cpp"
)
target_link_libraries(r3-0-gain-b-fixture PRIVATE sdk)
target_compile_definitions(r3-0-gain-b-fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(r3-0-rack-editor-lifecycle
    "${CMAKE_CURRENT_LIST_DIR}/rack_editor_lifecycle_integration.cpp"
)
target_include_directories(r3-0-rack-editor-lifecycle PRIVATE "${SAFEVST3_ROOT}/src")
target_compile_definitions(r3-0-rack-editor-lifecycle PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_dependencies(r3-0-rack-editor-lifecycle
    obs-safe-vst3-rack-host
    r3-0-gain-a-fixture
    r3-0-gain-b-fixture
)

enable_testing()
add_test(NAME r3-0-rack-editor-contract COMMAND r3-0-rack-editor-contract)
add_test(
    NAME r3-0-rack-editor-lifecycle
    COMMAND $<TARGET_FILE:r3-0-rack-editor-lifecycle>
        $<TARGET_FILE:obs-safe-vst3-rack-host>
        $<TARGET_FILE:r3-0-gain-a-fixture>
        $<TARGET_FILE:r3-0-gain-b-fixture>
)
