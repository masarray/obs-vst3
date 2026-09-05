#pragma once

#ifdef _WIN32

// P3 premium product pass. This header layers on top of the qualified P2 skin
// and remains force-included only into the Rack Editor translation unit.
// It changes presentation only: no Rack DSP, protocol, persistence, scanner,
// topology or vendor-hosting behavior is touched.

#include "rack/rack_editor_p1_skin.hpp"

#undef StyleColorsDark
#undef BeginChild
#undef EndChild
#undef Button
#undef Text
#undef TextDisabled
#undef TextUnformatted

namespace safevst3::rack::ui::p3 {

using namespace safevst3::rack::ui::p1;

inline constexpr float kPremiumSlotHeight = 74.0f;
inline constexpr float kPremiumSplitThreshold = 680.0f;
inline constexpr float kPremiumConsoleShare = 0.40f;
inline constexpr float kPremiumConsoleGap = 11.0f;

inline ImVec4 surface_app() noexcept { return ImVec4(0.020f, 0.025f, 0.026f, 1.0f); }
inline ImVec4 surface_lane() noexcept { return ImVec4(0.028f, 0.034f, 0.035f, 1.0f); }
inline ImVec4 surface_slot() noexcept { return ImVec4(0.040f, 0.047f, 0.048f, 1.0f); }
inline ImVec4 surface_console() noexcept { return ImVec4(0.024f, 0.030f, 0.031f, 1.0f); }
inline ImVec4 hairline() noexcept { return ImVec4(0.115f, 0.135f, 0.136f, 0.82f); }

inline void draw_section_label(const char* left, const char* accent_text)
{
    ImGui::TextColored(text_muted(), "%s", left);
    ImGui::SameLine(0.0f, 7.0f);
    ImGui::TextColored(accent(), "%s", accent_text);
}

inline void render_kpi_row()
{
    const float width = ImGui::GetContentRegionAvail().x;
    const float gap = 9.0f;
    const float card = std::max(96.0f, (width - gap) * 0.5f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 9.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.038f, 0.046f, 0.047f, 1.0f));
    ImGui::BeginChild("premium-kpi-effects", ImVec2(card, 59.0f), ImGuiChildFlags_None);
    ImGui::TextDisabled("EFFECTS");
    if (semibold_font)
        ImGui::PushFont(semibold_font);
    ImGui::TextColored(text_primary(), "%u", console_frame.slot_count);
    if (semibold_font)
        ImGui::PopFont();
    ImGui::EndChild();

