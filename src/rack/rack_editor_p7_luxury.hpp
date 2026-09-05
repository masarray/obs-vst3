#pragma once

#ifdef _WIN32

#include "rack/rack_editor_p6_hardware.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <string>

#undef StyleColorsDark
#undef BeginChild
#undef EndChild
#undef Button
#undef Text
#undef TextDisabled
#undef TextUnformatted
#undef BeginCombo
#undef InputTextWithHint
#ifdef BeginDisabled
#undef BeginDisabled
#endif

namespace safevst3::rack::ui::p7 {

using namespace safevst3::rack::ui;
using namespace safevst3::rack::ui::p1;
using namespace safevst3::rack::ui::p3;
using namespace safevst3::rack::ui::p4;
using namespace safevst3::rack::ui::p5;
using namespace safevst3::rack::ui::p6;

inline constexpr float kP7SlotHeight = 44.0f;
inline constexpr float kP7ConsoleShare = 0.405f;
inline constexpr float kP7PaneGap = 12.0f;
inline constexpr float kP7SlotControlReserve = 146.0f;
inline constexpr float kP7SlotRightMargin = 9.0f;

inline thread_local bool p7_rack_well_started = false;
inline thread_local bool p7_transition_pending = false;
inline thread_local float p7_slot_row_y = 0.0f;
inline thread_local ImVec2 p7_slot_led{};

inline void draw_outer_chassis()
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetWindowPos();
    const ImVec2 s = ImGui::GetWindowSize();
    const ImVec2 q(p.x + s.x, p.y + s.y);

    draw->AddRectFilledMultiColor(
        p, q,
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.055f, 0.064f, 0.065f, 1.0f)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.050f, 0.059f, 0.060f, 1.0f)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.012f, 0.017f, 0.018f, 1.0f)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.010f, 0.015f, 0.016f, 1.0f)));

    const ImU32 grain_hi = ImGui::ColorConvertFloat4ToU32(
        ImVec4(0.64f, 0.69f, 0.69f, 0.014f));
    const ImU32 grain_lo = ImGui::ColorConvertFloat4ToU32(
        ImVec4(0.0f, 0.0f, 0.0f, 0.030f));
    for (float y = p.y + 2.0f; y < q.y - 2.0f; y += 5.0f) {
        draw->AddLine(ImVec2(p.x + 1.0f, y), ImVec2(q.x - 1.0f, y),
                      (static_cast<int>(y) & 5) ? grain_hi : grain_lo, 1.0f);
    }

    draw->AddLine(ImVec2(p.x + 1.0f, p.y + 1.0f),
                  ImVec2(q.x - 1.0f, p.y + 1.0f),
                  ImGui::ColorConvertFloat4ToU32(
                      ImVec4(0.78f, 0.82f, 0.81f, 0.10f)), 1.0f);
}

inline void draw_slot_surface()
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetWindowPos();
    const ImVec2 s = ImGui::GetWindowSize();
    const ImVec2 a(p.x + 1.0f, p.y + 1.0f);
    const ImVec2 b(p.x + s.x - 1.0f, p.y + s.y - 1.0f);

    draw->AddRectFilledMultiColor(
        a, b,
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.043f, 0.051f, 0.052f, 0.98f)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.037f, 0.045f, 0.046f, 0.98f)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.019f, 0.025f, 0.026f, 0.99f)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.017f, 0.023f, 0.024f, 0.99f)));
    draw->AddRect(a, b,
                  ImGui::ColorConvertFloat4ToU32(
                      ImVec4(0.30f, 0.34f, 0.34f, 0.28f)),
                  6.0f, 0, 1.0f);
    draw->AddLine(ImVec2(a.x + 7.0f, a.y + 1.0f),
                  ImVec2(b.x - 7.0f, a.y + 1.0f),
                  ImGui::ColorConvertFloat4ToU32(
                      ImVec4(0.76f, 0.81f, 0.80f, 0.055f)), 1.0f);
}

