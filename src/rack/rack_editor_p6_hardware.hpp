#pragma once

#ifdef _WIN32

#include "rack/rack_editor_p5_broadcast.hpp"
#include "rack/rack_master_controls.hpp"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <string>

#undef StyleColorsDark
#undef BeginChild
#undef EndChild
#undef Button
#undef Text
#undef TextDisabled
#undef TextUnformatted
#ifdef BeginCombo
#undef BeginCombo
#endif
#ifdef InputTextWithHint
#undef InputTextWithHint
#endif

namespace safevst3::rack::ui::p6 {

using namespace safevst3::rack::ui;
using namespace safevst3::rack::ui::p1;
using namespace safevst3::rack::ui::p3;
using namespace safevst3::rack::ui::p4;
using namespace safevst3::rack::ui::p5;

inline constexpr float kLuxurySlotHeight = 48.0f;
inline constexpr float kLuxuryConsoleShare = 0.42f;
inline constexpr float kLuxuryGap = 11.0f;

inline ImVec4 metal_top() noexcept { return ImVec4(0.070f, 0.080f, 0.081f, 1.0f); }
inline ImVec4 metal_bottom() noexcept { return ImVec4(0.020f, 0.026f, 0.027f, 1.0f); }
inline ImVec4 metal_recess() noexcept { return ImVec4(0.010f, 0.014f, 0.015f, 1.0f); }
inline ImVec4 metal_strip() noexcept { return ImVec4(0.028f, 0.035f, 0.036f, 0.98f); }
inline ImVec4 metal_edge() noexcept { return ImVec4(0.23f, 0.27f, 0.27f, 0.34f); }
inline ImVec4 cyan_led() noexcept { return ImVec4(0.08f, 0.88f, 0.94f, 1.0f); }

struct HardwareMeterBallistics {
    float in_l = kMeterFloorDb;
    float in_r = kMeterFloorDb;
    float out_l = kMeterFloorDb;
    float out_r = kMeterFloorDb;
    float in_l_hold = kMeterFloorDb;
    float in_r_hold = kMeterFloorDb;
    float out_l_hold = kMeterFloorDb;
    float out_r_hold = kMeterFloorDb;
    float in_l_hold_time = 0.0f;
    float in_r_hold_time = 0.0f;
    float out_l_hold_time = 0.0f;
    float out_r_hold_time = 0.0f;
    bool valid = false;
};

inline thread_local HardwareMeterBallistics hardware_meter{};
inline thread_local int luxury_slot_depth = 0;
inline thread_local bool rack_well_started = false;
inline thread_local float slot_row_y = 0.0f;
inline thread_local ImVec2 slot_led_screen_pos{};
inline thread_local int fader_reset_lock = 0;

inline void update_hardware_meter()
{
    const RackMeterTelemetrySnapshot sample = g_rack_meter_telemetry.snapshot();
    const float dt = ImGui::GetIO().DeltaTime;
    const auto db = [](float linear) noexcept {
        return clamp_meter_db(rack_meter_linear_to_db(linear));
    };

    const float in_l = sample.valid ? db(sample.input_left_peak_linear) : kMeterFloorDb;
    const float in_r = sample.valid ? db(sample.input_right_peak_linear) : kMeterFloorDb;
    const float out_l = sample.valid ? db(sample.output_left_peak_linear) : kMeterFloorDb;
    const float out_r = sample.valid ? db(sample.output_right_peak_linear) : kMeterFloorDb;

    update_one_meter(in_l, dt, hardware_meter.in_l, hardware_meter.in_l_hold,
                     hardware_meter.in_l_hold_time);
    update_one_meter(in_r, dt, hardware_meter.in_r, hardware_meter.in_r_hold,
                     hardware_meter.in_r_hold_time);
    update_one_meter(out_l, dt, hardware_meter.out_l, hardware_meter.out_l_hold,
                     hardware_meter.out_l_hold_time);
    update_one_meter(out_r, dt, hardware_meter.out_r, hardware_meter.out_r_hold,
                     hardware_meter.out_r_hold_time);
    hardware_meter.valid = sample.valid;
}

inline void draw_brushed_metal_backplate()
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetWindowPos();
    const ImVec2 s = ImGui::GetWindowSize();
    const ImVec2 q(p.x + s.x, p.y + s.y);

    draw->AddRectFilledMultiColor(
        p, q,
        ImGui::ColorConvertFloat4ToU32(metal_top()),
        ImGui::ColorConvertFloat4ToU32(metal_top()),
        ImGui::ColorConvertFloat4ToU32(metal_bottom()),
        ImGui::ColorConvertFloat4ToU32(metal_bottom()));

