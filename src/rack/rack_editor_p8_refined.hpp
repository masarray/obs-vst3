#pragma once

#ifdef _WIN32

#include "rack/rack_editor_p7_luxury.hpp"

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
#undef TextUnformatted

namespace safevst3::rack::ui::p8 {

using namespace safevst3::rack::ui;
using namespace safevst3::rack::ui::p1;
using namespace safevst3::rack::ui::p3;
using namespace safevst3::rack::ui::p4;
using namespace safevst3::rack::ui::p5;
using namespace safevst3::rack::ui::p6;
using namespace safevst3::rack::ui::p7;

inline constexpr float kP8SlotActionReserve = 42.0f;
inline constexpr float kP8SlotHitHeight = 28.0f;

inline thread_local ImVec2 p8_slot_led{};
inline thread_local ImVec2 p8_slot_name_min{};
inline thread_local ImVec2 p8_slot_name_max{};
inline thread_local float p8_slot_row_y = 0.0f;
inline thread_local bool p8_scroll_style_pushed = false;

inline void draw_p8_slot_identity(unsigned index, const char* name)
{
    const ImVec2 window = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const float row_center = window.y + size.y * 0.5f;
    const float text_h = ImGui::CalcTextSize("Ag").y;
    const float text_y = std::floor(row_center - text_h * 0.5f);
    const float left = window.x + 11.0f;
    const float content_right = window.x + ImGui::GetWindowContentRegionMax().x;
    const float action_left = content_right - kP8SlotActionReserve;

    p8_slot_led = ImVec2(left + 2.0f, row_center);

    char number[8]{};
    _snprintf_s(number, sizeof(number), _TRUNCATE, "%02u", index);
    draw->AddText(ImVec2(left + 14.0f, text_y),
                  ImGui::ColorConvertFloat4ToU32(text_muted()), number);

    const float name_x = left + 42.0f;
    p8_slot_name_min = ImVec2(name_x, window.y + 5.0f);
    p8_slot_name_max = ImVec2(std::max(name_x + 1.0f, action_left - 9.0f),
                              window.y + size.y - 5.0f);

    draw->PushClipRect(p8_slot_name_min, p8_slot_name_max, true);
    if (semibold_font)
        ImGui::PushFont(semibold_font);
    const char* display = name ? name : "VST3 Effect";
    const float name_h = ImGui::CalcTextSize(display).y;
    draw->AddText(ImVec2(name_x, std::floor(row_center - name_h * 0.5f)),
                  ImGui::ColorConvertFloat4ToU32(text_primary()), display);
    if (semibold_font)
        ImGui::PopFont();
    draw->PopClipRect();

    p8_slot_row_y = std::max(0.0f, (ImGui::GetWindowHeight() - 26.0f) * 0.5f);
    const float local_action_x =
        ImGui::GetWindowContentRegionMax().x - kP8SlotActionReserve;
    ImGui::SetCursorPosY(p8_slot_row_y);
    const float current_x = ImGui::GetCursorPosX();
    ImGui::Dummy(ImVec2(std::max(1.0f, local_action_x - current_x - 5.0f), 26.0f));
}

inline void draw_p8_slot_health(const char* health)
{
    observe_health(health);
    const ImVec4 color = health_color(health);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddCircleFilled(p8_slot_led, 7.2f,
                          ImGui::ColorConvertFloat4ToU32(
                              ImVec4(color.x, color.y, color.z, 0.048f)));
    draw->AddCircleFilled(p8_slot_led, 3.1f,
                          ImGui::ColorConvertFloat4ToU32(color));
    draw->AddCircleFilled(ImVec2(p8_slot_led.x - 0.7f, p8_slot_led.y - 0.9f),
                          0.85f,
                          ImGui::ColorConvertFloat4ToU32(
                              ImVec4(0.97f, 1.0f, 1.0f, 0.75f)));

    ImGui::SetCursorPosY(p8_slot_row_y);
    ImGui::SetCursorPosX(
        ImGui::GetWindowContentRegionMax().x - kP8SlotActionReserve);
}

inline bool p8_invisible_hit(const char* id,
                             const ImVec2& min,
                             const ImVec2& max,
                             const char* tooltip,
                             bool led = false)
{
    if (max.x <= min.x || max.y <= min.y)
        return false;

    const ImVec2 saved = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(min);
    const bool pressed = ImGui::InvisibleButton(
        id, ImVec2(max.x - min.x, max.y - min.y));
    const bool hovered = ImGui::IsItemHovered();

    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (tooltip)
            ImGui::SetTooltip("%s", tooltip);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (led) {
            draw->AddCircle(p8_slot_led, 6.0f,
                            ImGui::ColorConvertFloat4ToU32(
                                ImVec4(0.16f, 0.92f, 0.96f, 0.30f)),
                            0, 1.2f);
        } else {
            draw->AddLine(ImVec2(min.x, max.y - 2.0f),
                          ImVec2(std::min(max.x, min.x + 78.0f), max.y - 2.0f),
                          ImGui::ColorConvertFloat4ToU32(
                              ImVec4(0.10f, 0.82f, 0.88f, 0.30f)), 1.0f);
        }
    }