inline void draw_precise_slot_identity(unsigned index, const char* name)
{
    const ImVec2 window = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const float row_center = window.y + size.y * 0.5f;
    const float text_h = ImGui::CalcTextSize("Ag").y;
    const float text_y = std::floor(row_center - text_h * 0.5f);
    const float left = window.x + 10.0f;
    const float control_screen_x =
        window.x + ImGui::GetWindowContentRegionMax().x - kP7SlotControlReserve;

    p7_slot_led = ImVec2(left + 2.0f, row_center);

    draw->AddCircle(p7_slot_led, 5.2f,
                    ImGui::ColorConvertFloat4ToU32(
                        ImVec4(cyan_led().x, cyan_led().y, cyan_led().z, 0.09f)),
                    0, 2.0f);

    char number[8]{};
    _snprintf_s(number, sizeof(number), _TRUNCATE, "%02u", index);
    draw->AddText(ImVec2(left + 13.0f, text_y),
                  ImGui::ColorConvertFloat4ToU32(text_muted()), number);

    const float name_x = left + 38.0f;
    const ImVec2 clip_min(name_x, window.y + 2.0f);
    const ImVec2 clip_max(control_screen_x - 10.0f, window.y + size.y - 2.0f);
    draw->PushClipRect(clip_min, clip_max, true);
    if (semibold_font)
        ImGui::PushFont(semibold_font);
    const float name_h = ImGui::CalcTextSize(name ? name : "VST3 Effect").y;
    draw->AddText(ImVec2(name_x, std::floor(row_center - name_h * 0.5f)),
                  ImGui::ColorConvertFloat4ToU32(text_primary()),
                  name ? name : "VST3 Effect");
    if (semibold_font)
        ImGui::PopFont();
    draw->PopClipRect();

    p7_slot_row_y = std::max(0.0f,
        (ImGui::GetWindowHeight() - 26.0f) * 0.5f);

    const float local_control_x =
        ImGui::GetWindowContentRegionMax().x - kP7SlotControlReserve;
    ImGui::SetCursorPosY(p7_slot_row_y);
    const float current_x = ImGui::GetCursorPosX();
    ImGui::Dummy(ImVec2(std::max(1.0f, local_control_x - current_x - 5.0f), 26.0f));
}

inline void draw_slot_health_led(const char* health)
{
    observe_health(health);
    const ImVec4 color = health_color(health);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddCircleFilled(p7_slot_led, 7.0f,
                          ImGui::ColorConvertFloat4ToU32(
                              ImVec4(color.x, color.y, color.z, 0.050f)));
    draw->AddCircleFilled(p7_slot_led, 3.0f,
                          ImGui::ColorConvertFloat4ToU32(color));
    draw->AddCircleFilled(ImVec2(p7_slot_led.x - 0.7f, p7_slot_led.y - 0.8f),
                          0.8f,
                          ImGui::ColorConvertFloat4ToU32(
                              ImVec4(0.96f, 1.0f, 1.0f, 0.72f)));

    ImGui::SetCursorPosY(p7_slot_row_y);
    ImGui::SetCursorPosX(
        ImGui::GetWindowContentRegionMax().x - kP7SlotControlReserve);
}