    const ImU32 grain_hi = ImGui::ColorConvertFloat4ToU32(
        ImVec4(0.52f, 0.57f, 0.57f, 0.020f));
    const ImU32 grain_lo = ImGui::ColorConvertFloat4ToU32(
        ImVec4(0.0f, 0.0f, 0.0f, 0.050f));
    for (float y = p.y + 2.0f; y < q.y - 2.0f; y += 4.0f) {
        draw->AddLine(ImVec2(p.x + 1.0f, y), ImVec2(q.x - 1.0f, y),
                      (static_cast<int>(y) & 4) ? grain_hi : grain_lo, 1.0f);
    }

    draw->AddLine(ImVec2(p.x + 2.0f, p.y + 1.0f),
                  ImVec2(q.x - 2.0f, p.y + 1.0f),
                  ImGui::ColorConvertFloat4ToU32(
                      ImVec4(0.72f, 0.77f, 0.77f, 0.12f)), 1.0f);
}

inline void draw_recessed_rack_bay()
{
    draw_brushed_metal_backplate();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetWindowPos();
    const ImVec2 s = ImGui::GetWindowSize();
    const ImVec2 a(p.x + 5.0f, p.y + 5.0f);
    const ImVec2 b(p.x + s.x - 5.0f, p.y + s.y - 5.0f);

    draw->AddRectFilled(a, b,
                        ImGui::ColorConvertFloat4ToU32(
                            ImVec4(0.010f, 0.015f, 0.016f, 0.965f)),
                        8.0f);
    draw->AddRect(a, b,
                  ImGui::ColorConvertFloat4ToU32(metal_edge()),
                  8.0f, 0, 1.0f);
    draw->AddLine(ImVec2(a.x + 7.0f, a.y + 1.0f),
                  ImVec2(b.x - 7.0f, a.y + 1.0f),
                  ImGui::ColorConvertFloat4ToU32(
                      ImVec4(0.63f, 0.68f, 0.68f, 0.07f)), 1.0f);
}

inline ImU32 segment_color(float db, bool active) noexcept
{
    if (!active)
        return ImGui::ColorConvertFloat4ToU32(
            ImVec4(0.070f, 0.082f, 0.082f, 0.62f));
    if (db >= -3.0f)
        return ImGui::ColorConvertFloat4ToU32(
            ImVec4(0.98f, 0.20f, 0.13f, 1.0f));
    if (db >= -12.0f)
        return ImGui::ColorConvertFloat4ToU32(
            ImVec4(1.00f, 0.69f, 0.10f, 1.0f));
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(0.98f, 0.50f, 0.055f, 1.0f));
}

inline void draw_segment_meter(ImDrawList* draw,
                               float center_x,
                               float top,
                               float bottom,
                               float width,
                               float db,
                               float hold_db)
{
    constexpr int kSegments = 24;
    constexpr float kGap = 1.8f;
    const float height = bottom - top;
    const float segment_h =
        (height - kGap * static_cast<float>(kSegments - 1)) /
        static_cast<float>(kSegments);
    const int lit = static_cast<int>(
        std::ceil(meter_ratio(db) * static_cast<float>(kSegments)));

    draw->AddRectFilled(
        ImVec2(center_x - width * 0.82f, top - 5.0f),
        ImVec2(center_x + width * 0.82f, bottom + 5.0f),
        ImGui::ColorConvertFloat4ToU32(metal_recess()), 5.0f);
    draw->AddRect(
        ImVec2(center_x - width * 0.82f, top - 5.0f),
        ImVec2(center_x + width * 0.82f, bottom + 5.0f),
        ImGui::ColorConvertFloat4ToU32(
            ImVec4(0.28f, 0.32f, 0.32f, 0.22f)), 5.0f, 0, 1.0f);

    for (int index = 0; index < kSegments; ++index) {
        const float y1 =
            bottom - (segment_h + kGap) * static_cast<float>(index + 1) + kGap;
        const float y2 = y1 + segment_h;
        const bool active = index < lit;
        const float segment_db =
            kMeterFloorDb +
            (kMeterCeilingDb - kMeterFloorDb) *
                (static_cast<float>(index + 1) /
                 static_cast<float>(kSegments));

        if (active) {
            const ImVec4 glow =
                segment_db >= -3.0f
                    ? ImVec4(1.0f, 0.20f, 0.12f, 0.11f)
                    : ImVec4(1.0f, 0.58f, 0.07f, 0.10f);
            draw->AddRectFilled(
                ImVec2(center_x - width * 0.82f, y1 - 1.3f),
                ImVec2(center_x + width * 0.82f, y2 + 1.3f),
                ImGui::ColorConvertFloat4ToU32(glow), 2.0f);
        }

        draw->AddRectFilled(
            ImVec2(center_x - width * 0.50f, y1),
            ImVec2(center_x + width * 0.50f, y2),
            segment_color(segment_db, active), 1.25f);
    }

    if (hold_db > kMeterFloorDb) {
        const float hold_y =
            bottom - height * meter_ratio(hold_db);
        draw->AddLine(
            ImVec2(center_x - width * 0.62f, hold_y),
            ImVec2(center_x + width * 0.62f, hold_y),
            ImGui::ColorConvertFloat4ToU32(
                ImVec4(1.0f, 0.86f, 0.42f, 0.95f)), 1.2f);
    }
}