    ImGui::SetCursorScreenPos(saved);
    return pressed;
}

inline bool draw_p8_slot_action_button()
{
    const ImVec2 window = ImGui::GetWindowPos();
    const float x = window.x + ImGui::GetWindowContentRegionMax().x - 30.0f;
    const float y = window.y + (ImGui::GetWindowHeight() - 26.0f) * 0.5f;
    const ImVec2 saved = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(x, y));

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.026f, 0.034f, 0.035f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.045f, 0.12f, 0.13f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.055f, 0.18f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.20f, 0.30f, 0.30f, 0.42f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.76f, 0.92f, 0.93f, 1.0f));

    const bool pressed = ImGui::Button("+##p8-slot-actions", ImVec2(28.0f, 26.0f));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Slot actions");

    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(2);
    ImGui::SetCursorScreenPos(saved);
    return pressed;
}

inline bool draw_p8_fader(const char* id,
                          const char* tooltip_label,
                          float& db,
                          float x,
                          float top,
                          float bottom,
                          float width)
{
    const ImVec2 saved = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(x - width * 0.5f, top));
    ImGui::InvisibleButton(id, ImVec2(width, bottom - top));
    const bool hovered = ImGui::IsItemHovered();
    bool changed = false;

    const int fader_id =
        (id && std::strcmp(id, "##p6-input-fader") == 0) ? 1 : 2;

    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        db = 0.0f;
        fader_reset_lock = fader_id;
        changed = true;
    } else if (fader_reset_lock != fader_id &&
               ImGui::IsItemActive() &&
               ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
        const float ratio = std::clamp(
            (ImGui::GetIO().MousePos.y - top) / std::max(1.0f, bottom - top),
            0.0f, 1.0f);
        db = kRackMasterMaxDb -
             ratio * (kRackMasterMaxDb - kRackMasterMinDb);
        db = std::round(db * 10.0f) * 0.1f;
        changed = true;
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        fader_reset_lock == fader_id)
        fader_reset_lock = 0;

    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        db = std::clamp(db + ImGui::GetIO().MouseWheel * 0.2f,
                        kRackMasterMinDb, kRackMasterMaxDb);
        changed = true;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float track_top = top + 5.0f;
    const float track_bottom = bottom - 5.0f;

    draw->AddRectFilled(ImVec2(x - 5.0f, track_top),
                        ImVec2(x + 5.0f, track_bottom),
                        ImGui::ColorConvertFloat4ToU32(
                            ImVec4(0.004f, 0.007f, 0.008f, 1.0f)), 4.0f);
    draw->AddRect(ImVec2(x - 5.0f, track_top),
                  ImVec2(x + 5.0f, track_bottom),
                  ImGui::ColorConvertFloat4ToU32(
                      ImVec4(0.31f, 0.35f, 0.35f, 0.22f)), 4.0f, 0, 1.0f);
    draw->AddLine(ImVec2(x - 3.0f, track_top + 2.0f),
                  ImVec2(x - 3.0f, track_bottom - 2.0f),
                  ImGui::ColorConvertFloat4ToU32(
                      ImVec4(0.64f, 0.68f, 0.67f, 0.10f)), 1.0f);

    const float zero_y = track_top + (track_bottom - track_top) * fader_value_ratio(0.0f);
    draw->AddLine(ImVec2(x - 13.0f, zero_y), ImVec2(x + 13.0f, zero_y),
                  ImGui::ColorConvertFloat4ToU32(
                      ImVec4(0.82f, 0.88f, 0.87f, 0.20f)), 1.0f);

    const float cap_y = track_top + (track_bottom - track_top) * fader_value_ratio(db);
    const ImVec2 shadow_min(x - 20.0f, cap_y - 9.0f);
    const ImVec2 shadow_max(x + 20.0f, cap_y + 10.0f);
    draw->AddRectFilled(ImVec2(shadow_min.x + 1.0f, shadow_min.y + 2.0f),
                        ImVec2(shadow_max.x + 1.0f, shadow_max.y + 2.0f),
                        ImGui::ColorConvertFloat4ToU32(
                            ImVec4(0.0f, 0.0f, 0.0f, 0.42f)), 4.0f);
    draw->AddRectFilled(shadow_min, shadow_max,
                        ImGui::ColorConvertFloat4ToU32(
                            ImVec4(0.36f, 0.39f, 0.39f, 1.0f)), 4.0f);
    draw->AddRectFilledMultiColor(
        ImVec2(shadow_min.x + 1.0f, shadow_min.y + 1.0f),
        ImVec2(shadow_max.x - 1.0f, shadow_max.y - 1.0f),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.78f, 0.81f, 0.80f, 1.0f)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.56f, 0.59f, 0.58f, 1.0f)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.23f, 0.25f, 0.25f, 1.0f)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.31f, 0.33f, 0.33f, 1.0f)));
    draw->AddRect(shadow_min, shadow_max,
                  ImGui::ColorConvertFloat4ToU32(
                      hovered ? ImVec4(0.25f, 0.76f, 0.79f, 0.42f)
                              : ImVec4(0.76f, 0.80f, 0.79f, 0.28f)),
                  4.0f, 0, 1.0f);
    draw->AddLine(ImVec2(x - 14.0f, cap_y), ImVec2(x + 14.0f, cap_y),
                  ImGui::ColorConvertFloat4ToU32(
                      ImVec4(0.94f, 0.96f, 0.94f, 0.70f)), 1.0f);
    draw->AddLine(ImVec2(x - 12.0f, cap_y - 3.0f), ImVec2(x + 12.0f, cap_y - 3.0f),
                  ImGui::ColorConvertFloat4ToU32(
                      ImVec4(0.92f, 0.94f, 0.93f, 0.12f)), 1.0f);

    if (hovered)
        ImGui::SetTooltip("%s  %.1f dB\nDrag · wheel fine adjust · double-click = 0.0 dB",
                          tooltip_label, db);

    ImGui::SetCursorScreenPos(saved);
    return changed;
}

