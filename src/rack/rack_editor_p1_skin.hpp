#pragma once

#ifdef _WIN32

// P1 is intentionally isolated to the Rack Editor translation unit.  This
// header is force-included only for rack_editor_window.cpp so product polish
// cannot leak into the DSP/control host or the scanner.

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

inline constexpr float kSlotCardHeight = 96.0f;

inline ImVec4 accent() noexcept { return ImVec4(0.34f, 0.56f, 0.96f, 1.0f); }
inline ImVec4 accent_hover() noexcept { return ImVec4(0.42f, 0.64f, 1.00f, 1.0f); }
inline ImVec4 accent_active() noexcept { return ImVec4(0.27f, 0.48f, 0.88f, 1.0f); }
inline ImVec4 text_primary() noexcept { return ImVec4(0.92f, 0.94f, 0.97f, 1.0f); }
inline ImVec4 text_muted() noexcept { return ImVec4(0.52f, 0.57f, 0.64f, 1.0f); }
inline ImVec4 ready() noexcept { return ImVec4(0.32f, 0.80f, 0.58f, 1.0f); }
inline ImVec4 warning() noexcept { return ImVec4(0.96f, 0.69f, 0.31f, 1.0f); }
inline ImVec4 danger() noexcept { return ImVec4(0.94f, 0.37f, 0.42f, 1.0f); }

inline ImVec4 health_color(const char* health) noexcept
{
    if (!health)
        return text_muted();
    if (std::strcmp(health, "Ready") == 0)
        return ready();
    if (std::strcmp(health, "Bypassed") == 0)
        return warning();
    if (std::strcmp(health, "Loading") == 0 ||
        std::strcmp(health, "Recovering") == 0)
        return accent();
    if (std::strcmp(health, "Missing") == 0 ||
        std::strcmp(health, "Needs Attention") == 0 ||
        std::strcmp(health, "Quarantined") == 0)
        return danger();
    return text_muted();
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
    regular_font = add_windows_font(io, "C:\\Windows\\Fonts\\segoeui.ttf", 15.0f);
    semibold_font = add_windows_font(io, "C:\\Windows\\Fonts\\seguisb.ttf", 15.5f);
    heading_font = add_windows_font(io, "C:\\Windows\\Fonts\\seguisb.ttf", 18.0f);

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

} // namespace safevst3::rack::ui::p1