inline float fader_value_ratio(float db) noexcept
{
    return std::clamp(
        (kRackMasterMaxDb - db) /
            (kRackMasterMaxDb - kRackMasterMinDb),
        0.0f, 1.0f);
}

inline bool draw_metal_fader(const char* id,
                             const char* tooltip_label,
                             float& db,
                             float x,
                             float top,
                             float bottom,
                             float width)
{
    const ImVec2 saved_cursor = ImGui::GetCursorScreenPos();
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
            (ImGui::GetIO().MousePos.y - top) /
                std::max(1.0f, bottom - top),
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
        db = std::clamp(
            db + ImGui::GetIO().MouseWheel * 0.2f,
            kRackMasterMinDb, kRackMasterMaxDb);
        changed = true;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float track_top = top + 5.0f;
    const float track_bottom = bottom - 5.0f;

    draw->AddRectFilled(
        ImVec2(x - 3.0f, track_top),
        ImVec2(x + 3.0f, track_bottom),
        ImGui::ColorConvertFloat4ToU32(
            ImVec4(0.006f, 0.009f, 0.010f, 1.0f)),
        3.0f);
    draw->AddLine(
        ImVec2(x - 2.0f, track_top + 1.0f),
        ImVec2(x - 2.0f, track_bottom - 1.0f),
        ImGui::ColorConvertFloat4ToU32(
            ImVec4(0.54f, 0.58f, 0.58f, 0.13f)), 1.0f);

    const float zero_y =
        track_top + (track_bottom - track_top) *
                        fader_value_ratio(0.0f);
    draw->AddLine(
        ImVec2(x - 10.0f, zero_y), ImVec2(x + 10.0f, zero_y),
        ImGui::ColorConvertFloat4ToU32(
            ImVec4(0.82f, 0.88f, 0.87f, 0.20f)), 1.0f);

    const float cap_y =
        track_top + (track_bottom - track_top) *
                        fader_value_ratio(db);
    const ImVec2 cap_min(x - 16.0f, cap_y - 9.0f);
    const ImVec2 cap_max(x + 16.0f, cap_y + 9.0f);

    draw->AddRectFilledMultiColor(
        cap_min, cap_max,
        ImGui::ColorConvertFloat4ToU32(
            ImVec4(0.72f, 0.75f, 0.74f, 1.0f)),
        ImGui::ColorConvertFloat4ToU32(
            ImVec4(0.50f, 0.53f, 0.52f, 1.0f)),
        ImGui::ColorConvertFloat4ToU32(
            ImVec4(0.22f, 0.24f, 0.24f, 1.0f)),
        ImGui::ColorConvertFloat4ToU32(
            ImVec4(0.31f, 0.33f, 0.33f, 1.0f)));
    draw->AddRect(
        cap_min, cap_max,
        ImGui::ColorConvertFloat4ToU32(
            ImVec4(0.80f, 0.84f, 0.83f, hovered ? 0.46f : 0.30f)),
        3.0f, 0, 1.0f);
    draw->AddLine(
        ImVec2(cap_min.x + 4.0f, cap_y),
        ImVec2(cap_max.x - 4.0f, cap_y),
        ImGui::ColorConvertFloat4ToU32(
            ImVec4(0.94f, 0.96f, 0.94f, 0.66f)), 1.0f);

    if (hovered)
        ImGui::SetTooltip(
            "%s  %.1f dB\nDrag · wheel fine adjust · double-click = 0.0 dB",
            tooltip_label, db);

    ImGui::SetCursorScreenPos(saved_cursor);
    return changed;
}

inline void draw_centered_text(ImDrawList* draw,
                               const char* text,
                               float x,
                               float y,
                               ImU32 color,
                               ImFont* font = nullptr)
{
    if (!text)
        return;
    ImVec2 size{};
    if (font)
        size = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, text);
    else
        size = ImGui::CalcTextSize(text);

    if (font)
        draw->AddText(font, font->FontSize,
                      ImVec2(x - size.x * 0.5f, y), color, text);
    else
        draw->AddText(ImVec2(x - size.x * 0.5f, y), color, text);
}

inline void draw_centered_value(ImDrawList* draw,
                                float x,
                                float y,
                                float value,
                                bool valid,
                                const char* suffix = nullptr)
{
    char text[32] = "--";
    if (valid) {
        if (suffix)
            _snprintf_s(text, sizeof(text), _TRUNCATE,
                        "%+.1f%s", value, suffix);
        else
            _snprintf_s(text, sizeof(text), _TRUNCATE,
                        "%.1f", value);
    }
    draw_centered_text(
        draw, text, x, y,
        ImGui::ColorConvertFloat4ToU32(
            valid ? text_primary() : text_muted()));
}

