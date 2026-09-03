cmake_minimum_required(VERSION 3.25)
project(safevst3-r3-3-vendor-editor-contract VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "R3-3 vendor editor contract is Windows-only")
endif()

get_filename_component(SAFEVST3_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_executable(r3-3-vendor-editor-contract
    "${CMAKE_CURRENT_LIST_DIR}/rack_vendor_editor_contract.cpp"
)
target_include_directories(r3-3-vendor-editor-contract PRIVATE "${SAFEVST3_ROOT}/src")
target_compile_definitions(r3-3-vendor-editor-contract PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

enable_testing()
add_test(NAME r3-3-vendor-editor-contract COMMAND $<TARGET_FILE:r3-3-vendor-editor-contract>)
