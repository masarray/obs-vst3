#pragma once

#ifdef _WIN32

#include "rack/rack_editor_p5_broadcast.hpp"
#include "rack/rack_master_controls.hpp"

#undef StyleColorsDark
#undef TextUnformatted

namespace safevst3::rack::ui::p6 {

using namespace safevst3::rack::ui;
using namespace safevst3::rack::ui::p1;
using namespace safevst3::rack::ui::p3;
using namespace safevst3::rack::ui::p4;
using namespace safevst3::rack::ui::p5;

inline ImVec4 metal_top() noexcept { return ImVec4(0.075f, 0.086f, 0.087f, 1.0f); }
inline ImVec4 metal_bottom() noexcept { return ImVec4(0.025f, 0.031f, 0.032f, 1.0f); }
inline ImVec4 metal_recess() noexcept { return ImVec4(0.012f, 0.017f, 0.018f, 1.0f); }

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

    const ImU32 grain_a = ImGui::ColorConvertFloat4ToU32(ImVec4(0.42f, 0.46f, 0.46f, 0.025f));
    const ImU32 grain_b = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.055f));
    for (float y = p.y + 2.0f; y < q.y - 2.0f; y += 4.0f) {
        draw->AddLine(ImVec2(p.x + 1.0f, y), ImVec2(q.x - 1.0f, y),
                      (static_cast<int>(y) & 4) ? grain_a : grain_b, 1.0f);
    }
    draw->AddLine(ImVec2(p.x + 1.0f, p.y + 1.0f), ImVec2(q.x - 1.0f, p.y + 1.0f),
                  ImGui::ColorConvertFloat4ToU32(ImVec4(0.65f, 0.72f, 0.72f, 0.14f)), 1.0f);
}

inline void draw_recessed_rack_bay()
{
    draw_brushed_metal_backplate();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetWindowPos();
    const ImVec2 s = ImGui::GetWindowSize();
    const ImVec2 inset_min(p.x + 5.0f, p.y + 5.0f);
    const ImVec2 inset_max(p.x + s.x - 5.0f, p.y + s.y - 5.0f);
    draw->AddRectFilled(inset_min, inset_max,
                        ImGui::ColorConvertFloat4ToU32(ImVec4(0.014f, 0.020f, 0.021f, 0.93f)), 7.0f);
    draw->AddRect(inset_min, inset_max,
                  ImGui::ColorConvertFloat4ToU32(ImVec4(0.24f, 0.28f, 0.28f, 0.28f)),
                  7.0f, 0, 1.0f);
    draw->AddLine(ImVec2(inset_min.x + 5.0f, inset_min.y + 1.0f),
                  ImVec2(inset_max.x - 5.0f, inset_min.y + 1.0f),
                  ImGui::ColorConvertFloat4ToU32(ImVec4(0.55f, 0.61f, 0.61f, 0.08f)), 1.0f);
}

inline ImU32 segment_color(float db, bool active) noexcept
{
    if (!active)
        return ImGui::ColorConvertFloat4ToU32(ImVec4(0.09f, 0.105f, 0.105f, 0.55f));
    if (db >= -3.0f)
        return ImGui::ColorConvertFloat4ToU32(ImVec4(0.98f, 0.20f, 0.12f, 1.0f));
    if (db >= -12.0f)
        return ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.68f, 0.10f, 1.0f));
    return ImGui::ColorConvertFloat4ToU32(ImVec4(0.98f, 0.51f, 0.055f, 1.0f));
}