inline void render_aligned_master_surface()
{
    update_hardware_meter();
    RackMasterControlSnapshot controls =
        g_rack_master_controls.snapshot();
    float input_db = controls.input_db;
    float output_db = controls.output_db;

    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const float group_width = width * 0.5f;
    const float input_center = origin.x + group_width * 0.50f;
    const float output_center = origin.x + group_width * 1.50f;
    const float control_top = origin.y + 39.0f;
    const float control_bottom = control_top + 188.0f;
    const float fader_offset = 39.0f;
    const float meter_offset = 18.0f;
    const float meter_gap = 15.0f;
    const float meter_width = 8.0f;
    const float panel_bottom = control_bottom + 58.0f;

    draw->AddRectFilled(
        ImVec2(origin.x, origin.y),
        ImVec2(origin.x + width, panel_bottom),
        ImGui::ColorConvertFloat4ToU32(
            ImVec4(0.010f, 0.015f, 0.016f, 0.78f)),
        9.0f);
    draw->AddRect(
        ImVec2(origin.x, origin.y),
        ImVec2(origin.x + width, panel_bottom),
        ImGui::ColorConvertFloat4ToU32(metal_edge()),
        9.0f, 0, 1.0f);

    draw->AddLine(
        ImVec2(origin.x + group_width, origin.y + 11.0f),
        ImVec2(origin.x + group_width, panel_bottom - 11.0f),
        ImGui::ColorConvertFloat4ToU32(
            ImVec4(0.18f, 0.22f, 0.22f, 0.48f)), 1.0f);

    const ImU32 label_color =
        ImGui::ColorConvertFloat4ToU32(text_muted());
    draw_centered_text(draw, "INPUT", input_center,
                       origin.y + 10.0f, label_color);
    draw_centered_text(draw, "OUTPUT", output_center,
                       origin.y + 10.0f, label_color);

    const float in_fader_x = input_center - fader_offset;
    const float out_fader_x = output_center - fader_offset;
    const float in_l_x = input_center + meter_offset;
    const float in_r_x = in_l_x + meter_gap;
    const float out_l_x = output_center + meter_offset;
    const float out_r_x = out_l_x + meter_gap;

    if (draw_metal_fader("##p6-input-fader", "Input Trim",
                         input_db, in_fader_x,
                         control_top, control_bottom, 42.0f))
        g_rack_master_controls.set_input_db(input_db);

    if (draw_metal_fader("##p6-output-fader", "Output Fader",
                         output_db, out_fader_x,
                         control_top, control_bottom, 42.0f))
        g_rack_master_controls.set_output_db(output_db);

    draw_segment_meter(draw, in_l_x, control_top, control_bottom,
                       meter_width, hardware_meter.in_l,
                       hardware_meter.in_l_hold);
    draw_segment_meter(draw, in_r_x, control_top, control_bottom,
                       meter_width, hardware_meter.in_r,
                       hardware_meter.in_r_hold);
    draw_segment_meter(draw, out_l_x, control_top, control_bottom,
                       meter_width, hardware_meter.out_l,
                       hardware_meter.out_l_hold);
    draw_segment_meter(draw, out_r_x, control_top, control_bottom,
                       meter_width, hardware_meter.out_r,
                       hardware_meter.out_r_hold);

    draw_centered_text(draw, "TRIM", in_fader_x,
                       control_bottom + 9.0f, label_color);
    draw_centered_text(draw, "FADER", out_fader_x,
                       control_bottom + 9.0f, label_color);
    draw_centered_text(draw, "L", in_l_x,
                       control_bottom + 9.0f, label_color);
    draw_centered_text(draw, "R", in_r_x,
                       control_bottom + 9.0f, label_color);
    draw_centered_text(draw, "L", out_l_x,
                       control_bottom + 9.0f, label_color);
    draw_centered_text(draw, "R", out_r_x,
                       control_bottom + 9.0f, label_color);

    draw_centered_value(draw, in_fader_x,
                        control_bottom + 30.0f,
                        input_db, true, " dB");
    draw_centered_value(draw, out_fader_x,
                        control_bottom + 30.0f,
                        output_db, true, " dB");

    draw_centered_value(
        draw, (in_l_x + in_r_x) * 0.5f,
        control_bottom + 30.0f,
        std::max(hardware_meter.in_l, hardware_meter.in_r),
        hardware_meter.valid);
    draw_centered_value(
        draw, (out_l_x + out_r_x) * 0.5f,
        control_bottom + 30.0f,
        std::max(hardware_meter.out_l, hardware_meter.out_r),
        hardware_meter.valid);

    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, panel_bottom - origin.y));
}