namespace ImGui {

inline void SafeVst3P1StyleColorsDark(ImGuiStyle* dst = nullptr)
{
    // Preserve Dear ImGui defaults as a known base, then intentionally replace
    // the visible product surface.  No rendering loop or host timing changes.
    ImGui::StyleColorsDark(dst);
    ImGuiStyle& style = dst ? *dst : ImGui::GetStyle();
    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    safevst3::rack::ui::p1::install_fonts();

    style.WindowPadding = ImVec2(16.0f, 14.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.CellPadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

    style.WindowRounding = 0.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = safevst3::rack::ui::p1::text_primary();
    colors[ImGuiCol_TextDisabled] = safevst3::rack::ui::p1::text_muted();
    colors[ImGuiCol_WindowBg] = ImVec4(0.040f, 0.047f, 0.058f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.058f, 0.067f, 0.081f, 1.0f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.048f, 0.055f, 0.068f, 0.99f);
    colors[ImGuiCol_Border] = ImVec4(0.15f, 0.17f, 0.21f, 1.0f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg] = ImVec4(0.072f, 0.082f, 0.100f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.095f, 0.113f, 0.142f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.105f, 0.128f, 0.165f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.040f, 0.047f, 0.058f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.040f, 0.047f, 0.058f, 1.0f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.040f, 0.047f, 0.058f, 1.0f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.035f, 0.041f, 0.050f, 1.0f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.18f, 0.21f, 0.27f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.24f, 0.29f, 0.37f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive] = safevst3::rack::ui::p1::accent();
    colors[ImGuiCol_CheckMark] = safevst3::rack::ui::p1::accent();
    colors[ImGuiCol_SliderGrab] = safevst3::rack::ui::p1::accent();
    colors[ImGuiCol_SliderGrabActive] = safevst3::rack::ui::p1::accent_hover();
    colors[ImGuiCol_Button] = ImVec4(0.095f, 0.112f, 0.142f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.135f, 0.166f, 0.216f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.115f, 0.143f, 0.190f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.110f, 0.137f, 0.180f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.145f, 0.185f, 0.250f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.170f, 0.220f, 0.310f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(0.13f, 0.15f, 0.19f, 1.0f);
    colors[ImGuiCol_SeparatorHovered] = safevst3::rack::ui::p1::accent();
    colors[ImGuiCol_SeparatorActive] = safevst3::rack::ui::p1::accent_active();
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.20f, 0.24f, 0.31f, 0.35f);
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
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 11.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.057f, 0.066f, 0.080f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.145f, 0.17f, 0.215f, 1.0f));
        ++safevst3::rack::ui::p1::slot_child_style_depth;
    }

    const bool visible = ImGui::BeginChild(str_id, size, child_flags, window_flags);
    if (is_slot && visible) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetWindowPos();
        const ImVec2 s = ImGui::GetWindowSize();
        draw->AddRectFilled(
            ImVec2(p.x + 1.0f, p.y + 8.0f),
            ImVec2(p.x + 4.0f, p.y + s.y - 8.0f),
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
    const bool primary = label_is(label, "Add Effect") || label_is(label, "Save As") ||
                         label_is(label, "Update") || label_is(label, "Enable") ||
                         label_is(label, "Open UI");
    const bool destructive = label_is(label, "Delete");

    ImVec2 size = size_arg;
    if (size.y <= 0.0f)
        size.y = 30.0f;
    if (label_is(label, "...") && size.x <= 0.0f)
        size.x = 34.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, accent());
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent_hover());
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent_active());
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.97f, 0.98f, 1.0f, 1.0f));
    } else if (destructive) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42f, 0.12f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.16f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.48f, 0.10f, 0.14f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.93f, 0.94f, 1.0f));
    }

    const bool pressed = ImGui::Button(label, size);
    if (primary || destructive)
        ImGui::PopStyleColor(4);
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
        ImGui::TextColored(accent(), "%02u", index);
        ImGui::SameLine(0.0f, 9.0f);
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
        ImGui::TextColored(health_color(health), "%s", health ? health : "Unknown");
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextDisabled("• %u samples%s", latency, suffix ? suffix : "");
        va_end(args);
        return;
    }

    ImGui::TextV(fmt, args);
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
        if (heading_font)
            ImGui::PushFont(heading_font);
        ImGui::TextColored(text_primary(), "SAFE VST3  /  RACK");
        if (heading_font)
            ImGui::PopFont();
        return;
    }
    if (!text_end && std::strcmp(text, "INPUT") == 0) {
        ImGui::TextColored(text_muted(), "SIGNAL CHAIN");
        ImGui::SameLine(0.0f, 7.0f);
        ImGui::TextDisabled("INPUT");
        return;
    }
    if (!text_end && std::strcmp(text, "OUTPUT TO OBS") == 0) {
        ImGui::TextColored(text_muted(), "OUTPUT");
        ImGui::SameLine(0.0f, 7.0f);
        ImGui::TextDisabled("TO OBS");
        return;
    }
    if (!text_end && std::strcmp(text, "Pending...") == 0) {
        ImGui::TextColored(warning(), "• Pending");
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

// Source-local redirection.  Because this header force-includes the complete
// dependency set before these macros, declarations in Win32/ImGui/std headers
// are not rewritten.  Only calls made by rack_editor_window.cpp are skinned.
#define StyleColorsDark SafeVst3P1StyleColorsDark
#define BeginChild SafeVst3P1BeginChild
#define EndChild SafeVst3P1EndChild
#define Button SafeVst3P1Button
#define Text SafeVst3P1Text
#define TextUnformatted SafeVst3P1TextUnformatted

#endif // _WIN32
