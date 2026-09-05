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

# P6 instruments only the shipping helper copy of the qualified Rack DSP loop.
# P5's authoritative final-output LUFS-I/dBTP remains intact; P6 adds real
# session Input Trim / Output Fader gain, one-block ramps, and stereo L/R peak
# telemetry. The checked-in qualified R1/R2 source remains unchanged.
set(_safevst3_meter_main_source "${CMAKE_CURRENT_LIST_DIR}/main.cpp")
file(READ "${_safevst3_meter_main_source}" _safevst3_meter_main_embed)

set(_safevst3_meter_include_anchor "#include \"rack/rack_protocol.hpp\"\n")
string(FIND "${_safevst3_meter_main_embed}" "${_safevst3_meter_include_anchor}" _safevst3_meter_include_pos)
if(_safevst3_meter_include_pos EQUAL -1)
    message(FATAL_ERROR "P6 master instrumentation could not locate Rack protocol include")
endif()
string(REPLACE
    "${_safevst3_meter_include_anchor}"
    "#include \"rack/rack_protocol.hpp\"\n#include \"rack/rack_meter_telemetry.hpp\"\n#include \"rack/rack_broadcast_loudness.hpp\"\n#include \"rack/rack_master_controls.hpp\"\n"
    _safevst3_meter_main_embed
    "${_safevst3_meter_main_embed}"
)

set(_safevst3_master_setup_anchor [=[
    RackBuffers buffers;
    float* rack_input[kMaxChannels]{};
    float* ping[kMaxChannels]{};
    float* pong[kMaxChannels]{};

    for (std::uint32_t ch = 0; ch < kMaxChannels; ++ch) {
        rack_input[ch] = endpoint.region->input[ch];
        ping[ch] = buffers.ping[ch].data();
        pong[ch] = buffers.pong[ch].data();
    }
]=])
string(FIND "${_safevst3_meter_main_embed}" "${_safevst3_master_setup_anchor}" _safevst3_master_setup_pos)
if(_safevst3_master_setup_pos EQUAL -1)
    message(FATAL_ERROR "P6 master instrumentation could not locate Rack buffer setup")
endif()
set(_safevst3_master_setup_replace [=[
    RackBuffers buffers;
    float* rack_input[kMaxChannels]{};
    float* trimmed[kMaxChannels]{};
    float* ping[kMaxChannels]{};
    float* pong[kMaxChannels]{};
    safevst3::rack::ui::RackMasterGainSmoother master_gain_smoother{};

    for (std::uint32_t ch = 0; ch < kMaxChannels; ++ch) {
        rack_input[ch] = endpoint.region->input[ch];
        trimmed[ch] = endpoint.region->output[ch];
        ping[ch] = buffers.ping[ch].data();
        pong[ch] = buffers.pong[ch].data();
    }
]=])
string(REPLACE
    "${_safevst3_master_setup_anchor}"
    "${_safevst3_master_setup_replace}"
    _safevst3_meter_main_embed
    "${_safevst3_meter_main_embed}"
)

set(_safevst3_meter_valid_anchor [=[
            continue;
        }

        std::uint32_t generation_index = 0;
]=])
string(FIND "${_safevst3_meter_main_embed}" "${_safevst3_meter_valid_anchor}" _safevst3_meter_valid_pos)
if(_safevst3_meter_valid_pos EQUAL -1)
    message(FATAL_ERROR "P6 master instrumentation could not locate validated DSP block seam")
endif()
set(_safevst3_meter_valid_replace [=[
            continue;
        }

        const safevst3::rack::ui::RackMasterControlSnapshot master_controls =
            safevst3::rack::ui::g_rack_master_controls.snapshot();
        safevst3::rack::ui::rack_apply_smoothed_gain(
            rack_input, trimmed, channels, frames, master_controls.input_db,
            master_gain_smoother.input_linear);
        const safevst3::rack::ui::RackStereoPeaks input_meter_peaks =
            safevst3::rack::ui::rack_meter_stereo_peaks(trimmed, channels, frames);

        std::uint32_t generation_index = 0;
]=])
string(REPLACE
    "${_safevst3_meter_valid_anchor}"
    "${_safevst3_meter_valid_replace}"
    _safevst3_meter_main_embed
    "${_safevst3_meter_main_embed}"
)