    ImGui::SameLine(0.0f, gap);
    ImGui::BeginChild("premium-kpi-latency", ImVec2(0.0f, 59.0f), ImGuiChildFlags_None);
    ImGui::TextDisabled("LATENCY");
    if (semibold_font)
        ImGui::PushFont(semibold_font);
    ImGui::TextColored(text_primary(), "%u smp", console_frame.latency_samples);
    if (semibold_font)
        ImGui::PopFont();
    ImGui::EndChild();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

inline void render_premium_meter_surface()
{
    const float width = std::max(210.0f, ImGui::GetContentRegionAvail().x);
    const float available_height = ImGui::GetContentRegionAvail().y;
    const float meter_height = std::clamp(available_height * 0.44f, 150.0f, 235.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const float scale_width = 32.0f;
    const float usable = std::max(150.0f, width - scale_width - 8.0f);
    const float spacing = usable / 3.0f;
    const float track_width = 12.0f;
    const ImU32 grid = ImGui::ColorConvertFloat4ToU32(ImVec4(0.14f, 0.165f, 0.165f, 0.58f));
    const ImU32 track_bg = ImGui::ColorConvertFloat4ToU32(ImVec4(0.012f, 0.016f, 0.017f, 1.0f));
    const ImU32 warm = ImGui::ColorConvertFloat4ToU32(
        ImVec4(meter_warm().x, meter_warm().y, meter_warm().z, 0.44f));
    const ImU32 cool = ImGui::ColorConvertFloat4ToU32(
        ImVec4(accent().x, accent().y, accent().z, 0.50f));
    const ImU32 muted = ImGui::ColorConvertFloat4ToU32(text_muted());

    struct Tick { const char* label; float ratio; };
    constexpr Tick ticks[] = {
        {"0", 0.0f}, {"-12", 0.26f}, {"-24", 0.49f},
        {"-36", 0.68f}, {"-48", 0.84f}, {"-60", 1.0f},
    };

    for (const Tick& tick : ticks) {
        const float y = origin.y + meter_height * tick.ratio;
        draw->AddLine(ImVec2(origin.x + scale_width, y),
                      ImVec2(origin.x + width - 2.0f, y), grid, 1.0f);
        draw->AddText(ImVec2(origin.x, y - 7.0f), muted, tick.label);
    }

    const char* labels[] = {"IN", "OUT", "GR"};
    for (int index = 0; index < 3; ++index) {
        const float center_x =
            origin.x + scale_width + spacing * (static_cast<float>(index) + 0.5f);
        const ImVec2 min(center_x - track_width * 0.5f, origin.y);
        const ImVec2 max(center_x + track_width * 0.5f, origin.y + meter_height);

        draw->AddRectFilled(min, max, track_bg, 5.0f);
        draw->AddRect(min, max, index == 2 ? cool : warm, 5.0f, 0, 1.0f);

        const ImVec2 label_size = ImGui::CalcTextSize(labels[index]);
        draw->AddText(ImVec2(center_x - label_size.x * 0.5f, max.y + 9.0f),
                      muted, labels[index]);
        const ImVec2 empty_size = ImGui::CalcTextSize("--");
        draw->AddText(ImVec2(center_x - empty_size.x * 0.5f, max.y + 29.0f),
                      muted, "--");
    }

    ImGui::Dummy(ImVec2(width, meter_height + 50.0f));
}

inline void render_premium_master_console()
{
    if (semibold_font)
        ImGui::PushFont(semibold_font);
    ImGui::TextColored(text_primary(), "RACK STATUS");
    if (semibold_font)
        ImGui::PopFont();

    ImGui::TextDisabled("SAFE VST3 / ISOLATED HOST");

    const ImVec2 line_start = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(
        line_start,
        ImVec2(line_start.x + ImGui::GetContentRegionAvail().x, line_start.y),
        ImGui::ColorConvertFloat4ToU32(hairline()), 1.0f);
    ImGui::Dummy(ImVec2(0.0f, 9.0f));

    ImGui::TextColored(aggregate_health_color(), "● %s", aggregate_health_text());
    if (!console_frame.rack_name.empty()) {
        if (semibold_font)
            ImGui::PushFont(semibold_font);
        ImGui::TextColored(text_primary(), "%s", console_frame.rack_name.c_str());
        if (semibold_font)
            ImGui::PopFont();
    }

    ImGui::Spacing();
    render_kpi_row();

    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    ImGui::TextDisabled("METERING");
    render_premium_meter_surface();

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    ImGui::TextDisabled("OUTPUT");
    if (semibold_font)
        ImGui::PushFont(semibold_font);
    ImGui::TextColored(accent(), "TO OBS");
    if (semibold_font)
        ImGui::PopFont();
}

} // namespace safevst3::rack::ui::p3

namespace ImGui {

inline void SafeVst3P3StyleColorsDark(ImGuiStyle* dst = nullptr)
{
    ImGui::SafeVst3P1StyleColorsDark(dst);
    ImGuiStyle& style = dst ? *dst : ImGui::GetStyle();

    style.WindowPadding = ImVec2(14.0f, 10.0f);
    style.FramePadding = ImVec2(9.0f, 5.0f);
    style.ItemSpacing = ImVec2(6.0f, 5.0f);
    style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
    style.ChildRounding = 7.0f;
    style.FrameRounding = 4.0f;
    style.ChildBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = safevst3::rack::ui::p3::surface_app();
    colors[ImGuiCol_ChildBg] = safevst3::rack::ui::p3::surface_lane();
    colors[ImGuiCol_FrameBg] = ImVec4(0.043f, 0.050f, 0.051f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.060f, 0.073f, 0.074f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.066f, 0.083f, 0.084f, 1.0f);
    colors[ImGuiCol_Separator] = safevst3::rack::ui::p3::hairline();
    colors[ImGuiCol_Button] = ImVec4(0.050f, 0.059f, 0.060f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.071f, 0.090f, 0.092f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.060f, 0.078f, 0.080f, 1.0f);
}

inline bool SafeVst3P3BeginChild(const char* str_id,
                                 const ImVec2& size_arg = ImVec2(0, 0),
                                 ImGuiChildFlags child_flags = 0,
                                 ImGuiWindowFlags window_flags = 0)
{
    using namespace safevst3::rack::ui::p1;
    using namespace safevst3::rack::ui::p3;

    const bool is_slot = str_id && std::strcmp(str_id, "rack-slot-card") == 0;
    if (!is_slot)
        return ImGui::BeginChild(str_id, size_arg, child_flags, window_flags);

    ImVec2 size = size_arg;
    size.y = kPremiumSlotHeight;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(11.0f, 7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, surface_slot());
    ImGui::PushStyleColor(ImGuiCol_Border, hairline());
    ++slot_child_style_depth;

    const bool visible = ImGui::BeginChild(
        str_id, size, child_flags & ~ImGuiChildFlags_Borders, window_flags);

    if (visible) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetWindowPos();
        const ImVec2 s = ImGui::GetWindowSize();
        draw->AddLine(
            ImVec2(p.x + 10.0f, p.y + s.y - 1.0f),
            ImVec2(p.x + s.x - 10.0f, p.y + s.y - 1.0f),
            ImGui::ColorConvertFloat4ToU32(hairline()), 1.0f);
    }
    return visible;
}

inline void SafeVst3P3EndChild()
{
    using namespace safevst3::rack::ui::p1;
    ImGui::EndChild();
    if (slot_child_style_depth > 0) {
        --slot_child_style_depth;
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }
}

inline bool SafeVst3P3Button(const char* label, const ImVec2& size_arg = ImVec2(0, 0))
{
    using namespace safevst3::rack::ui::p1;
    using namespace safevst3::rack::ui::p3;

    const bool in_slot = slot_child_style_depth > 0;
    if (!in_slot)
        return ImGui::SafeVst3P1Button(label, size_arg);

    const bool enabled = label_is(label, "Bypass");
    const bool bypassed = label_is(label, "Enable");
    const bool slot_ui = label_is(label, "Open UI");
    const bool slot_menu = label_is(label, "...");

    if (!enabled && !bypassed && !slot_ui && !slot_menu)
        return ImGui::SafeVst3P1Button(label, size_arg);

    const char* display = label;
    ImVec2 size = size_arg;
    size.y = 22.0f;

    if (enabled) {
        display = "ON##premium-slot-state";
        size.x = 42.0f;
    } else if (bypassed) {
        display = "OFF##premium-slot-state";
        size.x = 42.0f;
    } else if (slot_ui) {
        display = "OPEN##premium-slot-ui";
        size.x = 54.0f;
    } else {
        display = "...##premium-slot-actions";
        size.x = 29.0f;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

    if (enabled) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.032f, 0.078f, 0.080f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.044f, 0.135f, 0.140f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent_active());
        ImGui::PushStyleColor(ImGuiCol_Text, accent());
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(accent().x, accent().y, accent().z, 0.55f));
    } else if (bypassed) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.055f, 0.058f, 0.058f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.078f, 0.082f, 0.082f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.105f, 0.105f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, text_muted());
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.19f, 0.20f, 0.20f, 0.75f));
    } else if (slot_ui) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.045f, 0.053f, 0.054f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.060f, 0.095f, 0.097f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.071f, 0.112f, 0.115f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, text_primary());
        ImGui::PushStyleColor(ImGuiCol_Border, hairline());
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.038f, 0.045f, 0.046f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.062f, 0.072f, 0.073f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.079f, 0.091f, 0.092f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, text_muted());
        ImGui::PushStyleColor(ImGuiCol_Border, hairline());
    }

    const bool pressed = ImGui::Button(display, size);

    if (ImGui::IsItemHovered()) {
        if (enabled)
            ImGui::SetTooltip("Effect enabled — click to bypass");
        else if (bypassed)
            ImGui::SetTooltip("Effect bypassed — click to enable");
        else if (slot_ui)
            ImGui::SetTooltip("Open plug-in interface");
        else
            ImGui::SetTooltip("Slot actions");
    }

    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(2);
    return pressed;
}