inline void render_p7_master_surface()
{
    update_hardware_meter();
    RackMasterControlSnapshot controls = g_rack_master_controls.snapshot();
    float input_db = controls.input_db;
    float output_db = controls.output_db;

    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const float group_width = width * 0.5f;
    const float input_center = origin.x + group_width * 0.50f;
    const float output_center = origin.x + group_width * 1.50f;
    const float track_height = std::clamp(ImGui::GetWindowHeight() * 0.29f, 142.0f, 164.0f);
    const float control_top = origin.y + 42.0f;
    const float control_bottom = control_top + track_height;
    const float panel_bottom = control_bottom + 52.0f;
    const float fader_offset = 43.0f;
    const float meter_offset = 20.0f;
    const float meter_gap = 15.0f;
    const float meter_width = 8.0f;

    draw->AddRectFilled(
        origin, ImVec2(origin.x + width, panel_bottom),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.008f, 0.012f, 0.013f, 0.84f)),
        9.0f);
    draw->AddRect(
        origin, ImVec2(origin.x + width, panel_bottom),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.31f, 0.35f, 0.35f, 0.30f)),
        9.0f, 0, 1.0f);
    draw->AddLine(ImVec2(origin.x + 10.0f, origin.y + 1.0f),
                  ImVec2(origin.x + width - 10.0f, origin.y + 1.0f),
                  ImGui::ColorConvertFloat4ToU32(
                      ImVec4(0.74f, 0.80f, 0.79f, 0.055f)), 1.0f);
    draw->AddLine(ImVec2(origin.x + group_width, origin.y + 12.0f),
                  ImVec2(origin.x + group_width, panel_bottom - 12.0f),
                  ImGui::ColorConvertFloat4ToU32(
                      ImVec4(0.20f, 0.24f, 0.24f, 0.46f)), 1.0f);

    const ImU32 label = ImGui::ColorConvertFloat4ToU32(text_muted());
    draw_centered_text(draw, "INPUT", input_center, origin.y + 11.0f, label);
    draw_centered_text(draw, "OUTPUT", output_center, origin.y + 11.0f, label);

    const float in_fader_x = input_center - fader_offset;
    const float out_fader_x = output_center - fader_offset;
    const float in_l_x = input_center + meter_offset;
    const float in_r_x = in_l_x + meter_gap;
    const float out_l_x = output_center + meter_offset;
    const float out_r_x = out_l_x + meter_gap;

    if (draw_metal_fader("##p6-input-fader", "Input Trim", input_db,
                         in_fader_x, control_top, control_bottom, 42.0f))
        g_rack_master_controls.set_input_db(input_db);
    if (draw_metal_fader("##p6-output-fader", "Output Fader", output_db,
                         out_fader_x, control_top, control_bottom, 42.0f))
        g_rack_master_controls.set_output_db(output_db);

    draw_segment_meter(draw, in_l_x, control_top, control_bottom, meter_width,
                       hardware_meter.in_l, hardware_meter.in_l_hold);
    draw_segment_meter(draw, in_r_x, control_top, control_bottom, meter_width,
                       hardware_meter.in_r, hardware_meter.in_r_hold);
    draw_segment_meter(draw, out_l_x, control_top, control_bottom, meter_width,
                       hardware_meter.out_l, hardware_meter.out_l_hold);
    draw_segment_meter(draw, out_r_x, control_top, control_bottom, meter_width,
                       hardware_meter.out_r, hardware_meter.out_r_hold);

    const float label_y = control_bottom + 8.0f;
    const float value_y = control_bottom + 29.0f;
    draw_centered_text(draw, "TRIM", in_fader_x, label_y, label);
    draw_centered_text(draw, "FADER", out_fader_x, label_y, label);
    draw_centered_text(draw, "L", in_l_x, label_y, label);
    draw_centered_text(draw, "R", in_r_x, label_y, label);
    draw_centered_text(draw, "L", out_l_x, label_y, label);
    draw_centered_text(draw, "R", out_r_x, label_y, label);

    draw_centered_value(draw, in_fader_x, value_y, input_db, true, " dB");
    draw_centered_value(draw, out_fader_x, value_y, output_db, true, " dB");
    draw_centered_value(draw, (in_l_x + in_r_x) * 0.5f, value_y,
                        std::max(hardware_meter.in_l, hardware_meter.in_r),
                        hardware_meter.valid);
    draw_centered_value(draw, (out_l_x + out_r_x) * 0.5f, value_y,
                        std::max(hardware_meter.out_l, hardware_meter.out_r),
                        hardware_meter.valid);

    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, panel_bottom - origin.y));
}

