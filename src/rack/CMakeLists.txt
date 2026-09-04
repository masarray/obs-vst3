if(NOT WIN32)
    return()
endif()

get_filename_component(SAFEVST3_RACK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

include(FetchContent)
set(SAFEVST3_IMGUI_SOURCE_DIR "" CACHE PATH "Optional exact Dear ImGui source checkout for Rack editor")
set(SAFEVST3_IMGUI_COMMIT "01380c579715e62fb9a8d6ec0502c4ea83bfde6e")
if(SAFEVST3_IMGUI_SOURCE_DIR)
    set(_safevst3_imgui_source "${SAFEVST3_IMGUI_SOURCE_DIR}")
else()
    FetchContent_Declare(
        safevst3_rack_imgui_source
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG ${SAFEVST3_IMGUI_COMMIT}
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(safevst3_rack_imgui_source)
    set(_safevst3_imgui_source "${safevst3_rack_imgui_source_SOURCE_DIR}")
endif()

if(NOT EXISTS "${_safevst3_imgui_source}/imgui.cpp" OR
   NOT EXISTS "${_safevst3_imgui_source}/LICENSE.txt")
    message(FATAL_ERROR "R3-0 Rack editor requires the exact pinned Dear ImGui source tree")
endif()

add_library(safevst3_rack_imgui STATIC
    "${_safevst3_imgui_source}/imgui.cpp"
    "${_safevst3_imgui_source}/imgui_draw.cpp"
    "${_safevst3_imgui_source}/imgui_tables.cpp"
    "${_safevst3_imgui_source}/imgui_widgets.cpp"
    "${_safevst3_imgui_source}/backends/imgui_impl_win32.cpp"
    "${_safevst3_imgui_source}/backends/imgui_impl_dx11.cpp"
)
target_include_directories(safevst3_rack_imgui PUBLIC
    "${_safevst3_imgui_source}"
    "${_safevst3_imgui_source}/backends"
)
target_compile_definitions(safevst3_rack_imgui PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(safevst3_rack_imgui PUBLIC d3d11 dxgi dwmapi)

add_executable(obs-safe-vst3-rack-host
    "${CMAKE_CURRENT_LIST_DIR}/main_r3_2_hosted.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/rack_editor_window.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/rack_plugin_catalog.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/rack_preset_library.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/rack_preset_management.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/rack_session_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/rack_vendor_editor_manager.cpp"
    "${SAFEVST3_RACK_ROOT}/src/host/hosted_plugin.cpp"
    "${SAFEVST3_RACK_ROOT}/src/host/native_editor.cpp"
)
target_include_directories(obs-safe-vst3-rack-host PRIVATE "${SAFEVST3_RACK_ROOT}/src")
target_link_libraries(obs-safe-vst3-rack-host PRIVATE
    ole32
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
target_compile_definitions(obs-safe-vst3-rack-host PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(TARGET obs-safe-vst3-scanner)
    add_dependencies(obs-safe-vst3-rack-host obs-safe-vst3-scanner)
endif()