set(_safevst3_master_current_anchor "        float** current = rack_input;")
string(FIND "${_safevst3_meter_main_embed}" "${_safevst3_master_current_anchor}" _safevst3_master_current_pos)
if(_safevst3_master_current_pos EQUAL -1)
    message(FATAL_ERROR "P6 master instrumentation could not locate Rack chain input pointer")
endif()
string(REPLACE
    "${_safevst3_master_current_anchor}"
    "        float** current = trimmed;"
    _safevst3_meter_main_embed
    "${_safevst3_meter_main_embed}"
)

set(_safevst3_meter_failure_anchor [=[
        if (!block_ok) {
            copy_original_dry(*endpoint.region, channels, frames);
            release_generation(store, generation_index);
            publish_result(*endpoint.region, endpoint.response, request_generation, block_result);
            continue;
        }
]=])
string(FIND "${_safevst3_meter_main_embed}" "${_safevst3_meter_failure_anchor}" _safevst3_meter_failure_pos)
if(_safevst3_meter_failure_pos EQUAL -1)
    message(FATAL_ERROR "P6 master instrumentation could not locate fail-dry seam")
endif()
set(_safevst3_meter_failure_replace [=[
        if (!block_ok) {
            // `trimmed` still holds the post-input-trim dry signal because plug-ins
            // process only ping/pong. Keep the master Output Fader authoritative on
            // fail-dry so a muted/attenuated Rack can never jump to full level.
            safevst3::rack::ui::rack_apply_smoothed_gain(
                trimmed, trimmed, channels, frames, master_controls.output_db,
                master_gain_smoother.output_linear);
            const safevst3::rack::ui::RackStereoPeaks output_meter_peaks =
                safevst3::rack::ui::rack_meter_stereo_peaks(trimmed, channels, frames);
            safevst3::rack::ui::g_rack_meter_telemetry.publish_stereo(
                sequence, input_meter_peaks, output_meter_peaks);
            safevst3::rack::ui::g_rack_broadcast_loudness.process(
                trimmed, channels, frames, endpoint.region->sample_rate, sequence);
            release_generation(store, generation_index);
            publish_result(*endpoint.region, endpoint.response, request_generation, block_result);
            continue;
        }
]=])
string(REPLACE
    "${_safevst3_meter_failure_anchor}"
    "${_safevst3_meter_failure_replace}"
    _safevst3_meter_main_embed
    "${_safevst3_meter_main_embed}"
)

set(_safevst3_meter_success_anchor [=[
        clear_rack_breadcrumb(*endpoint.region);
        copy_current_output(*endpoint.region, current, channels, frames);
        release_generation(store, generation_index);
]=])
string(FIND "${_safevst3_meter_main_embed}" "${_safevst3_meter_success_anchor}" _safevst3_meter_success_pos)
if(_safevst3_meter_success_pos EQUAL -1)
    message(FATAL_ERROR "P6 master instrumentation could not locate successful output seam")
endif()
set(_safevst3_meter_success_replace [=[
        clear_rack_breadcrumb(*endpoint.region);
        safevst3::rack::ui::rack_apply_smoothed_gain(
            current, trimmed, channels, frames, master_controls.output_db,
            master_gain_smoother.output_linear);
        const safevst3::rack::ui::RackStereoPeaks output_meter_peaks =
            safevst3::rack::ui::rack_meter_stereo_peaks(trimmed, channels, frames);
        safevst3::rack::ui::g_rack_meter_telemetry.publish_stereo(
            sequence, input_meter_peaks, output_meter_peaks);
        safevst3::rack::ui::g_rack_broadcast_loudness.process(
            trimmed, channels, frames, endpoint.region->sample_rate, sequence);
        release_generation(store, generation_index);
]=])
string(REPLACE
    "${_safevst3_meter_success_anchor}"
    "${_safevst3_meter_success_replace}"
    _safevst3_meter_main_embed
    "${_safevst3_meter_main_embed}"
)
file(WRITE "${_safevst3_r3_2_embed_root}/rack/main.cpp" "${_safevst3_meter_main_embed}")

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

