cmake_minimum_required(VERSION 3.25)
project(safevst3-ui0-smoke LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "UI-0 Win32/D3D11 smoke is Windows-only")
endif()
if(NOT DEFINED IMGUI_SOURCE_DIR OR NOT EXISTS "${IMGUI_SOURCE_DIR}/imgui.cpp")
    message(FATAL_ERROR "IMGUI_SOURCE_DIR must point at the exact pinned Dear ImGui checkout")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(ui0-dear-imgui STATIC
    "${IMGUI_SOURCE_DIR}/imgui.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_draw.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_tables.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
    "${IMGUI_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
    "${IMGUI_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
)
target_include_directories(ui0-dear-imgui PUBLIC
    "${IMGUI_SOURCE_DIR}"
    "${IMGUI_SOURCE_DIR}/backends"
)
target_compile_definitions(ui0-dear-imgui PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(ui0-dear-imgui PUBLIC d3d11 dxgi dwmapi)

add_executable(obs-safe-vst3-rack-ui-smoke WIN32 rack_ui_smoke.cpp)
target_compile_definitions(obs-safe-vst3-rack-ui-smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(obs-safe-vst3-rack-ui-smoke PRIVATE ui0-dear-imgui d3d11 dxgi dwmapi)

set_target_properties(obs-safe-vst3-rack-ui-smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

enable_testing()
add_test(NAME ui0-imgui-win32-d3d11-smoke COMMAND obs-safe-vst3-rack-ui-smoke)