inline void render_hardware_loudness()
{
    const RackBroadcastLoudnessSnapshot loudness =
        g_rack_broadcast_loudness.snapshot();

    char integrated[24] = "--.-";
    char true_peak[24] = "--.-";
    if (loudness.integrated_valid)
        _snprintf_s(integrated, sizeof(integrated), _TRUNCATE,
                    "%.1f", loudness.integrated_lufs);
    if (loudness.true_peak_valid)
        _snprintf_s(true_peak, sizeof(true_peak), _TRUNCATE,
                    "%.1f", loudness.true_peak_dbtp);

    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float half = width * 0.5f;
    const float left_center = p.x + half * 0.5f;
    const float right_center = p.x + half * 1.5f;
    const float height = 78.0f;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(
        p, ImVec2(p.x + width, p.y + height),
        ImGui::ColorConvertFloat4ToU32(
            ImVec4(0.010f, 0.014f, 0.015f, 0.66f)),
        8.0f);
    draw->AddRect(
        p, ImVec2(p.x + width, p.y + height),
        ImGui::ColorConvertFloat4ToU32(metal_edge()),
        8.0f, 0, 1.0f);
    draw->AddLine(
        ImVec2(p.x + half, p.y + 9.0f),
        ImVec2(p.x + half, p.y + height - 9.0f),
        ImGui::ColorConvertFloat4ToU32(
            ImVec4(0.18f, 0.22f, 0.22f, 0.46f)), 1.0f);

    const ImU32 value_color =
        ImGui::ColorConvertFloat4ToU32(text_primary());
    const ImU32 tp_color =
        ImGui::ColorConvertFloat4ToU32(
            loudness.true_peak_valid
                ? true_peak_color(loudness.true_peak_dbtp)
                : text_muted());
    const ImU32 label_color =
        ImGui::ColorConvertFloat4ToU32(text_muted());

    draw_centered_text(draw, integrated, left_center,
                       p.y + 12.0f, value_color, loudness_font);
    draw_centered_text(draw, true_peak, right_center,
                       p.y + 12.0f, tp_color, loudness_font);
    draw_centered_text(draw, "LUFS-I", left_center,
                       p.y + 48.0f, label_color);
    draw_centered_text(draw, "dBTP", right_center,
                       p.y + 48.0f, label_color);

    ImGui::Dummy(ImVec2(width, height));
}

inline void render_hardware_master_console()
{
    draw_brushed_metal_backplate();

    if (semibold_font)
        ImGui::PushFont(semibold_font);
    ImGui::TextColored(text_primary(), "SAFE VST3");
    if (semibold_font)
        ImGui::PopFont();

    const std::string status =
        std::string("● ") + aggregate_health_text();
    const float status_width =
        ImGui::CalcTextSize(status.c_str()).x;
    ImGui::SameLine();
    ImGui::SetCursorPosX(
        std::max(ImGui::GetCursorPosX(),
                 ImGui::GetWindowContentRegionMax().x -
                     status_width));
    ImGui::TextColored(
        aggregate_health_color(), "%s", status.c_str());

    ImGui::TextDisabled(
        "%u FX  ·  %u smp",
        console_frame.slot_count,
        console_frame.latency_samples);

    ImGui::Dummy(ImVec2(0.0f, 7.0f));
    render_aligned_master_surface();

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextDisabled("LOUDNESS");
    render_hardware_loudness();

    ImGui::Dummy(ImVec2(0.0f, 7.0f));
    if (semibold_font)
        ImGui::PushFont(semibold_font);
    ImGui::TextColored(accent(), "TO OBS");
    if (semibold_font)
        ImGui::PopFont();
}

inline bool is_toolbar_label(const char* label) noexcept
{
    return label &&
           (std::strcmp(label, "Save As") == 0 ||
            std::strcmp(label, "Update") == 0 ||
            std::strcmp(label, "Preset ...") == 0 ||
            std::strcmp(label, "Add Effect") == 0 ||
            std::strcmp(label, "Refresh") == 0);
}