inline void draw_segment_meter(ImDrawList* draw,
                               float center_x,
                               float top,
                               float bottom,
                               float width,
                               float db,
                               float hold_db)
{
    constexpr int kSegments = 22;
    constexpr float kGap = 2.0f;
    const float height = bottom - top;
    const float segment_h = (height - kGap * static_cast<float>(kSegments - 1)) /
                            static_cast<float>(kSegments);
    const float ratio = meter_ratio(db);
    const int lit = static_cast<int>(std::ceil(ratio * static_cast<float>(kSegments)));

    draw->AddRectFilled(ImVec2(center_x - width * 0.75f, top - 5.0f),
                        ImVec2(center_x + width * 0.75f, bottom + 5.0f),
                        ImGui::ColorConvertFloat4ToU32(metal_recess()), 5.0f);

    for (int index = 0; index < kSegments; ++index) {
        const float y1 = bottom - (segment_h + kGap) * static_cast<float>(index + 1) + kGap;
        const float y2 = y1 + segment_h;
        const bool active = index < lit;
        const float segment_db = kMeterFloorDb +
            (kMeterCeilingDb - kMeterFloorDb) *
            (static_cast<float>(index + 1) / static_cast<float>(kSegments));
        const ImU32 colour = segment_color(segment_db, active);
        if (active) {
            const ImVec4 glow = segment_db >= -3.0f
                ? ImVec4(1.0f, 0.20f, 0.12f, 0.12f)
                : ImVec4(1.0f, 0.58f, 0.07f, 0.11f);
            draw->AddRectFilled(ImVec2(center_x - width * 0.72f, y1 - 1.5f),
                                ImVec2(center_x + width * 0.72f, y2 + 1.5f),
                                ImGui::ColorConvertFloat4ToU32(glow), 2.0f);
        }
        draw->AddRectFilled(ImVec2(center_x - width * 0.5f, y1),
                            ImVec2(center_x + width * 0.5f, y2), colour, 1.4f);
    }

    if (hold_db > kMeterFloorDb) {
        const float hold_y = bottom - height * meter_ratio(hold_db);
        draw->AddLine(ImVec2(center_x - width * 0.58f, hold_y),
                      ImVec2(center_x + width * 0.58f, hold_y),
                      ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.86f, 0.42f, 1.0f)), 1.3f);
    }
}

inline bool draw_metal_fader(const char* id,
                             const char* label,
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

    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float ratio = std::clamp((ImGui::GetIO().MousePos.y - top) /
                                      std::max(1.0f, bottom - top), 0.0f, 1.0f);
        db = kRackMasterMaxDb - ratio * (kRackMasterMaxDb - kRackMasterMinDb);
        db = std::round(db * 10.0f) * 0.1f;
        changed = true;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        db = 0.0f;
        changed = true;
    }
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        db = std::clamp(db + ImGui::GetIO().MouseWheel * 0.2f,
                        kRackMasterMinDb, kRackMasterMaxDb);
        changed = true;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float center = x;
    const float track_top = top + 5.0f;
    const float track_bottom = bottom - 5.0f;
    draw->AddRectFilled(ImVec2(center - 3.0f, track_top),
                        ImVec2(center + 3.0f, track_bottom),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(0.008f, 0.011f, 0.012f, 1.0f)), 3.0f);
    draw->AddLine(ImVec2(center - 2.0f, track_top + 1.0f),
                  ImVec2(center - 2.0f, track_bottom - 1.0f),
                  ImGui::ColorConvertFloat4ToU32(ImVec4(0.48f, 0.52f, 0.52f, 0.14f)), 1.0f);

    const float ratio = (kRackMasterMaxDb - db) / (kRackMasterMaxDb - kRackMasterMinDb);
    const float cap_y = track_top + (track_bottom - track_top) * std::clamp(ratio, 0.0f, 1.0f);
    const ImVec2 cap_min(center - 16.0f, cap_y - 9.0f);
    const ImVec2 cap_max(center + 16.0f, cap_y + 9.0f);
    draw->AddRectFilledMultiColor(
        cap_min, cap_max,
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.66f, 0.69f, 0.68f, 1.0f)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.48f, 0.51f, 0.50f, 1.0f)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.22f, 0.24f, 0.24f, 1.0f)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.31f, 0.33f, 0.33f, 1.0f)));
    draw->AddRect(cap_min, cap_max,
                  ImGui::ColorConvertFloat4ToU32(ImVec4(0.74f, 0.78f, 0.77f, 0.34f)), 3.0f);
    draw->AddLine(ImVec2(cap_min.x + 4.0f, cap_y), ImVec2(cap_max.x - 4.0f, cap_y),
                  ImGui::ColorConvertFloat4ToU32(ImVec4(0.92f, 0.94f, 0.92f, 0.60f)), 1.0f);

    const ImVec2 label_size = ImGui::CalcTextSize(label);
    draw->AddText(ImVec2(center - label_size.x * 0.5f, top - 22.0f),
                  ImGui::ColorConvertFloat4ToU32(text_muted()), label);

    if (hovered)
        ImGui::SetTooltip("%s %.1f dB\nDrag · wheel fine adjust · double-click reset", label, db);
    ImGui::SetCursorScreenPos(saved_cursor);
    return changed;
}

