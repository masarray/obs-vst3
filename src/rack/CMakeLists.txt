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

# R3-4 reuses the already-qualified R3-2 implementation in the same translation
# unit so its internal generation/control seams stay authoritative. Generate an
# include-only copy whose sole textual difference is the legacy entrypoint name;
# the checked-in R3-2 source remains unchanged and its exact regressions continue
# to compile it normally in their own targets.
set(_safevst3_r3_2_source "${CMAKE_CURRENT_LIST_DIR}/main_r3_2.cpp")
file(READ "${_safevst3_r3_2_source}" _safevst3_r3_2_embed)
set(_safevst3_r3_2_entry "int wmain(int argc, wchar_t** argv)")
string(FIND "${_safevst3_r3_2_embed}" "${_safevst3_r3_2_entry}" _safevst3_r3_2_entry_pos)
if(_safevst3_r3_2_entry_pos EQUAL -1)
    message(FATAL_ERROR "R3-4 could not locate the qualified R3-2 entrypoint")
endif()
string(REPLACE
    "${_safevst3_r3_2_entry}"
    "int safevst3_r3_2_embedded_wmain(int argc, wchar_t** argv)"
    _safevst3_r3_2_embed
    "${_safevst3_r3_2_embed}"
)
set(_safevst3_r3_2_embed_root "${CMAKE_CURRENT_BINARY_DIR}/r3_4_embed")
file(MAKE_DIRECTORY "${_safevst3_r3_2_embed_root}/rack")
file(WRITE "${_safevst3_r3_2_embed_root}/rack/main_r3_2.cpp" "${_safevst3_r3_2_embed}")

# Stable R3-5 composes automatic Session Snapshot restore/checkpoint semantics
# above the qualified R3-4 implementation. Keep R3-4 checked in unchanged and
# generate an include-only copy with only its entrypoint renamed. This mirrors
# the established R3-4/R3-2 composition pattern and preserves every lower-level
# regression target on its original source file.
set(_safevst3_r3_4_source "${CMAKE_CURRENT_LIST_DIR}/main_r3_4.cpp")
file(READ "${_safevst3_r3_4_source}" _safevst3_r3_4_embed)
set(_safevst3_r3_4_entry "int wmain(int argc, wchar_t** argv)")
string(FIND "${_safevst3_r3_4_embed}" "${_safevst3_r3_4_entry}" _safevst3_r3_4_entry_pos)
if(_safevst3_r3_4_entry_pos EQUAL -1)
    message(FATAL_ERROR "Stable Rack could not locate the qualified R3-4 entrypoint")
endif()
string(REPLACE
    "${_safevst3_r3_4_entry}"
    "int safevst3_r3_4_embedded_wmain(int argc, wchar_t** argv)"
    _safevst3_r3_4_embed
    "${_safevst3_r3_4_embed}"
)
set(_safevst3_r3_4_embed_root "${CMAKE_CURRENT_BINARY_DIR}/r3_5_embed")
file(MAKE_DIRECTORY "${_safevst3_r3_4_embed_root}/rack")
file(WRITE "${_safevst3_r3_4_embed_root}/rack/main_r3_4.cpp" "${_safevst3_r3_4_embed}")

# A Rack Editor close destroys the Win32/D3D/ImGui session while the owning
# helper/window thread remains alive. Some Windows runners exposed a narrow
# immediate-reopen race where transient session creation can fail once after
# teardown. Preserve the qualified checked-in editor source and generate the
# shipping translation unit with a bounded UI-thread-only recreation retry.
# A successfully created session still blocks until normal user close and is
# therefore never retried. DSP/control paths are unaffected.
set(_safevst3_editor_source "${CMAKE_CURRENT_LIST_DIR}/rack_editor_window.cpp")
file(READ "${_safevst3_editor_source}" _safevst3_editor_embed)
set(_safevst3_editor_session_call "            run_window_session();")
string(FIND "${_safevst3_editor_embed}" "${_safevst3_editor_session_call}" _safevst3_editor_session_pos)
if(_safevst3_editor_session_pos EQUAL -1)
    message(FATAL_ERROR "Rack editor lifecycle hardening could not locate the session call")
endif()
set(_safevst3_editor_session_retry [=[
            for (int attempt = 0; attempt < 3; ++attempt) {
                if (run_window_session())
                    break;
                if (stop_requested.load(std::memory_order_acquire))
                    break;
                Sleep(50);
            }
]=])
string(REPLACE
    "${_safevst3_editor_session_call}"
    "${_safevst3_editor_session_retry}"
    _safevst3_editor_embed
    "${_safevst3_editor_embed}"
)
set(_safevst3_editor_generated "${CMAKE_CURRENT_BINARY_DIR}/rack_editor_window_release.cpp")
file(WRITE "${_safevst3_editor_generated}" "${_safevst3_editor_embed}")

add_executable(obs-safe-vst3-rack-host
    "${CMAKE_CURRENT_LIST_DIR}/main_r3_2_hosted.cpp"
    "${_safevst3_editor_generated}"
    "${CMAKE_CURRENT_LIST_DIR}/rack_plugin_catalog.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/rack_preset_library.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/rack_preset_management.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/rack_session_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/rack_vendor_editor_manager.cpp"
    "${SAFEVST3_RACK_ROOT}/src/host/hosted_plugin.cpp"
    "${SAFEVST3_RACK_ROOT}/src/host/native_editor.cpp"
)

# P1 product skin is compile-time isolated to the generated Rack Editor source.
# The qualified helper/DSP/control implementation never sees the visual macros.
set_source_files_properties(
    "${_safevst3_editor_generated}"
    PROPERTIES
        COMPILE_OPTIONS "/FI${CMAKE_CURRENT_LIST_DIR}/rack_editor_p1_skin.hpp"
)

target_include_directories(obs-safe-vst3-rack-host BEFORE PRIVATE
    "${_safevst3_r3_4_embed_root}"
    "${_safevst3_r3_2_embed_root}"
    "${SAFEVST3_RACK_ROOT}/src"
)
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