inline bool draw_luxury_button(const char* label,
                               const char* display,
                               const ImVec2& size,
                               bool accent_button)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

    if (accent_button) {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(0.035f, 0.24f, 0.27f, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(0.045f, 0.34f, 0.38f, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            ImVec4(0.055f, 0.40f, 0.44f, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(0.82f, 0.98f, 1.0f, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_Border,
            ImVec4(0.08f, 0.62f, 0.68f, 0.42f));
    } else {
        ImGui::PushStyleColor(
            ImGuiCol_Button, metal_strip());
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(0.055f, 0.070f, 0.071f, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            ImVec4(0.065f, 0.085f, 0.087f, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_Text, text_primary());
        ImGui::PushStyleColor(
            ImGuiCol_Border, metal_edge());
    }

    std::string id_text =
        std::string(display ? display : label) +
        "##p6-" + (label ? label : "button");
    const bool pressed =
        ImGui::Button(id_text.c_str(), size);

    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(2);
    return pressed;
}

} // namespace safevst3::rack::ui::p6

namespace ImGui {

inline void SafeVst3P6StyleColorsDark(ImGuiStyle* dst = nullptr)
{
    ImGui::SafeVst3P5StyleColorsDark(dst);
    ImGuiStyle& style = dst ? *dst : ImGui::GetStyle();

    style.WindowPadding = ImVec2(14.0f, 11.0f);
    style.FramePadding = ImVec2(10.0f, 7.0f);
    style.ItemSpacing = ImVec2(7.0f, 7.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
    style.WindowRounding = 0.0f;
    style.ChildRounding = 9.0f;
    style.FrameRounding = 6.0f;
    style.ChildBorderSize = 0.0f;
    style.FrameBorderSize = 1.0f;
    style.ScrollbarSize = 11.0f;
    style.GrabMinSize = 10.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] =
        ImVec4(0.014f, 0.019f, 0.020f, 1.0f);
    colors[ImGuiCol_ChildBg] =
        safevst3::rack::ui::p6::metal_bottom();
    colors[ImGuiCol_FrameBg] =
        ImVec4(0.027f, 0.034f, 0.035f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] =
        ImVec4(0.042f, 0.055f, 0.056f, 1.0f);
    colors[ImGuiCol_FrameBgActive] =
        ImVec4(0.050f, 0.067f, 0.068f, 1.0f);
    colors[ImGuiCol_Border] =
        safevst3::rack::ui::p6::metal_edge();
    colors[ImGuiCol_Separator] =
        ImVec4(0.13f, 0.16f, 0.16f, 0.60f);
}

inline bool SafeVst3P6BeginChild(
    const char* str_id,
    const ImVec2& size_arg = ImVec2(0, 0),
    ImGuiChildFlags child_flags = 0,
    ImGuiWindowFlags window_flags = 0)
{
    using namespace safevst3::rack::ui::p6;

    const bool slot =
        str_id &&
        std::strcmp(str_id, "rack-slot-card") == 0;
    if (!slot)
        return ImGui::SafeVst3P3BeginChild(
            str_id, size_arg, child_flags, window_flags);

    ImGui::Indent(8.0f);
    ImVec2 size = size_arg;
    size.x = std::max(
        1.0f, ImGui::GetContentRegionAvail().x - 8.0f);
    size.y = kLuxurySlotHeight;

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 7.0f));
    ImGui::PushStyleVar(
        ImGuiStyleVar_ChildRounding, 7.0f);
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg, metal_strip());
    ImGui::PushStyleColor(
        ImGuiCol_Border, metal_edge());

    ++luxury_slot_depth;
    const bool visible = ImGui::BeginChild(
        str_id, size,
        child_flags | ImGuiChildFlags_Borders,
        window_flags);

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    if (visible) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetWindowPos();
        const ImVec2 s = ImGui::GetWindowSize();
        draw->AddLine(
            ImVec2(p.x + 7.0f, p.y + 1.0f),
            ImVec2(p.x + s.x - 7.0f, p.y + 1.0f),
            ImGui::ColorConvertFloat4ToU32(
                ImVec4(0.66f, 0.72f, 0.71f, 0.07f)),
            1.0f);
    }

    return visible;
}

inline void SafeVst3P6EndChild()
{
    using namespace safevst3::rack::ui::p6;

    if (luxury_slot_depth > 0) {
        ImGui::EndChild();
        --luxury_slot_depth;
        ImGui::Unindent(8.0f);
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        return;
    }
    ImGui::SafeVst3P3EndChild();
}

inline bool SafeVst3P6Button(
    const char* label,
    const ImVec2& size_arg = ImVec2(0, 0))
{
    using namespace safevst3::rack::ui::p1;
    using namespace safevst3::rack::ui::p6;

    if (luxury_slot_depth > 0 && label) {
        const bool enabled =
            std::strcmp(label, "Bypass") == 0;
        const bool bypassed =
            std::strcmp(label, "Enable") == 0;
        const bool open_ui =
            std::strcmp(label, "Open UI") == 0;
        const bool actions =
            std::strcmp(label, "...") == 0;

        if (enabled || bypassed || open_ui || actions) {
            const char* display =
                enabled ? "ON" :
                bypassed ? "OFF" :
                open_ui ? "OPEN" : "...";
            const float width =
                (enabled || bypassed) ? 40.0f :
                open_ui ? 52.0f : 28.0f;
            return draw_luxury_button(
                label, display,
                ImVec2(width, 24.0f),
                enabled);
        }
    }

    if (is_toolbar_label(label)) {
        const bool save =
            std::strcmp(label, "Save As") == 0;
        const bool update =
            std::strcmp(label, "Update") == 0;
        const bool preset =
            std::strcmp(label, "Preset ...") == 0;
        const bool add =
            std::strcmp(label, "Add Effect") == 0;
        const bool refresh =
            std::strcmp(label, "Refresh") == 0;

        const char* display =
            save ? "SAVE AS" :
            update ? "UPDATE" :
            preset ? "..." :
            add ? "+ ADD" : "RESCAN";
        const float width =
            save ? 66.0f :
            update ? 64.0f :
            preset ? 30.0f :
            add ? 92.0f : 62.0f;

        return draw_luxury_button(
            label, display,
            ImVec2(width, 30.0f),
            save || add);
    }

    return ImGui::SafeVst3P3Button(label, size_arg);
}