inline void render_p8_master_surface()
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
    const float track_height = std::clamp(ImGui::GetWindowHeight() * 0.285f, 142.0f, 160.0f);
    const float control_top = origin.y + 39.0f;
    const float control_bottom = control_top + track_height;
    const float panel_bottom = control_bottom + 50.0f;
    const float fader_offset = 39.0f;
    const float meter_offset = 24.0f;
    const float meter_gap = 16.0f;
    const float meter_width = 7.5f;

    draw->AddRectFilled(origin, ImVec2(origin.x + width, panel_bottom),
                        ImGui::ColorConvertFloat4ToU32(
                            ImVec4(0.006f, 0.010f, 0.011f, 0.78f)), 9.0f);
    draw->AddRect(origin, ImVec2(origin.x + width, panel_bottom),
                  ImGui::ColorConvertFloat4ToU32(
                      ImVec4(0.28f, 0.32f, 0.32f, 0.24f)), 9.0f, 0, 1.0f);
    draw->AddLine(ImVec2(origin.x + group_width, origin.y + 12.0f),
                  ImVec2(origin.x + group_width, panel_bottom - 12.0f),
                  ImGui::ColorConvertFloat4ToU32(
                      ImVec4(0.19f, 0.23f, 0.23f, 0.42f)), 1.0f);

    const ImU32 label = ImGui::ColorConvertFloat4ToU32(text_muted());
    draw_centered_text(draw, "INPUT", input_center, origin.y + 10.0f, label);
    draw_centered_text(draw, "OUTPUT", output_center, origin.y + 10.0f, label);

    const float in_fader_x = input_center - fader_offset;
    const float out_fader_x = output_center - fader_offset;
    const float in_l_x = input_center + meter_offset;
    const float in_r_x = in_l_x + meter_gap;
    const float out_l_x = output_center + meter_offset;
    const float out_r_x = out_l_x + meter_gap;

    if (draw_p8_fader("##p6-input-fader", "Input Trim", input_db,
                      in_fader_x, control_top, control_bottom, 46.0f))
        g_rack_master_controls.set_input_db(input_db);
    if (draw_p8_fader("##p6-output-fader", "Output Fader", output_db,
                      out_fader_x, control_top, control_bottom, 46.0f))
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
    const float value_y = control_bottom + 28.0f;
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