# The Rack helper is visibly a child/companion application of OBS. The EXE owns
# its icon resource and the generated Win32 class explicitly supplies both large
# and small variants so title bar/taskbar/Explorer do not fall back to a blank
# generic application icon.
set(_safevst3_editor_icon_anchor [=[
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
]=])
string(FIND "${_safevst3_editor_embed}" "${_safevst3_editor_icon_anchor}" _safevst3_editor_icon_pos)
if(_safevst3_editor_icon_pos EQUAL -1)
    message(FATAL_ERROR "P3 Rack icon wiring could not locate Win32 class icon seam")
endif()
set(_safevst3_editor_icon_replace [=[
        wc.hInstance = instance;
        wc.hIcon = reinterpret_cast<HICON>(LoadImageW(
            instance, MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32,
            LR_DEFAULTCOLOR | LR_SHARED));
        wc.hIconSm = reinterpret_cast<HICON>(LoadImageW(
            instance, MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16,
            LR_DEFAULTCOLOR | LR_SHARED));
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
]=])
string(REPLACE
    "${_safevst3_editor_icon_anchor}"
    "${_safevst3_editor_icon_replace}"
    _safevst3_editor_embed
    "${_safevst3_editor_embed}"
)

set(_safevst3_editor_generated "${CMAKE_CURRENT_BINARY_DIR}/rack_editor_window_release.cpp")
file(WRITE "${_safevst3_editor_generated}" "${_safevst3_editor_embed}")

add_executable(obs-safe-vst3-rack-host
    "${CMAKE_CURRENT_LIST_DIR}/main_r3_2_hosted.cpp"
    "${_safevst3_editor_generated}"
    "${CMAKE_CURRENT_LIST_DIR}/rack_host.rc"
    "${CMAKE_CURRENT_LIST_DIR}/rack_plugin_catalog.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/rack_preset_library.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/rack_preset_management.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/rack_session_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/rack_vendor_editor_manager.cpp"
    "${SAFEVST3_RACK_ROOT}/src/host/hosted_plugin.cpp"
    "${SAFEVST3_RACK_ROOT}/src/host/native_editor.cpp"
)

# P8 is the minimal-noise luxury overlay on top of qualified P7. It keeps the
# stable no-flicker topology transition while making each slot LED/name the
# primary controls and refining the master hardware surface.
set_source_files_properties(
    "${_safevst3_editor_generated}"
    PROPERTIES
        COMPILE_OPTIONS "/FI${CMAKE_CURRENT_LIST_DIR}/rack_editor_p8_refined.hpp"
)

target_include_directories(obs-safe-vst3-rack-host BEFORE PRIVATE
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

if(SAFEVST3_BUILD_TESTS)
    add_executable(safevst3-rack-broadcast-meter-contract
        "${SAFEVST3_RACK_ROOT}/tests/p1_rack_editor/broadcast_meter_contract.cpp")
    target_include_directories(safevst3-rack-broadcast-meter-contract PRIVATE
        "${SAFEVST3_RACK_ROOT}/src")
    add_test(NAME safevst3-rack-broadcast-meter-contract
             COMMAND safevst3-rack-broadcast-meter-contract)

    add_executable(safevst3-rack-master-gain-contract
        "${SAFEVST3_RACK_ROOT}/tests/p1_rack_editor/master_gain_contract.cpp")
    target_include_directories(safevst3-rack-master-gain-contract PRIVATE
        "${SAFEVST3_RACK_ROOT}/src")
    add_test(NAME safevst3-rack-master-gain-contract
             COMMAND safevst3-rack-master-gain-contract)

    # Any qualified Rack build also compiles the numerical broadcast and master
    # probes, so focused compatibility jobs cannot silently skip the cores.
    add_dependencies(obs-safe-vst3-rack-host
        safevst3-rack-broadcast-meter-contract
        safevst3-rack-master-gain-contract)
endif()

if(TARGET obs-safe-vst3-scanner)
    add_dependencies(obs-safe-vst3-rack-host obs-safe-vst3-scanner)
endif()