inline void SafeVst3P6Text(const char* fmt, ...)
{
    using namespace safevst3::rack::ui::p1;
    using namespace safevst3::rack::ui::p6;

    if (!fmt)
        return;

    va_list args;
    va_start(args, fmt);

    if (luxury_slot_depth > 0 &&
        std::strcmp(fmt, "%u  %s") == 0) {
        const unsigned index = va_arg(args, unsigned);
        const char* name = va_arg(args, const char*);

        slot_row_y = ImGui::GetCursorPosY();
        ImGui::AlignTextToFramePadding();

        const ImVec2 screen = ImGui::GetCursorScreenPos();
        slot_led_screen_pos =
            ImVec2(screen.x + 5.0f,
                   screen.y +
                       ImGui::GetFrameHeight() * 0.5f);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddCircle(
            slot_led_screen_pos, 5.2f,
            ImGui::ColorConvertFloat4ToU32(
                ImVec4(cyan_led().x, cyan_led().y,
                       cyan_led().z, 0.10f)),
            0, 2.0f);

        ImGui::Dummy(ImVec2(12.0f, 24.0f));
        ImGui::SameLine(0.0f, 5.0f);
        ImGui::TextColored(
            text_muted(), "%02u", index);
        ImGui::SameLine(0.0f, 8.0f);

        const float control_start =
            ImGui::GetWindowContentRegionMax().x - 128.0f;
        const ImVec2 clip_min =
            ImGui::GetCursorScreenPos();
        const ImVec2 window =
            ImGui::GetWindowPos();
        const ImVec2 clip_max(
            window.x + control_start - 8.0f,
            clip_min.y + ImGui::GetFrameHeight());

        ImGui::PushClipRect(clip_min, clip_max, true);
        if (semibold_font)
            ImGui::PushFont(semibold_font);
        ImGui::TextColored(
            text_primary(), "%s",
            name ? name : "VST3 Effect");
        if (semibold_font)
            ImGui::PopFont();
        ImGui::PopClipRect();

        va_end(args);
        return;
    }

    if (std::strcmp(fmt, "OBS Safe VST3 Rack — %s") == 0) {
        const char* rack = va_arg(args, const char*);
        reset_console_frame(rack ? rack : "VST3 Rack");
        rack_well_started = false;

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
        ImGui::TextColored(text_primary(), "%s",
                           rack ? rack : "Rack");
        if (semibold_font)
            ImGui::PopFont();

        va_end(args);
        return;
    }

    if (luxury_slot_depth > 0 &&
        std::strcmp(fmt, "%s · %u samples%s") == 0) {
        const char* health = va_arg(args, const char*);
        (void)va_arg(args, unsigned);
        (void)va_arg(args, const char*);
        observe_health(health);

        const ImVec4 color = health_color(health);
        ImDrawList* draw = ImGui::GetWindowDrawList();

        draw->AddCircleFilled(
            slot_led_screen_pos, 7.0f,
            ImGui::ColorConvertFloat4ToU32(
                ImVec4(color.x, color.y, color.z, 0.055f)));
        draw->AddCircleFilled(
            slot_led_screen_pos, 3.1f,
            ImGui::ColorConvertFloat4ToU32(color));
        draw->AddCircleFilled(
            ImVec2(slot_led_screen_pos.x - 0.7f,
                   slot_led_screen_pos.y - 0.8f),
            0.9f,
            ImGui::ColorConvertFloat4ToU32(
                ImVec4(0.95f, 1.0f, 1.0f, 0.74f)));

        ImGui::SetCursorPosY(slot_row_y);
        ImGui::SetCursorPosX(
            ImGui::GetWindowContentRegionMax().x -
            128.0f);

        va_end(args);
        return;
    }

    va_end(args);

    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
}

inline void SafeVst3P6TextDisabled(
    const char* fmt, ...)
{
    using namespace safevst3::rack::ui::p3;
    using namespace safevst3::rack::ui::p6;

    if (!fmt)
        return;

    if (luxury_slot_depth > 0 &&
        std::strcmp(fmt, "%s") == 0) {
        return;
    }

    va_list args;
    va_start(args, fmt);

    if (std::strcmp(
            fmt, "%u effects · %u samples") == 0) {
        console_frame.slot_count =
            va_arg(args, unsigned);
        console_frame.latency_samples =
            va_arg(args, unsigned);
        va_end(args);
        return;
    }

    ImGui::TextDisabledV(fmt, args);
    va_end(args);
}

inline bool SafeVst3P6BeginCombo(
    const char* label,
    const char* preview_value,
    ImGuiComboFlags flags = 0)
{
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(
        ImGuiStyleVar_FrameRounding, 6.0f);
    const bool open =
        ImGui::BeginCombo(label, preview_value, flags);
    ImGui::PopStyleVar(2);
    return open;
}