inline void render_p7_loudness()
{
    const RackBroadcastLoudnessSnapshot loudness = g_rack_broadcast_loudness.snapshot();
    char integrated[24] = "--.-";
    char true_peak[24] = "--.-";
    if (loudness.integrated_valid)
        _snprintf_s(integrated, sizeof(integrated), _TRUNCATE, "%.1f", loudness.integrated_lufs);
    if (loudness.true_peak_valid)
        _snprintf_s(true_peak, sizeof(true_peak), _TRUNCATE, "%.1f", loudness.true_peak_dbtp);

    const float width = ImGui::GetContentRegionAvail().x;
    const float half = width * 0.5f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float height = 70.0f;
    const float left_center = p.x + half * 0.5f;
    const float right_center = p.x + half * 1.5f;
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(p, ImVec2(p.x + width, p.y + height),
                        ImGui::ColorConvertFloat4ToU32(
                            ImVec4(0.008f, 0.012f, 0.013f, 0.78f)), 8.0f);
    draw->AddRect(p, ImVec2(p.x + width, p.y + height),
                  ImGui::ColorConvertFloat4ToU32(
                      ImVec4(0.30f, 0.34f, 0.34f, 0.28f)), 8.0f, 0, 1.0f);
    draw->AddLine(ImVec2(p.x + half, p.y + 10.0f),
                  ImVec2(p.x + half, p.y + height - 10.0f),
                  ImGui::ColorConvertFloat4ToU32(
                      ImVec4(0.20f, 0.24f, 0.24f, 0.45f)), 1.0f);

    const ImU32 primary = ImGui::ColorConvertFloat4ToU32(text_primary());
    const ImU32 tp = ImGui::ColorConvertFloat4ToU32(
        loudness.true_peak_valid ? true_peak_color(loudness.true_peak_dbtp) : text_muted());
    const ImU32 muted = ImGui::ColorConvertFloat4ToU32(text_muted());

    draw_centered_text(draw, integrated, left_center, p.y + 9.0f, primary, loudness_font);
    draw_centered_text(draw, true_peak, right_center, p.y + 9.0f, tp, loudness_font);
    draw_centered_text(draw, "LUFS-I", left_center, p.y + 44.0f, muted);
    draw_centered_text(draw, "dBTP", right_center, p.y + 44.0f, muted);

    ImGui::Dummy(ImVec2(width, height));
}

inline void render_p7_master_console()
{
    draw_brushed_metal_backplate();

    if (semibold_font)
        ImGui::PushFont(semibold_font);
    ImGui::TextColored(text_primary(), "SAFE VST3");
    if (semibold_font)
        ImGui::PopFont();

    const std::string status = std::string("● ") + aggregate_health_text();
    const float status_width = ImGui::CalcTextSize(status.c_str()).x;
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
        ImGui::GetWindowContentRegionMax().x - status_width));
    ImGui::TextColored(aggregate_health_color(), "%s", status.c_str());

    ImGui::TextDisabled("%u FX  ·  %u smp",
                        console_frame.slot_count, console_frame.latency_samples);
    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    render_p7_master_surface();

    ImGui::Dummy(ImVec2(0.0f, 7.0f));
    ImGui::TextDisabled("LOUDNESS");
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    render_p7_loudness();

    ImGui::Dummy(ImVec2(0.0f, 7.0f));
    const char* footer = "TO OBS";
    const float footer_w = ImGui::CalcTextSize(footer).x;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
        ImGui::GetWindowContentRegionMax().x - footer_w));
    if (semibold_font)
        ImGui::PushFont(semibold_font);
    ImGui::TextColored(accent(), "%s", footer);
    if (semibold_font)
        ImGui::PopFont();
}

