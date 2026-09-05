#pragma once

#ifdef _WIN32

// Product skin is intentionally isolated to the Rack Editor translation unit.
// This header is force-included only for rack_editor_window.cpp so visual/layout
// polish cannot leak into the DSP/control host, OBS module or scanner.
//
// P2 keeps the qualified command/snapshot implementation intact and reshapes
// only its presentation into a compact serial-rack console: dense effect strips
// on the left and a read-only Rack status / meter-ready surface on the right.
// Live level telemetry is deliberately not fabricated here; the current
// RackUiSnapshot does not publish bounded meter values yet.

#include "rack/rack_editor_window.hpp"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <d3d11.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace safevst3::rack::ui::p1 {

inline ImFont* regular_font = nullptr;
inline ImFont* semibold_font = nullptr;
inline ImFont* heading_font = nullptr;
inline thread_local int slot_child_style_depth = 0;

inline constexpr float kSlotCardHeight = 82.0f;
inline constexpr float kConsoleSplitThreshold = 680.0f;
inline constexpr float kConsoleGap = 10.0f;

inline ImVec4 accent() noexcept { return ImVec4(0.05f, 0.70f, 0.79f, 1.0f); }
inline ImVec4 accent_hover() noexcept { return ImVec4(0.09f, 0.80f, 0.88f, 1.0f); }
inline ImVec4 accent_active() noexcept { return ImVec4(0.03f, 0.58f, 0.68f, 1.0f); }
inline ImVec4 meter_warm() noexcept { return ImVec4(0.96f, 0.57f, 0.10f, 1.0f); }
inline ImVec4 text_primary() noexcept { return ImVec4(0.93f, 0.95f, 0.95f, 1.0f); }
inline ImVec4 text_muted() noexcept { return ImVec4(0.48f, 0.53f, 0.54f, 1.0f); }
inline ImVec4 ready() noexcept { return ImVec4(0.09f, 0.77f, 0.81f, 1.0f); }
inline ImVec4 warning() noexcept { return ImVec4(0.96f, 0.64f, 0.21f, 1.0f); }
inline ImVec4 danger() noexcept { return ImVec4(0.94f, 0.34f, 0.38f, 1.0f); }

struct ConsoleFrameState {
    std::string rack_name = "VST3 Rack";
    std::uint32_t slot_count = 0;
    std::uint32_t latency_samples = 0;
    int health_rank = 0;
    bool pending = false;
    bool split_started = false;
    float pane_height = 0.0f;
};

inline thread_local ConsoleFrameState console_frame{};

inline void reset_console_frame(std::string_view rack_name)
{
    console_frame = ConsoleFrameState{};
    if (!rack_name.empty())
        console_frame.rack_name.assign(rack_name.begin(), rack_name.end());
}

inline ImVec4 health_color(const char* health) noexcept
{
    if (!health)
        return text_muted();
    if (std::strcmp(health, "Ready") == 0)
        return ready();
    if (std::strcmp(health, "Bypassed") == 0)
        return text_muted();
    if (std::strcmp(health, "Loading") == 0 ||
        std::strcmp(health, "Recovering") == 0)
        return warning();
    if (std::strcmp(health, "Missing") == 0 ||
        std::strcmp(health, "Needs Attention") == 0 ||
        std::strcmp(health, "Quarantined") == 0)
        return danger();
    return text_muted();
}

inline void observe_health(const char* health) noexcept
{
    if (!health)
        return;
    if (std::strcmp(health, "Missing") == 0 ||
        std::strcmp(health, "Needs Attention") == 0 ||
        std::strcmp(health, "Quarantined") == 0) {
        console_frame.health_rank = 2;
        return;
    }
    if (console_frame.health_rank < 1 &&
        (std::strcmp(health, "Loading") == 0 ||
         std::strcmp(health, "Recovering") == 0))
        console_frame.health_rank = 1;
}