inline bool SafeVst3P6InputTextWithHint(
    const char* label,
    const char* hint,
    char* buf,
    size_t buf_size,
    ImGuiInputTextFlags flags = 0,
    ImGuiInputTextCallback callback = nullptr,
    void* user_data = nullptr)
{
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(
        ImGuiStyleVar_FrameRounding, 6.0f);
    const bool result =
        ImGui::InputTextWithHint(
            label, hint, buf, buf_size,
            flags, callback, user_data);
    ImGui::PopStyleVar(2);
    return result;
}

inline void SafeVst3P6TextUnformatted(
    const char* text,
    const char* text_end = nullptr)
{
    using namespace safevst3::rack::ui::p1;
    using namespace safevst3::rack::ui::p3;
    using namespace safevst3::rack::ui::p6;

    if (!text) {
        ImGui::TextUnformatted(text, text_end);
        return;
    }

    if (!text_end &&
        std::strcmp(text, "OBS Safe VST3 Rack") == 0) {
        rack_well_started = false;
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

    if (!text_end &&
        std::strcmp(text, "Preset") == 0) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(text_muted(), "PRESET");
        return;
    }

    if (!text_end &&
        std::strcmp(text, "INPUT") == 0) {
        const ImVec2 available =
            ImGui::GetContentRegionAvail();

        if (available.x >= kPremiumSplitThreshold) {
            const float right_width =
                std::clamp(
                    available.x * kLuxuryConsoleShare,
                    320.0f, 380.0f);
            const float left_width =
                std::max(
                    390.0f,
                    available.x - right_width -
                        kLuxuryGap);
            console_frame.pane_height =
                std::max(260.0f, available.y);

            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowPadding,
                ImVec2(10.0f, 9.0f));
            ImGui::PushStyleVar(
                ImGuiStyleVar_ChildRounding, 9.0f);
            ImGui::PushStyleColor(
                ImGuiCol_ChildBg, metal_bottom());
            ImGui::PushStyleColor(
                ImGuiCol_Border, metal_edge());

            ImGui::BeginChild(
                "rack-p6-luxury-lane",
                ImVec2(left_width,
                       console_frame.pane_height),
                ImGuiChildFlags_Borders);

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            console_frame.split_started = true;

            draw_brushed_metal_backplate();

            ImGui::TextColored(
                text_muted(), "SIGNAL CHAIN");
            ImGui::SameLine(0.0f, 7.0f);
            ImGui::TextColored(accent(), "INPUT");
            ImGui::Dummy(ImVec2(0.0f, 5.0f));

            const float well_height =
                std::max(
                    120.0f,
                    ImGui::GetContentRegionAvail().y -
                        2.0f);

            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowPadding,
                ImVec2(5.0f, 7.0f));
            ImGui::PushStyleVar(
                ImGuiStyleVar_ChildRounding, 8.0f);
            ImGui::PushStyleColor(
                ImGuiCol_ChildBg, metal_recess());
            ImGui::PushStyleColor(
                ImGuiCol_Border, metal_edge());

            ImGui::BeginChild(
                "rack-p6-slot-well",
                ImVec2(0.0f, well_height),
                ImGuiChildFlags_Borders);

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);

            draw_recessed_rack_bay();
            rack_well_started = true;
        } else {
            draw_section_label(
                "SIGNAL CHAIN", "INPUT");
            ImGui::Dummy(ImVec2(0.0f, 3.0f));
        }
        return;
    }

    if (!text_end &&
        std::strcmp(text, "OUTPUT TO OBS") == 0) {
        if (rack_well_started) {
            ImGui::EndChild();
            rack_well_started = false;
        }

        if (console_frame.split_started) {
            ImGui::EndChild();
            ImGui::SameLine(0.0f, kLuxuryGap);

            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowPadding,
                ImVec2(16.0f, 13.0f));
            ImGui::PushStyleVar(
                ImGuiStyleVar_ChildRounding, 9.0f);
            ImGui::PushStyleColor(
                ImGuiCol_ChildBg, metal_bottom());
            ImGui::PushStyleColor(
                ImGuiCol_Border, metal_edge());

            ImGui::BeginChild(
                "rack-p6-hardware-console",
                ImVec2(0.0f,
                       console_frame.pane_height),
                ImGuiChildFlags_Borders);

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);

            render_hardware_master_console();
            ImGui::EndChild();
            console_frame.split_started = false;
        } else {
            ImGui::Spacing();
            draw_section_label("OUTPUT", "TO OBS");
        }
        return;
    }

    ImGui::SafeVst3P5TextUnformatted(
        text, text_end);
}

} // namespace ImGui

#define StyleColorsDark SafeVst3P6StyleColorsDark
#define BeginChild SafeVst3P6BeginChild
#define EndChild SafeVst3P6EndChild
#define Button SafeVst3P6Button
#define Text SafeVst3P6Text
#define TextDisabled SafeVst3P6TextDisabled
#define TextUnformatted SafeVst3P6TextUnformatted
#define BeginCombo SafeVst3P6BeginCombo
#define InputTextWithHint SafeVst3P6InputTextWithHint

#endif // _WIN32