inline bool p7_toolbar_button(const char* label, const ImVec2& size_arg)
{
    const bool save = std::strcmp(label, "Save As") == 0;
    const bool update = std::strcmp(label, "Update") == 0;
    const bool preset = std::strcmp(label, "Preset ...") == 0;
    const bool add = std::strcmp(label, "Add Effect") == 0;
    const bool refresh = std::strcmp(label, "Refresh") == 0;
    if (!(save || update || preset || add || refresh))
        return false;

    const char* display = save ? "SAVE AS" : update ? "UPDATE" : preset ? "..." :
                          add ? "+ ADD" : "RESCAN";
    const float width = save ? 66.0f : update ? 64.0f : preset ? 30.0f :
                        add ? 92.0f : 62.0f;
    return draw_luxury_button(label, display,
                              ImVec2(size_arg.x > 0.0f ? size_arg.x : width,
                                     size_arg.y > 0.0f ? size_arg.y : 30.0f),
                              save || add);
}

} // namespace safevst3::rack::ui::p7

namespace ImGui {

inline void SafeVst3P7StyleColorsDark(ImGuiStyle* dst = nullptr)
{
    ImGui::SafeVst3P6StyleColorsDark(dst);
    ImGuiStyle& style = dst ? *dst : ImGui::GetStyle();
    style.WindowPadding = ImVec2(14.0f, 10.0f);
    style.FramePadding = ImVec2(10.0f, 7.0f);
    style.ItemSpacing = ImVec2(7.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.ScrollbarSize = 9.0f;
    style.GrabMinSize = 10.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.012f, 0.017f, 0.018f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.025f, 0.032f, 0.033f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.042f, 0.054f, 0.055f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.050f, 0.066f, 0.067f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(0.15f, 0.18f, 0.18f, 0.56f);
}

inline bool SafeVst3P7BeginChild(const char* str_id,
                                 const ImVec2& size_arg = ImVec2(0, 0),
                                 ImGuiChildFlags child_flags = 0,
                                 ImGuiWindowFlags window_flags = 0)
{
    using namespace safevst3::rack::ui::p6;
    using namespace safevst3::rack::ui::p7;

    const bool slot = str_id && std::strcmp(str_id, "rack-slot-card") == 0;
    if (!slot)
        return ImGui::SafeVst3P6BeginChild(str_id, size_arg, child_flags, window_flags);

    ImGui::Indent(6.0f);
    ImVec2 size = size_arg;
    size.x = std::max(1.0f, ImGui::GetContentRegionAvail().x - 6.0f);
    size.y = kP7SlotHeight;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(9.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, metal_strip());
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImVec4(0.28f, 0.32f, 0.32f, 0.30f));

    ++luxury_slot_depth;
    const bool visible = ImGui::BeginChild(
        str_id, size, child_flags | ImGuiChildFlags_Borders,
        window_flags | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    if (visible)
        draw_slot_surface();
    return visible;
}

inline void SafeVst3P7EndChild()
{
    using namespace safevst3::rack::ui::p6;
    if (luxury_slot_depth > 0) {
        ImGui::EndChild();
        --luxury_slot_depth;
        ImGui::Unindent(6.0f);
        return;
    }
    ImGui::SafeVst3P6EndChild();
}

inline bool SafeVst3P7Button(const char* label,
                             const ImVec2& size_arg = ImVec2(0, 0))
{
    using namespace safevst3::rack::ui::p6;
    using namespace safevst3::rack::ui::p7;

    if (luxury_slot_depth > 0 && label) {
        const bool enabled = std::strcmp(label, "Bypass") == 0;
        const bool bypassed = std::strcmp(label, "Enable") == 0;
        const bool open_ui = std::strcmp(label, "Open UI") == 0;
        const bool actions = std::strcmp(label, "...") == 0;
        if (enabled || bypassed || open_ui || actions) {
            const char* display = enabled ? "ON" : bypassed ? "OFF" : open_ui ? "OPEN" : "...";
            const float width = (enabled || bypassed) ? 38.0f : open_ui ? 48.0f : 28.0f;
            return draw_luxury_button(label, display, ImVec2(width, 26.0f), enabled);
        }
    }

    if (label && (std::strcmp(label, "Save As") == 0 ||
                  std::strcmp(label, "Update") == 0 ||
                  std::strcmp(label, "Preset ...") == 0 ||
                  std::strcmp(label, "Add Effect") == 0 ||
                  std::strcmp(label, "Refresh") == 0))
        return p7_toolbar_button(label, size_arg);

    return ImGui::SafeVst3P3Button(label, size_arg);
}

inline void SafeVst3P7Text(const char* fmt, ...)
{
    using namespace safevst3::rack::ui::p1;
    using namespace safevst3::rack::ui::p3;
    using namespace safevst3::rack::ui::p6;
    using namespace safevst3::rack::ui::p7;

    if (!fmt)
        return;

    va_list args;
    va_start(args, fmt);

    if (luxury_slot_depth > 0 && std::strcmp(fmt, "%u  %s") == 0) {
        const unsigned index = va_arg(args, unsigned);
        const char* name = va_arg(args, const char*);
        draw_precise_slot_identity(index, name);
        va_end(args);
        return;
    }

    if (luxury_slot_depth > 0 && std::strcmp(fmt, "%s · %u samples%s") == 0) {
        const char* health = va_arg(args, const char*);
        (void)va_arg(args, unsigned);
        (void)va_arg(args, const char*);
        draw_slot_health_led(health);
        va_end(args);
        return;
    }

    if (std::strcmp(fmt, "OBS Safe VST3 Rack — %s") == 0) {
        const char* rack = va_arg(args, const char*);
        p7_transition_pending = false;
        p7_rack_well_started = false;
        reset_console_frame(rack ? rack : "VST3 Rack");
        draw_outer_chassis();
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

    va_end(args);
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
}

inline void SafeVst3P7TextDisabled(const char* fmt, ...)
{
    using namespace safevst3::rack::ui::p3;
    using namespace safevst3::rack::ui::p6;

    if (!fmt)
        return;
    if (luxury_slot_depth > 0 && std::strcmp(fmt, "%s") == 0)
        return;

    va_list args;
    va_start(args, fmt);
    if (std::strcmp(fmt, "%u effects · %u samples") == 0) {
        console_frame.slot_count = va_arg(args, unsigned);
        console_frame.latency_samples = va_arg(args, unsigned);
        va_end(args);
        return;
    }
    ImGui::TextDisabledV(fmt, args);
    va_end(args);
}

inline bool SafeVst3P7BeginCombo(const char* label,
                                 const char* preview_value,
                                 ImGuiComboFlags flags = 0)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    const bool open = ImGui::BeginCombo(label, preview_value, flags);
    ImGui::PopStyleVar(2);
    return open;
}

inline bool SafeVst3P7InputTextWithHint(const char* label,
                                        const char* hint,
                                        char* buf,
                                        size_t buf_size,
                                        ImGuiInputTextFlags flags = 0,
                                        ImGuiInputTextCallback callback = nullptr,
                                        void* user_data = nullptr)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    const bool result = ImGui::InputTextWithHint(
        label, hint, buf, buf_size, flags, callback, user_data);
    ImGui::PopStyleVar(2);
    return result;
}

inline void SafeVst3P7BeginDisabled(bool disabled = true)
{
    using namespace safevst3::rack::ui::p7;
    if (p7_transition_pending && disabled) {
        ImGuiStyle& style = ImGui::GetStyle();
        const float saved = style.DisabledAlpha;
        style.DisabledAlpha = 1.0f;
        ImGui::BeginDisabled(true);
        style.DisabledAlpha = saved;
        return;
    }
    ImGui::BeginDisabled(disabled);
}

inline void SafeVst3P7TextUnformatted(const char* text,
                                      const char* text_end = nullptr)
{
    using namespace safevst3::rack::ui::p1;
    using namespace safevst3::rack::ui::p3;
    using namespace safevst3::rack::ui::p6;
    using namespace safevst3::rack::ui::p7;

    if (!text) {
        ImGui::TextUnformatted(text, text_end);
        return;
    }

    if (!text_end && std::strcmp(text, "OBS Safe VST3 Rack") == 0) {
        p7_transition_pending = false;
        p7_rack_well_started = false;
        reset_console_frame("VST3 Rack");
        draw_outer_chassis();
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
        p7_transition_pending = true;
        return;
    }

    if (!text_end && std::strcmp(text, "Preset") == 0) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(text_muted(), "PRESET");
        return;
    }