inline const char* aggregate_health_text() noexcept
{
    if (console_frame.pending)
        return "PENDING";
    if (console_frame.health_rank >= 2)
        return "ATTENTION";
    if (console_frame.health_rank == 1)
        return "RECOVERING";
    if (console_frame.slot_count == 0)
        return "EMPTY";
    return "READY";
}

inline ImVec4 aggregate_health_color() noexcept
{
    if (console_frame.pending || console_frame.health_rank == 1)
        return warning();
    if (console_frame.health_rank >= 2)
        return danger();
    if (console_frame.slot_count == 0)
        return text_muted();
    return ready();
}

inline ImFont* add_windows_font(ImGuiIO& io, const char* path, float size)
{
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
        return nullptr;
    ImFontConfig config{};
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.PixelSnapH = false;
    return io.Fonts->AddFontFromFileTTF(path, size, &config);
}

inline void install_fonts()
{
    ImGuiIO& io = ImGui::GetIO();
    regular_font = add_windows_font(io, "C:\\Windows\\Fonts\\segoeui.ttf", 14.5f);
    semibold_font = add_windows_font(io, "C:\\Windows\\Fonts\\seguisb.ttf", 15.0f);
    heading_font = add_windows_font(io, "C:\\Windows\\Fonts\\seguisb.ttf", 19.0f);

    if (!regular_font)
        regular_font = io.Fonts->AddFontDefault();
    if (!semibold_font)
        semibold_font = regular_font;
    if (!heading_font)
        heading_font = semibold_font;
    io.FontDefault = regular_font;
}

inline bool label_is(const char* label, const char* expected) noexcept
{
    return label && expected && std::strcmp(label, expected) == 0;
}

inline void render_meter_ready_surface()
{
    const float width = std::max(180.0f, ImGui::GetContentRegionAvail().x);
    const float available_height = ImGui::GetContentRegionAvail().y;
    const float meter_height = std::clamp(available_height * 0.48f, 150.0f, 250.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const float scale_width = 38.0f;
    const float track_width = 18.0f;
    const float usable = std::max(120.0f, width - scale_width - 12.0f);
    const float spacing = usable / 3.0f;
    const ImU32 grid = ImGui::ColorConvertFloat4ToU32(ImVec4(0.19f, 0.22f, 0.22f, 0.72f));
    const ImU32 track_bg = ImGui::ColorConvertFloat4ToU32(ImVec4(0.018f, 0.023f, 0.024f, 1.0f));
    const ImU32 warm_border = ImGui::ColorConvertFloat4ToU32(
        ImVec4(meter_warm().x, meter_warm().y, meter_warm().z, 0.52f));
    const ImU32 cool_border = ImGui::ColorConvertFloat4ToU32(
        ImVec4(accent().x, accent().y, accent().z, 0.52f));
    const ImU32 muted = ImGui::ColorConvertFloat4ToU32(text_muted());

    struct Tick {
        const char* label;
        float ratio;
    };
    constexpr Tick ticks[] = {
        {"0", 0.0f}, {"-6", 0.15f}, {"-12", 0.30f},
        {"-24", 0.52f}, {"-36", 0.70f}, {"-48", 0.86f}, {"-60", 1.0f},
    };

    for (const Tick& tick : ticks) {
        const float y = origin.y + meter_height * tick.ratio;
        draw->AddLine(ImVec2(origin.x + scale_width, y), ImVec2(origin.x + width - 3.0f, y), grid, 1.0f);
        draw->AddText(ImVec2(origin.x, y - 7.0f), muted, tick.label);
    }

    const char* labels[] = {"IN", "OUT", "GR"};
    for (int index = 0; index < 3; ++index) {
        const float center_x = origin.x + scale_width + spacing * (static_cast<float>(index) + 0.5f);
        const ImVec2 min(center_x - track_width * 0.5f, origin.y);
        const ImVec2 max(center_x + track_width * 0.5f, origin.y + meter_height);
        draw->AddRectFilled(min, max, track_bg, 4.0f);
        draw->AddRect(min, max, index == 2 ? cool_border : warm_border, 4.0f, 0, 1.2f);
        const ImVec2 label_size = ImGui::CalcTextSize(labels[index]);
        draw->AddText(ImVec2(center_x - label_size.x * 0.5f, max.y + 8.0f), muted, labels[index]);
        const ImVec2 empty_size = ImGui::CalcTextSize("--");
        draw->AddText(ImVec2(center_x - empty_size.x * 0.5f, max.y + 27.0f), muted, "--");
    }

    ImGui::Dummy(ImVec2(width, meter_height + 48.0f));
    ImGui::TextDisabled("Live meter telemetry is not exposed by the current Rack UI snapshot.");
}

inline void render_master_console()
{
    if (heading_font)
        ImGui::PushFont(heading_font);
    ImGui::TextColored(text_primary(), "SAFE VST3");
    if (heading_font)
        ImGui::PopFont();
    ImGui::TextDisabled("RACK / ISOLATED HOST");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(aggregate_health_color(), "● %s", aggregate_health_text());
    if (!console_frame.rack_name.empty())
        ImGui::TextColored(text_primary(), "%s", console_frame.rack_name.c_str());
    ImGui::TextDisabled("%u effects", console_frame.slot_count);
    ImGui::TextDisabled("%u samples latency", console_frame.latency_samples);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("METER TELEMETRY");
    render_meter_ready_surface();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("OUTPUT");
    ImGui::TextColored(text_primary(), "TO OBS");
}

} // namespace safevst3::rack::ui::p1

