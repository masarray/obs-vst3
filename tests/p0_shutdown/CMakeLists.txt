cmake_minimum_required(VERSION 3.24)
project(p0_rack_shutdown LANGUAGES CXX)

if(NOT WIN32)
    return()
endif()

get_filename_component(SAFEVST3_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

enable_testing()

add_executable(p0-rack-hung-helper hung_helper.cpp)
target_include_directories(p0-rack-hung-helper PRIVATE "${SAFEVST3_ROOT}/src")
target_compile_features(p0-rack-hung-helper PRIVATE cxx_std_20)
target_compile_definitions(p0-rack-hung-helper PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

add_executable(p0-rack-shutdown-runtime
    shutdown_runtime.cpp
    "${SAFEVST3_ROOT}/src/platform/windows/win_rack_bridge.cpp"
)
target_include_directories(p0-rack-shutdown-runtime PRIVATE "${SAFEVST3_ROOT}/src")
target_compile_features(p0-rack-shutdown-runtime PRIVATE cxx_std_20)
target_compile_definitions(p0-rack-shutdown-runtime PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

add_test(
    NAME p0-rack-hung-helper-bounded-shutdown
    COMMAND p0-rack-shutdown-runtime "$<TARGET_FILE:p0-rack-hung-helper>"
)
set_tests_properties(p0-rack-hung-helper-bounded-shutdown PROPERTIES TIMEOUT 5)