    if (!text_end && std::strcmp(text, "INPUT") == 0) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (available.x >= kPremiumSplitThreshold) {
            const float right_width = std::clamp(
                available.x * kP7ConsoleShare, 326.0f, 372.0f);
            const float left_width = std::max(
                390.0f, available.x - right_width - kP7PaneGap);
            console_frame.pane_height = std::max(1.0f, available.y);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 9.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, metal_bottom());
            ImGui::PushStyleColor(ImGuiCol_Border,
                                  ImVec4(0.28f, 0.32f, 0.32f, 0.30f));
            ImGui::BeginChild("rack-p7-luxury-lane",
                              ImVec2(left_width, console_frame.pane_height),
                              ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            console_frame.split_started = true;

            draw_brushed_metal_backplate();
            if (semibold_font)
                ImGui::PushFont(semibold_font);
            ImGui::TextColored(text_muted(), "SIGNAL CHAIN");
            if (semibold_font)
                ImGui::PopFont();
            ImGui::SameLine(0.0f, 7.0f);
            ImGui::TextColored(accent(), "INPUT");
            ImGui::Dummy(ImVec2(0.0f, 4.0f));

            const float well_height = std::max(
                110.0f, ImGui::GetContentRegionAvail().y - 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f, 6.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, metal_recess());
            ImGui::PushStyleColor(ImGuiCol_Border,
                                  ImVec4(0.27f, 0.31f, 0.31f, 0.28f));
            ImGui::BeginChild("rack-p7-slot-well",
                              ImVec2(0.0f, well_height),
                              ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_AlwaysVerticalScrollbar);
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            draw_recessed_rack_bay();
            p7_rack_well_started = true;
        } else {
            draw_section_label("SIGNAL CHAIN", "INPUT");
            ImGui::Dummy(ImVec2(0.0f, 3.0f));
        }
        return;
    }

    if (!text_end && std::strcmp(text, "OUTPUT TO OBS") == 0) {
        if (p7_rack_well_started) {
            ImGui::EndChild();
            p7_rack_well_started = false;
        }
        if (console_frame.split_started) {
            ImGui::EndChild();
            ImGui::SameLine(0.0f, kP7PaneGap);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15.0f, 12.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, metal_bottom());
            ImGui::PushStyleColor(ImGuiCol_Border,
                                  ImVec4(0.28f, 0.32f, 0.32f, 0.30f));
            ImGui::BeginChild("rack-p7-master-console",
                              ImVec2(0.0f, console_frame.pane_height),
                              ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);

            render_p7_master_console();
            ImGui::EndChild();
            console_frame.split_started = false;
        } else {
            ImGui::Spacing();
            draw_section_label("OUTPUT", "TO OBS");
        }
        return;
    }

    ImGui::SafeVst3P5TextUnformatted(text, text_end);
}

} // namespace ImGui

#define StyleColorsDark SafeVst3P7StyleColorsDark
#define BeginChild SafeVst3P7BeginChild
#define EndChild SafeVst3P7EndChild
#define Button SafeVst3P7Button
#define Text SafeVst3P7Text
#define TextDisabled SafeVst3P7TextDisabled
#define TextUnformatted SafeVst3P7TextUnformatted
#define BeginCombo SafeVst3P7BeginCombo
#define InputTextWithHint SafeVst3P7InputTextWithHint
#define BeginDisabled SafeVst3P7BeginDisabled

#endif // _WIN32