namespace ImGui {

inline void SafeVst3P1StyleColorsDark(ImGuiStyle* dst = nullptr)
{
    // Keep Dear ImGui as a known base and replace only the helper-owned product
    // surface. No rendering loop, Rack model or host timing changes occur here.
    ImGui::StyleColorsDark(dst);
    ImGuiStyle& style = dst ? *dst : ImGui::GetStyle();
    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    safevst3::rack::ui::p1::install_fonts();

    style.WindowPadding = ImVec2(13.0f, 11.0f);
    style.FramePadding = ImVec2(9.0f, 5.0f);
    style.CellPadding = ImVec2(7.0f, 4.0f);
    style.ItemSpacing = ImVec2(7.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 11.0f;
    style.GrabMinSize = 10.0f;

    style.WindowRounding = 0.0f;
    style.ChildRounding = 9.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 7.0f;
    style.ScrollbarRounding = 7.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 5.0f;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = safevst3::rack::ui::p1::text_primary();
    colors[ImGuiCol_TextDisabled] = safevst3::rack::ui::p1::text_muted();
    colors[ImGuiCol_WindowBg] = ImVec4(0.025f, 0.031f, 0.032f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.038f, 0.046f, 0.047f, 1.0f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.032f, 0.039f, 0.040f, 0.995f);
    colors[ImGuiCol_Border] = ImVec4(0.13f, 0.15f, 0.15f, 1.0f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg] = ImVec4(0.045f, 0.054f, 0.055f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.062f, 0.078f, 0.080f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.071f, 0.090f, 0.092f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.025f, 0.031f, 0.032f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.025f, 0.031f, 0.032f, 1.0f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.025f, 0.031f, 0.032f, 1.0f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.018f, 0.023f, 0.024f, 1.0f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.13f, 0.16f, 0.16f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.19f, 0.23f, 0.23f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive] = safevst3::rack::ui::p1::accent();
    colors[ImGuiCol_CheckMark] = safevst3::rack::ui::p1::accent();
    colors[ImGuiCol_SliderGrab] = safevst3::rack::ui::p1::accent();
    colors[ImGuiCol_SliderGrabActive] = safevst3::rack::ui::p1::accent_hover();
    colors[ImGuiCol_Button] = ImVec4(0.055f, 0.066f, 0.067f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.078f, 0.102f, 0.104f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.063f, 0.088f, 0.090f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.061f, 0.083f, 0.085f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.080f, 0.116f, 0.120f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.092f, 0.137f, 0.142f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(0.12f, 0.14f, 0.14f, 1.0f);
    colors[ImGuiCol_SeparatorHovered] = safevst3::rack::ui::p1::accent();
    colors[ImGuiCol_SeparatorActive] = safevst3::rack::ui::p1::accent_active();
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.17f, 0.20f, 0.20f, 0.30f);
    colors[ImGuiCol_ResizeGripHovered] = safevst3::rack::ui::p1::accent();
    colors[ImGuiCol_ResizeGripActive] = safevst3::rack::ui::p1::accent_active();
    colors[ImGuiCol_NavHighlight] = safevst3::rack::ui::p1::accent();
}

inline bool SafeVst3P1BeginChild(const char* str_id,
                                 const ImVec2& size_arg = ImVec2(0, 0),
                                 ImGuiChildFlags child_flags = 0,
                                 ImGuiWindowFlags window_flags = 0)
{
    const bool is_slot = str_id && std::strcmp(str_id, "rack-slot-card") == 0;
    ImVec2 size = size_arg;
    if (is_slot) {
        size.y = safevst3::rack::ui::p1::kSlotCardHeight;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(11.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 9.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.040f, 0.049f, 0.050f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.13f, 0.16f, 0.16f, 1.0f));
        ++safevst3::rack::ui::p1::slot_child_style_depth;
    }

    const bool visible = ImGui::BeginChild(str_id, size, child_flags, window_flags);
    if (is_slot && visible) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetWindowPos();
        const ImVec2 s = ImGui::GetWindowSize();
        draw->AddRectFilled(
            ImVec2(p.x + 1.0f, p.y + 10.0f),
            ImVec2(p.x + 4.0f, p.y + s.y - 10.0f),
            ImGui::ColorConvertFloat4ToU32(safevst3::rack::ui::p1::accent()),
            2.0f);
    }
    return visible;
}