inline void SafeVst3P3Text(const char* fmt, ...)
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
        ImGui::TextColored(text_primary(), "SAFE VST3");
        if (heading_font)
            ImGui::PopFont();
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextColored(text_muted(), "/");
        ImGui::SameLine(0.0f, 8.0f);
        if (semibold_font)
            ImGui::PushFont(semibold_font);
        ImGui::TextColored(text_primary(), "%s", rack ? rack : "Rack");
        if (semibold_font)
            ImGui::PopFont();
        va_end(args);
        return;
    }

    if (std::strcmp(fmt, "%u  %s") == 0) {
        const unsigned index = va_arg(args, unsigned);
        const char* name = va_arg(args, const char*);

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(pos.x + 4.0f, pos.y + 7.5f), 3.2f,
            ImGui::ColorConvertFloat4ToU32(accent()));
        ImGui::Dummy(ImVec2(9.0f, 15.0f));
        ImGui::SameLine(0.0f, 5.0f);
        ImGui::TextColored(text_muted(), "%02u", index);
        ImGui::SameLine(0.0f, 8.0f);
        if (semibold_font)
            ImGui::PushFont(semibold_font);
        ImGui::TextColored(text_primary(), "%s", name ? name : "VST3 Effect");
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
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextDisabled("· %u smp%s", latency, suffix ? suffix : "");
        va_end(args);
        return;
    }

    ImGui::TextV(fmt, args);
    va_end(args);
}