inline void draw_centered_value(ImDrawList* draw, float x, float y,
                                float value, bool valid, const char* suffix = nullptr)
{
    char text[24] = "--";
    if (valid) {
        if (suffix)
            _snprintf_s(text, sizeof(text), _TRUNCATE, "%.1f%s", value, suffix);
        else
            _snprintf_s(text, sizeof(text), _TRUNCATE, "%.1f", value);
    }
    const ImVec2 size = ImGui::CalcTextSize(text);
    draw->AddText(ImVec2(x - size.x * 0.5f, y),
                  ImGui::ColorConvertFloat4ToU32(valid ? text_primary() : text_muted()), text);
}

inline void render_aligned_master_surface()
{
    update_hardware_meter();
    RackMasterControlSnapshot controls = g_rack_master_controls.snapshot();
    float input_db = controls.input_db;
    float output_db = controls.output_db;

    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float control_top = origin.y + 31.0f;
    const float control_bottom = control_top + 174.0f;
    const float group_width = width * 0.5f;
    const float input_center = origin.x + group_width * 0.50f;
    const float output_center = origin.x + group_width * 1.50f;
    const float fader_offset = 32.0f;
    const float meter_gap = 15.0f;
    const float meter_width = 8.0f;

    draw->AddRectFilled(ImVec2(origin.x, origin.y),
                        ImVec2(origin.x + width, control_bottom + 54.0f),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(0.015f, 0.020f, 0.021f, 0.56f)), 8.0f);
    draw->AddLine(ImVec2(origin.x + group_width, origin.y + 9.0f),
                  ImVec2(origin.x + group_width, control_bottom + 41.0f),
                  ImGui::ColorConvertFloat4ToU32(hairline()), 1.0f);

    const char* input_label = "INPUT";
    const char* output_label = "OUTPUT";
    const ImVec2 in_size = ImGui::CalcTextSize(input_label);
    const ImVec2 out_size = ImGui::CalcTextSize(output_label);
    draw->AddText(ImVec2(input_center - in_size.x * 0.5f, origin.y + 7.0f),
                  ImGui::ColorConvertFloat4ToU32(text_muted()), input_label);
    draw->AddText(ImVec2(output_center - out_size.x * 0.5f, origin.y + 7.0f),
                  ImGui::ColorConvertFloat4ToU32(text_muted()), output_label);

    const float in_fader_x = input_center - fader_offset;
    const float out_fader_x = output_center - fader_offset;
    const float in_l_x = input_center + 20.0f;
    const float in_r_x = in_l_x + meter_gap;
    const float out_l_x = output_center + 20.0f;
    const float out_r_x = out_l_x + meter_gap;

    if (draw_metal_fader("##p6-input-fader", "TRIM", input_db,
                         in_fader_x, control_top, control_bottom, 40.0f))
        g_rack_master_controls.set_input_db(input_db);
    if (draw_metal_fader("##p6-output-fader", "FADER", output_db,
                         out_fader_x, control_top, control_bottom, 40.0f))
        g_rack_master_controls.set_output_db(output_db);

    draw_segment_meter(draw, in_l_x, control_top, control_bottom, meter_width,
                       hardware_meter.in_l, hardware_meter.in_l_hold);
    draw_segment_meter(draw, in_r_x, control_top, control_bottom, meter_width,
                       hardware_meter.in_r, hardware_meter.in_r_hold);
    draw_segment_meter(draw, out_l_x, control_top, control_bottom, meter_width,
                       hardware_meter.out_l, hardware_meter.out_l_hold);
    draw_segment_meter(draw, out_r_x, control_top, control_bottom, meter_width,
                       hardware_meter.out_r, hardware_meter.out_r_hold);

    const ImU32 muted = ImGui::ColorConvertFloat4ToU32(text_muted());
    draw->AddText(ImVec2(in_l_x - 3.0f, control_bottom + 8.0f), muted, "L");
    draw->AddText(ImVec2(in_r_x - 3.0f, control_bottom + 8.0f), muted, "R");
    draw->AddText(ImVec2(out_l_x - 3.0f, control_bottom + 8.0f), muted, "L");
    draw->AddText(ImVec2(out_r_x - 3.0f, control_bottom + 8.0f), muted, "R");

    draw_centered_value(draw, in_fader_x, control_bottom + 29.0f, input_db, true, " dB");
    draw_centered_value(draw, out_fader_x, control_bottom + 29.0f, output_db, true, " dB");
    draw_centered_value(draw, (in_l_x + in_r_x) * 0.5f, control_bottom + 29.0f,
                        std::max(hardware_meter.in_l, hardware_meter.in_r), hardware_meter.valid);
    draw_centered_value(draw, (out_l_x + out_r_x) * 0.5f, control_bottom + 29.0f,
                        std::max(hardware_meter.out_l, hardware_meter.out_r), hardware_meter.valid);

    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, 259.0f));
}