inline void SafeVst3P1EndChild()
{
    ImGui::EndChild();
    if (safevst3::rack::ui::p1::slot_child_style_depth > 0) {
        --safevst3::rack::ui::p1::slot_child_style_depth;
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }
}

inline bool SafeVst3P1Button(const char* label, const ImVec2& size_arg = ImVec2(0, 0))
{
    using namespace safevst3::rack::ui::p1;
    const bool in_slot = slot_child_style_depth > 0;
    const bool active_toggle = in_slot && label_is(label, "Bypass");
    const bool bypassed_toggle = in_slot && label_is(label, "Enable");
    const bool slot_ui = in_slot && label_is(label, "Open UI");
    const bool slot_menu = in_slot && label_is(label, "...");
    const bool primary = label_is(label, "Add Effect") || label_is(label, "Save As") ||
                         label_is(label, "Update") || bypassed_toggle;
    const bool destructive = label_is(label, "Delete");

    const char* display_label = label;
    if (active_toggle)
        display_label = "ON##slot-enable-state";
    else if (bypassed_toggle)
        display_label = "OFF##slot-enable-state";
    else if (slot_ui)
        display_label = "UI##slot-open-ui";
    else if (slot_menu)
        display_label = "+##slot-actions";
    else if (label_is(label, "Add Effect"))
        display_label = "+  ADD EFFECT##add-effect";
    else if (label_is(label, "Refresh"))
        display_label = "RESCAN##refresh";
    else if (label_is(label, "Save As"))
        display_label = "SAVE AS##save-as";
    else if (label_is(label, "Update"))
        display_label = "UPDATE##update";
    else if (label_is(label, "Preset ..."))
        display_label = "...##preset-actions";

    ImVec2 size = size_arg;
    if (in_slot) {
        size.y = 24.0f;
        if (active_toggle || bypassed_toggle)
            size.x = 46.0f;
        else if (slot_ui)
            size.x = 36.0f;
        else if (slot_menu)
            size.x = 30.0f;
    } else if (size.y <= 0.0f) {
        size.y = 28.0f;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, in_slot ? 4.0f : 5.0f);
    int pushed_colors = 0;
    if (active_toggle) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.04f, 0.37f, 0.40f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.05f, 0.50f, 0.54f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent_active());
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 1.0f, 1.0f, 1.0f));
        pushed_colors = 4;
    } else if (bypassed_toggle) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.13f, 0.13f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.19f, 0.21f, 0.21f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, text_muted());
        pushed_colors = 4;
    } else if (slot_ui) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.04f, 0.12f, 0.13f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.05f, 0.25f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent_active());
        ImGui::PushStyleColor(ImGuiCol_Text, accent());
        pushed_colors = 4;
    } else if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.04f, 0.36f, 0.40f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.05f, 0.47f, 0.52f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent_active());
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 1.0f, 1.0f, 1.0f));
        pushed_colors = 4;
    } else if (destructive) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.38f, 0.10f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.54f, 0.14f, 0.17f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.09f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.93f, 0.94f, 1.0f));
        pushed_colors = 4;
    }

    const bool pressed = ImGui::Button(display_label, size);

    if (ImGui::IsItemHovered()) {
        if (active_toggle)
            ImGui::SetTooltip("Effect enabled — click to bypass");
        else if (bypassed_toggle)
            ImGui::SetTooltip("Effect bypassed — click to enable");
        else if (slot_ui)
            ImGui::SetTooltip("Open plug-in interface");
        else if (slot_menu)
            ImGui::SetTooltip("Slot actions");
    }

    if (pushed_colors)
        ImGui::PopStyleColor(pushed_colors);
    ImGui::PopStyleVar();
    return pressed;
}

