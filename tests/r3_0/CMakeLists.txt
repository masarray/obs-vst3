cmake_minimum_required(VERSION 3.25)
project(safevst3-r3-0-editor-bridge VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(SAFEVST3_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_executable(r3-0-rack-editor-contract
    "${CMAKE_CURRENT_LIST_DIR}/rack_editor_contract.cpp"
)
target_include_directories(r3-0-rack-editor-contract PRIVATE "${SAFEVST3_ROOT}/src")

if(WIN32)
    target_compile_definitions(r3-0-rack-editor-contract PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

enable_testing()
add_test(NAME r3-0-rack-editor-contract COMMAND r3-0-rack-editor-contract)