inline void render_hardware_loudness()
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
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddLine(ImVec2(p.x + half, p.y), ImVec2(p.x + half, p.y + 60.0f),
                  ImGui::ColorConvertFloat4ToU32(hairline()), 1.0f);

    if (loudness_font)
        ImGui::PushFont(loudness_font);
    ImGui::TextColored(text_primary(), "%s", integrated);
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(10.0f, half - 72.0f));
    ImGui::TextColored(loudness.true_peak_valid ? true_peak_color(loudness.true_peak_dbtp) : text_muted(),
                       "%s", true_peak);
    if (loudness_font)
        ImGui::PopFont();

    ImGui::TextDisabled("LUFS-I");
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(10.0f, half - 53.0f));
    ImGui::TextDisabled("dBTP");
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
}

inline void render_hardware_master_console()
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
    const float right = ImGui::GetWindowContentRegionMax().x;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - status_width));
    ImGui::TextColored(aggregate_health_color(), "%s", status.c_str());
    ImGui::TextDisabled("%u FX  ·  %u smp", console_frame.slot_count, console_frame.latency_samples);

    ImGui::Dummy(ImVec2(0.0f, 7.0f));
    render_aligned_master_surface();

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::TextDisabled("LOUDNESS");
    render_hardware_loudness();

    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    if (semibold_font)
        ImGui::PushFont(semibold_font);
    ImGui::TextColored(accent(), "TO OBS");
    if (semibold_font)
        ImGui::PopFont();
}

} // namespace safevst3::rack::ui::p6

namespace ImGui {

inline void SafeVst3P6StyleColorsDark(ImGuiStyle* dst = nullptr)
{
    ImGui::SafeVst3P5StyleColorsDark(dst);
    ImGuiStyle& style = dst ? *dst : ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 5.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.018f, 0.024f, 0.025f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.028f, 0.035f, 0.036f, 1.0f);
}

inline void SafeVst3P6TextUnformatted(const char* text, const char* text_end = nullptr)
{
    using namespace safevst3::rack::ui::p1;
    using namespace safevst3::rack::ui::p3;
    using namespace safevst3::rack::ui::p6;

    if (!text_end && text && std::strcmp(text, "INPUT") == 0) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (available.x >= kPremiumSplitThreshold) {
            const float right_width =
                std::clamp(available.x * kPremiumConsoleShare, 285.0f, 360.0f);
            const float left_width =
                std::max(355.0f, available.x - right_width - kPremiumConsoleGap);
            console_frame.pane_height = std::max(260.0f, available.y);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 9.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, metal_bottom());
            ImGui::BeginChild("rack-p6-recessed-lane",
                              ImVec2(left_width, console_frame.pane_height),
                              ImGuiChildFlags_None);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            draw_recessed_rack_bay();
            console_frame.split_started = true;
        }

        draw_section_label("SIGNAL CHAIN", "INPUT");
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        return;
    }

    if (!text_end && text && std::strcmp(text, "OUTPUT TO OBS") == 0) {
        if (console_frame.split_started) {
            ImGui::EndChild();
            ImGui::SameLine(0.0f, kPremiumConsoleGap);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 13.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, metal_bottom());
            ImGui::BeginChild("rack-p6-hardware-console",
                              ImVec2(0.0f, console_frame.pane_height),
                              ImGuiChildFlags_None);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);

            render_hardware_master_console();
            ImGui::EndChild();
            console_frame.split_started = false;
        } else {
            ImGui::SafeVst3P5TextUnformatted(text, text_end);
        }
        return;
    }

    ImGui::SafeVst3P5TextUnformatted(text, text_end);
}

} // namespace ImGui

#define StyleColorsDark SafeVst3P6StyleColorsDark
#define TextUnformatted SafeVst3P6TextUnformatted

#endif // _WIN32