inline void SafeVst3P1Text(const char* fmt, ...)
{
    using namespace safevst3::rack::ui::p1;
    if (!fmt)
        return;

    va_list args;
    va_start(args, fmt);

    if (std::strcmp(fmt, "OBS Safe VST3 Rack — %s") == 0) {
        const char* rack = va_arg(args, const char*);
        reset_console_frame(rack ? rack : "VST3 Rack");
        if (heading_font)
            ImGui::PushFont(heading_font);
        ImGui::TextColored(text_primary(), "SAFE VST3  /  %s", rack ? rack : "Rack");
        if (heading_font)
            ImGui::PopFont();
        va_end(args);
        return;
    }

    if (std::strcmp(fmt, "%u  %s") == 0) {
        const unsigned index = va_arg(args, unsigned);
        const char* name = va_arg(args, const char*);

        const ImVec2 lamp = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            lamp, ImVec2(lamp.x + 14.0f, lamp.y + 7.0f),
            ImGui::ColorConvertFloat4ToU32(accent()), 3.0f);
        ImGui::Dummy(ImVec2(15.0f, 8.0f));
        ImGui::SameLine(0.0f, 7.0f);
        ImGui::TextColored(text_muted(), "%02u", index);
        ImGui::SameLine(0.0f, 8.0f);
        if (semibold_font)
            ImGui::PushFont(semibold_font);
        ImGui::TextUnformatted(name ? name : "VST3 Effect");
        if (semibold_font)
            ImGui::PopFont();
        va_end(args);
        return;
    }

    if (std::strcmp(fmt, "%s · %u samples%s") == 0) {
        const char* health = va_arg(args, const char*);
        const unsigned latency = va_arg(args, unsigned);
        const char* suffix = va_arg(args, const char*);
        observe_health(health);
        ImGui::TextColored(health_color(health), "%s", health ? health : "Unknown");
        ImGui::SameLine(0.0f, 7.0f);
        ImGui::TextDisabled("· %u smp%s", latency, suffix ? suffix : "");
        va_end(args);
        return;
    }

    ImGui::TextV(fmt, args);
    va_end(args);
}