inline void SafeVst3P3TextDisabled(const char* fmt, ...)
{
    using namespace safevst3::rack::ui::p1;
    if (!fmt)
        return;

    va_list args;
    va_start(args, fmt);

    if (std::strcmp(fmt, "%u effects · %u samples") == 0) {
        console_frame.slot_count = va_arg(args, unsigned);
        console_frame.latency_samples = va_arg(args, unsigned);
        ImGui::TextDisabled(" ");
        va_end(args);
        return;
    }

    ImGui::TextDisabledV(fmt, args);
    va_end(args);
}

inline void SafeVst3P3TextUnformatted(const char* text, const char* text_end = nullptr)
{
    using namespace safevst3::rack::ui::p1;
    using namespace safevst3::rack::ui::p3;

    if (!text) {
        ImGui::TextUnformatted(text, text_end);
        return;
    }

    if (!text_end && std::strcmp(text, "OBS Safe VST3 Rack") == 0) {
        reset_console_frame("VST3 Rack");
        if (heading_font)
            ImGui::PushFont(heading_font);
        ImGui::TextColored(text_primary(), "SAFE VST3");
        if (heading_font)
            ImGui::PopFont();
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextColored(text_muted(), "/  RACK");
        return;
    }

    if (!text_end && std::strcmp(text, "Pending...") == 0) {
        console_frame.pending = true;
        ImGui::TextColored(warning(), "● PENDING");
        return;
    }

    if (!text_end && std::strcmp(text, "INPUT") == 0) {
        const ImVec2 available = ImGui::GetContentRegionAvail();

        if (available.x >= kPremiumSplitThreshold) {
            const float right_width =
                std::clamp(available.x * kPremiumConsoleShare, 285.0f, 360.0f);
            const float left_width =
                std::max(355.0f, available.x - right_width - kPremiumConsoleGap);
            console_frame.pane_height = std::max(260.0f, available.y);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 9.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, surface_lane());
            ImGui::BeginChild("rack-premium-lane",
                              ImVec2(left_width, console_frame.pane_height),
                              ImGuiChildFlags_None);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            console_frame.split_started = true;
        }

        draw_section_label("SIGNAL CHAIN", "INPUT");
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        return;
    }

    if (!text_end && std::strcmp(text, "OUTPUT TO OBS") == 0) {
        if (console_frame.split_started) {
            ImGui::EndChild();
            ImGui::SameLine(0.0f, kPremiumConsoleGap);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(17.0f, 15.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, surface_console());
            ImGui::BeginChild("rack-premium-console",
                              ImVec2(0.0f, console_frame.pane_height),
                              ImGuiChildFlags_None);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);

            render_premium_master_console();
            ImGui::EndChild();
            console_frame.split_started = false;
        } else {
            ImGui::Spacing();
            draw_section_label("OUTPUT", "TO OBS");
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

#define StyleColorsDark SafeVst3P3StyleColorsDark
#define BeginChild SafeVst3P3BeginChild
#define EndChild SafeVst3P3EndChild
#define Button SafeVst3P3Button
#define Text SafeVst3P3Text
#define TextDisabled SafeVst3P3TextDisabled
#define TextUnformatted SafeVst3P3TextUnformatted

#endif // _WIN32