inline void render_p8_loudness()
{
    const RackBroadcastLoudnessSnapshot loudness = g_rack_broadcast_loudness.snapshot();
    char integrated[24] = "--.-";
    char true_peak[24] = "--.-";
    if (loudness.integrated_valid)
        _snprintf_s(integrated, sizeof(integrated), _TRUNCATE,
                    "%.1f", loudness.integrated_lufs);
    if (loudness.true_peak_valid)
        _snprintf_s(true_peak, sizeof(true_peak), _TRUNCATE,
                    "%.1f", loudness.true_peak_dbtp);

    const float width = ImGui::GetContentRegionAvail().x;
    const float half = width * 0.5f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float height = 64.0f;
    const float left_center = p.x + half * 0.5f;
    const float right_center = p.x + half * 1.5f;
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(p, ImVec2(p.x + width, p.y + height),
                        ImGui::ColorConvertFloat4ToU32(
                            ImVec4(0.006f, 0.010f, 0.011f, 0.56f)), 7.0f);
    draw->AddLine(ImVec2(p.x + half, p.y + 9.0f),
                  ImVec2(p.x + half, p.y + height - 9.0f),
                  ImGui::ColorConvertFloat4ToU32(
                      ImVec4(0.19f, 0.23f, 0.23f, 0.38f)), 1.0f);

    const ImU32 primary = ImGui::ColorConvertFloat4ToU32(text_primary());
    const ImU32 tp = ImGui::ColorConvertFloat4ToU32(
        loudness.true_peak_valid ? true_peak_color(loudness.true_peak_dbtp) : text_muted());
    const ImU32 muted = ImGui::ColorConvertFloat4ToU32(text_muted());

    draw_centered_text(draw, integrated, left_center, p.y + 7.0f,
                       primary, loudness_font);
    draw_centered_text(draw, true_peak, right_center, p.y + 7.0f,
                       tp, loudness_font);
    draw_centered_text(draw, "LUFS-I", left_center, p.y + 42.0f, muted);
    draw_centered_text(draw, "dBTP", right_center, p.y + 42.0f, muted);

    ImGui::Dummy(ImVec2(width, height));
}

inline void render_p8_master_console()
{
    draw_brushed_metal_backplate();

    if (semibold_font)
        ImGui::PushFont(semibold_font);
    ImGui::TextColored(text_primary(), "MASTER");
    if (semibold_font)
        ImGui::PopFont();

    const std::string status = std::string("● ") + aggregate_health_text();
    const float status_width = ImGui::CalcTextSize(status.c_str()).x;
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
        ImGui::GetWindowContentRegionMax().x - status_width));
    ImGui::TextColored(aggregate_health_color(), "%s", status.c_str());

    ImGui::TextDisabled("%u smp latency", console_frame.latency_samples);
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    render_p8_master_surface();

    ImGui::Dummy(ImVec2(0.0f, 7.0f));
    ImGui::TextDisabled("LOUDNESS");
    ImGui::Dummy(ImVec2(0.0f, 1.0f));
    render_p8_loudness();

    ImGui::Dummy(ImVec2(0.0f, 9.0f));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 line = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    draw->AddLine(line, ImVec2(line.x + width, line.y),
                  ImGui::ColorConvertFloat4ToU32(
                      ImVec4(0.16f, 0.20f, 0.20f, 0.46f)), 1.0f);
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    const char* footer = "OUTPUT  >  OBS";
    const float footer_w = ImGui::CalcTextSize(footer).x;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
        ImGui::GetWindowContentRegionMax().x - footer_w));
    if (semibold_font)
        ImGui::PushFont(semibold_font);
    ImGui::TextColored(accent(), "%s", footer);
    if (semibold_font)
        ImGui::PopFont();
}

inline void push_p8_scrollbar_style(bool overflow)
{
    if (p8_scroll_style_pushed)
        return;
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    if (overflow) {
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,
                              ImVec4(0.28f, 0.34f, 0.34f, 0.32f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,
                              ImVec4(0.36f, 0.48f, 0.49f, 0.52f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,
                              ImVec4(0.12f, 0.64f, 0.68f, 0.64f));
    } else {
        const ImVec4 invisible(0.0f, 0.0f, 0.0f, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, invisible);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, invisible);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, invisible);
    }
    p8_scroll_style_pushed = true;
}

inline void pop_p8_scrollbar_style()
{
    if (!p8_scroll_style_pushed)
        return;
    ImGui::PopStyleColor(4);
    p8_scroll_style_pushed = false;
}

} // namespace safevst3::rack::ui::p8

namespace ImGui {

inline void SafeVst3P8StyleColorsDark(ImGuiStyle* dst = nullptr)
{
    ImGui::SafeVst3P7StyleColorsDark(dst);
    ImGuiStyle& style = dst ? *dst : ImGui::GetStyle();
    style.ItemSpacing = ImVec2(7.0f, 5.0f);
    style.ScrollbarSize = 8.0f;
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.34f, 0.34f, 0.24f);
}