inline void SafeVst3P2TextDisabled(const char* fmt, ...)
{
    using namespace safevst3::rack::ui::p1;
    if (!fmt)
        return;

    va_list args;
    va_start(args, fmt);
    if (std::strcmp(fmt, "%u effects · %u samples") == 0) {
        const unsigned slot_count = va_arg(args, unsigned);
        const unsigned latency = va_arg(args, unsigned);
        console_frame.slot_count = slot_count;
        console_frame.latency_samples = latency;
        ImGui::TextColored(text_muted(), "%u FX  ·  %u smp", slot_count, latency);
        va_end(args);
        return;
    }

    ImGui::TextDisabledV(fmt, args);
    va_end(args);
}

inline void SafeVst3P1TextUnformatted(const char* text, const char* text_end = nullptr)
{
    using namespace safevst3::rack::ui::p1;
    if (!text) {
        ImGui::TextUnformatted(text, text_end);
        return;
    }

    if (!text_end && std::strcmp(text, "OBS Safe VST3 Rack") == 0) {
        reset_console_frame("VST3 Rack");
        if (heading_font)
            ImGui::PushFont(heading_font);
        ImGui::TextColored(text_primary(), "SAFE VST3  /  RACK");
        if (heading_font)
            ImGui::PopFont();
        return;
    }

    if (!text_end && std::strcmp(text, "Pending...") == 0) {
        console_frame.pending = true;
        ImGui::TextColored(warning(), "● PENDING");
        return;
    }

    if (!text_end && std::strcmp(text, "INPUT") == 0) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (available.x >= kConsoleSplitThreshold) {
            const float right_width = std::clamp(available.x * 0.34f, 245.0f, 310.0f);
            const float left_width = std::max(360.0f, available.x - right_width - kConsoleGap);
            console_frame.pane_height = std::max(260.0f, available.y);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.031f, 0.038f, 0.039f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.11f, 0.14f, 0.14f, 1.0f));
            ImGui::BeginChild("rack-console-lane", ImVec2(left_width, console_frame.pane_height),
                              ImGuiChildFlags_Borders);
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            console_frame.split_started = true;
        }

        ImGui::TextColored(text_muted(), "SIGNAL CHAIN");
        ImGui::SameLine(0.0f, 7.0f);
        ImGui::TextColored(accent(), "INPUT");
        return;
    }

    if (!text_end && std::strcmp(text, "OUTPUT TO OBS") == 0) {
        ImGui::Spacing();
        ImGui::TextColored(text_muted(), "OUTPUT");
        ImGui::SameLine(0.0f, 7.0f);
        ImGui::TextColored(accent(), "TO OBS");

        if (console_frame.split_started) {
            ImGui::EndChild();
            ImGui::SameLine(0.0f, kConsoleGap);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(17.0f, 15.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.028f, 0.034f, 0.035f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.11f, 0.14f, 0.14f, 1.0f));
            ImGui::BeginChild("rack-console-master", ImVec2(0.0f, console_frame.pane_height),
                              ImGuiChildFlags_Borders);
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);

            render_master_console();
            ImGui::EndChild();
            console_frame.split_started = false;
        }
        return;
    }

    if (!text_end && (std::strcmp(text, "Add Effect") == 0 ||
                      std::strcmp(text, "Replace Effect") == 0)) {
        if (semibold_font)
            ImGui::PushFont(semibold_font);
        ImGui::TextUnformatted(text);
        if (semibold_font)
            ImGui::PopFont();
        return;
    }

    ImGui::TextUnformatted(text, text_end);
}

} // namespace ImGui

// Source-local redirection. Because this header force-includes the complete
// dependency set before these macros, declarations in Win32/ImGui/std headers
// are not rewritten. Only calls made by rack_editor_window.cpp are skinned.
#define StyleColorsDark SafeVst3P1StyleColorsDark
#define BeginChild SafeVst3P1BeginChild
#define EndChild SafeVst3P1EndChild
#define Button SafeVst3P1Button
#define Text SafeVst3P1Text
#define TextDisabled SafeVst3P2TextDisabled
#define TextUnformatted SafeVst3P1TextUnformatted

#endif // _WIN32