inline bool SafeVst3P8Button(const char* label,
                             const ImVec2& size_arg = ImVec2(0, 0))
{
    using namespace safevst3::rack::ui::p6;
    using namespace safevst3::rack::ui::p8;

    if (luxury_slot_depth > 0 && label) {
        if (std::strcmp(label, "Bypass") == 0 ||
            std::strcmp(label, "Enable") == 0) {
            const ImVec2 min(p8_slot_led.x - 9.0f, p8_slot_led.y - 13.0f);
            const ImVec2 max(p8_slot_led.x + 9.0f, p8_slot_led.y + 13.0f);
            return p8_invisible_hit("##p8-led-toggle", min, max,
                                    std::strcmp(label, "Bypass") == 0
                                        ? "Bypass effect"
                                        : "Enable effect",
                                    true);
        }
        if (std::strcmp(label, "Open UI") == 0)
            return p8_invisible_hit("##p8-open-name", p8_slot_name_min,
                                    p8_slot_name_max, "Open plug-in UI");
        if (std::strcmp(label, "...") == 0)
            return draw_p8_slot_action_button();
    }

    return ImGui::SafeVst3P7Button(label, size_arg);
}

inline void SafeVst3P8Text(const char* fmt, ...)
{
    using namespace safevst3::rack::ui::p6;
    using namespace safevst3::rack::ui::p8;

    if (!fmt)
        return;

    va_list args;
    va_start(args, fmt);

    if (luxury_slot_depth > 0 && std::strcmp(fmt, "%u  %s") == 0) {
        const unsigned index = va_arg(args, unsigned);
        const char* name = va_arg(args, const char*);
        draw_p8_slot_identity(index, name);
        va_end(args);
        return;
    }

    if (luxury_slot_depth > 0 && std::strcmp(fmt, "%s · %u samples%s") == 0) {
        const char* health = va_arg(args, const char*);
        (void)va_arg(args, unsigned);
        (void)va_arg(args, const char*);
        draw_p8_slot_health(health);
        va_end(args);
        return;
    }

    if (std::strcmp(fmt, "OBS Safe VST3 Rack — %s") == 0) {
        const char* rack = va_arg(args, const char*);
        va_end(args);
        ImGui::SafeVst3P7Text("OBS Safe VST3 Rack — %s", rack);
        return;
    }

    ImGui::TextV(fmt, args);
    va_end(args);
}

inline void SafeVst3P8TextUnformatted(const char* text,
                                      const char* text_end = nullptr)
{
    using namespace safevst3::rack::ui::p1;
    using namespace safevst3::rack::ui::p3;
    using namespace safevst3::rack::ui::p6;
    using namespace safevst3::rack::ui::p7;
    using namespace safevst3::rack::ui::p8;

    if (!text) {
        ImGui::TextUnformatted(text, text_end);
        return;
    }

    if (!text_end && std::strcmp(text, "INPUT") == 0) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float estimated =
            static_cast<float>(console_frame.slot_count) * (kP7SlotHeight + 5.0f) + 14.0f;
        const bool overflow = estimated > std::max(110.0f, available.y - 36.0f);
        push_p8_scrollbar_style(overflow);
        ImGui::SafeVst3P7TextUnformatted(text, text_end);
        return;
    }

    if (!text_end && std::strcmp(text, "OUTPUT TO OBS") == 0) {
        if (p7_rack_well_started) {
            ImGui::EndChild();
            p7_rack_well_started = false;
        }
        pop_p8_scrollbar_style();

        if (console_frame.split_started) {
            ImGui::EndChild();
            ImGui::SameLine(0.0f, kP7PaneGap);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15.0f, 12.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, metal_bottom());
            ImGui::PushStyleColor(ImGuiCol_Border,
                                  ImVec4(0.28f, 0.32f, 0.32f, 0.28f));
            ImGui::BeginChild("rack-p8-master-console",
                              ImVec2(0.0f, console_frame.pane_height),
                              ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);

            render_p8_master_console();
            ImGui::EndChild();
            console_frame.split_started = false;
        } else {
            ImGui::Spacing();
            draw_section_label("OUTPUT", "TO OBS");
        }
        return;
    }

    ImGui::SafeVst3P7TextUnformatted(text, text_end);
}

} // namespace ImGui

#define StyleColorsDark SafeVst3P8StyleColorsDark
#define BeginChild SafeVst3P7BeginChild
#define EndChild SafeVst3P7EndChild
#define Button SafeVst3P8Button
#define Text SafeVst3P8Text
#define TextUnformatted SafeVst3P8TextUnformatted

#endif // _WIN32
