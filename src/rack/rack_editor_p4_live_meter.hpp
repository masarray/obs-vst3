#pragma once

#ifdef _WIN32

#include "rack/rack_editor_p3_premium.hpp"
#include "rack/rack_meter_telemetry.hpp"

#undef TextUnformatted

namespace safevst3::rack::ui::p4 {

using namespace safevst3::rack::ui;
using namespace safevst3::rack::ui::p1;
using namespace safevst3::rack::ui::p3;

inline constexpr float kMeterFloorDb = -60.0f;
inline constexpr float kMeterCeilingDb = 0.0f;
inline constexpr float kMeterReleaseDbPerSecond = 22.0f;
inline constexpr float kPeakHoldSeconds = 0.80f;
inline constexpr float kPeakHoldReleaseDbPerSecond = 14.0f;

struct MeterBallistics {
    std::uint64_t last_sequence = 0;
    float input_db = kMeterFloorDb;
    float output_db = kMeterFloorDb;
    float input_hold_db = kMeterFloorDb;
    float output_hold_db = kMeterFloorDb;
    float input_hold_time = 0.0f;
    float output_hold_time = 0.0f;
    bool valid = false;
};

inline thread_local MeterBallistics meter_ballistics{};

inline float clamp_meter_db(float db) noexcept
{
    return std::clamp(db, kMeterFloorDb, kMeterCeilingDb);
}

inline void update_one_meter(float target_db, float delta_time,
                             float& display_db, float& hold_db,
                             float& hold_time) noexcept
{
    target_db = clamp_meter_db(target_db);
    delta_time = std::clamp(delta_time, 0.0f, 0.10f);

    // Instant/fast attack; controlled release gives the same readable broadcast
    // behaviour users expect from professional rack meters.
    if (target_db >= display_db)
        display_db = target_db;
    else
        display_db = std::max(target_db,
                              display_db - kMeterReleaseDbPerSecond * delta_time);

    if (target_db >= hold_db) {
        hold_db = target_db;
        hold_time = kPeakHoldSeconds;
    } else if (hold_time > 0.0f) {
        hold_time = std::max(0.0f, hold_time - delta_time);
    } else {
        hold_db = std::max(display_db,
                           hold_db - kPeakHoldReleaseDbPerSecond * delta_time);
    }
}

inline void update_ballistics()
{
    const RackMeterTelemetrySnapshot sample = g_rack_meter_telemetry.snapshot();
    const float dt = ImGui::GetIO().DeltaTime;

    if (!sample.valid) {
        update_one_meter(kMeterFloorDb, dt,
                         meter_ballistics.input_db,
                         meter_ballistics.input_hold_db,
                         meter_ballistics.input_hold_time);
        update_one_meter(kMeterFloorDb, dt,
                         meter_ballistics.output_db,
                         meter_ballistics.output_hold_db,
                         meter_ballistics.output_hold_time);
        meter_ballistics.valid = false;
        return;
    }

    const float input_db = rack_meter_linear_to_db(sample.input_peak_linear);
    const float output_db = rack_meter_linear_to_db(sample.output_peak_linear);

    update_one_meter(input_db, dt,
                     meter_ballistics.input_db,
                     meter_ballistics.input_hold_db,
                     meter_ballistics.input_hold_time);
    update_one_meter(output_db, dt,
                     meter_ballistics.output_db,
                     meter_ballistics.output_hold_db,
                     meter_ballistics.output_hold_time);

    meter_ballistics.last_sequence = sample.sequence;
    meter_ballistics.valid = true;
}

inline float meter_ratio(float db) noexcept
{
    return std::clamp((db - kMeterFloorDb) /
                      (kMeterCeilingDb - kMeterFloorDb), 0.0f, 1.0f);
}

inline ImU32 meter_level_color(float db) noexcept
{
    if (db >= -3.0f)
        return ImGui::ColorConvertFloat4ToU32(ImVec4(0.92f, 0.22f, 0.16f, 1.0f));
    if (db >= -12.0f)
        return ImGui::ColorConvertFloat4ToU32(ImVec4(0.97f, 0.58f, 0.10f, 1.0f));
    return ImGui::ColorConvertFloat4ToU32(ImVec4(0.88f, 0.47f, 0.06f, 1.0f));
}

inline void draw_live_meter(ImDrawList* draw,
                            float center_x,
                            float top,
                            float bottom,
                            float width,
                            float db,
                            float hold_db,
                            ImU32 border)
{
    const ImVec2 track_min(center_x - width * 0.5f, top);
    const ImVec2 track_max(center_x + width * 0.5f, bottom);
    const ImU32 track_bg = ImGui::ColorConvertFloat4ToU32(
        ImVec4(0.010f, 0.014f, 0.015f, 1.0f));
    draw->AddRectFilled(track_min, track_max, track_bg, 4.0f);
    draw->AddRect(track_min, track_max, border, 4.0f, 0, 1.0f);

    const float ratio = meter_ratio(db);
    if (ratio > 0.0f) {
        const float fill_top = bottom - (bottom - top) * ratio;
        draw->AddRectFilled(
            ImVec2(track_min.x + 2.0f, fill_top),
            ImVec2(track_max.x - 2.0f, bottom - 2.0f),
            meter_level_color(db), 2.5f);
    }

    const float hold_ratio = meter_ratio(hold_db);
    if (hold_ratio > 0.0f) {
        const float hold_y = bottom - (bottom - top) * hold_ratio;
        const ImU32 hold_color = ImGui::ColorConvertFloat4ToU32(
            hold_db >= -3.0f ? ImVec4(1.0f, 0.30f, 0.20f, 1.0f)
                             : ImVec4(1.0f, 0.72f, 0.22f, 1.0f));
        draw->AddLine(ImVec2(track_min.x - 1.0f, hold_y),
                      ImVec2(track_max.x + 1.0f, hold_y), hold_color, 1.4f);
    }
}

inline void render_live_meter_surface()
{
    update_ballistics();

    const float width = std::max(220.0f, ImGui::GetContentRegionAvail().x);
    const float available_height = ImGui::GetContentRegionAvail().y;
    const float meter_height = std::clamp(available_height * 0.44f, 155.0f, 238.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const float scale_width = 34.0f;
    const float usable = std::max(160.0f, width - scale_width - 8.0f);
    const float spacing = usable / 3.0f;
    const float track_width = 14.0f;
    const ImU32 grid = ImGui::ColorConvertFloat4ToU32(
        ImVec4(0.14f, 0.165f, 0.165f, 0.58f));
    const ImU32 warm_border = ImGui::ColorConvertFloat4ToU32(
        ImVec4(meter_warm().x, meter_warm().y, meter_warm().z, 0.52f));
    const ImU32 cool_border = ImGui::ColorConvertFloat4ToU32(
        ImVec4(accent().x, accent().y, accent().z, 0.42f));
    const ImU32 muted = ImGui::ColorConvertFloat4ToU32(text_muted());

    struct Tick { const char* label; float db; };
    constexpr Tick ticks[] = {
        {"0", 0.0f}, {"-6", -6.0f}, {"-12", -12.0f},
        {"-24", -24.0f}, {"-36", -36.0f},
        {"-48", -48.0f}, {"-60", -60.0f},
    };

    for (const Tick& tick : ticks) {
        const float y = origin.y + meter_height * (1.0f - meter_ratio(tick.db));
        draw->AddLine(ImVec2(origin.x + scale_width, y),
                      ImVec2(origin.x + width - 2.0f, y), grid, 1.0f);
        draw->AddText(ImVec2(origin.x, y - 7.0f), muted, tick.label);
    }

    const float in_x = origin.x + scale_width + spacing * 0.5f;
    const float out_x = origin.x + scale_width + spacing * 1.5f;
    const float gr_x = origin.x + scale_width + spacing * 2.5f;
    const float top = origin.y;
    const float bottom = origin.y + meter_height;

    draw_live_meter(draw, in_x, top, bottom, track_width,
                    meter_ballistics.input_db,
                    meter_ballistics.input_hold_db,
                    warm_border);
    draw_live_meter(draw, out_x, top, bottom, track_width,
                    meter_ballistics.output_db,
                    meter_ballistics.output_hold_db,
                    warm_border);

    // GR remains deliberately non-fabricated. VST3 has no universal host-side
    // gain-reduction meter contract for arbitrary effects; a future compressor-
    // aware telemetry seam can light this track when authoritative data exists.
    const ImVec2 gr_min(gr_x - track_width * 0.5f, top);
    const ImVec2 gr_max(gr_x + track_width * 0.5f, bottom);
    draw->AddRectFilled(gr_min, gr_max,
                        ImGui::ColorConvertFloat4ToU32(ImVec4(0.010f, 0.014f, 0.015f, 1.0f)),
                        4.0f);
    draw->AddRect(gr_min, gr_max, cool_border, 4.0f, 0, 1.0f);

    const char* labels[] = {"IN", "OUT", "GR"};
    const float centers[] = {in_x, out_x, gr_x};
    for (int index = 0; index < 3; ++index) {
        const ImVec2 label_size = ImGui::CalcTextSize(labels[index]);
        draw->AddText(ImVec2(centers[index] - label_size.x * 0.5f, bottom + 9.0f),
                      muted, labels[index]);
    }

    const char* in_value = meter_ballistics.valid ? "LIVE" : "--";
    const char* out_value = meter_ballistics.valid ? "LIVE" : "--";
    const char* gr_value = "N/A";
    const char* values[] = {in_value, out_value, gr_value};
    for (int index = 0; index < 3; ++index) {
        const ImVec2 value_size = ImGui::CalcTextSize(values[index]);
        draw->AddText(ImVec2(centers[index] - value_size.x * 0.5f, bottom + 29.0f),
                      muted, values[index]);
    }

    // Numerical peak readout keeps the meter useful when the visual bars are
    // small. It is derived from the same authoritative audio-block peaks.
    char input_text[32]{};
    char output_text[32]{};
    if (meter_ballistics.valid) {
        _snprintf_s(input_text, sizeof(input_text), _TRUNCATE,
                    "IN %.1f dBFS", meter_ballistics.input_db);
        _snprintf_s(output_text, sizeof(output_text), _TRUNCATE,
                    "OUT %.1f dBFS", meter_ballistics.output_db);
    } else {
        strcpy_s(input_text, "IN -- dBFS");
        strcpy_s(output_text, "OUT -- dBFS");
    }

    ImGui::Dummy(ImVec2(width, meter_height + 49.0f));
    ImGui::TextDisabled("%s", input_text);
    ImGui::SameLine();
    ImGui::TextDisabled("  %s", output_text);
}

inline void render_live_master_console()
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
    ImGui::TextDisabled("METERING / PEAK dBFS");
    render_live_meter_surface();

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    ImGui::TextDisabled("OUTPUT");
    if (semibold_font)
        ImGui::PushFont(semibold_font);
    ImGui::TextColored(accent(), "TO OBS");
    if (semibold_font)
        ImGui::PopFont();
}

} // namespace safevst3::rack::ui::p4

namespace ImGui {

inline void SafeVst3P4TextUnformatted(const char* text, const char* text_end = nullptr)
{
    using namespace safevst3::rack::ui::p1;
    using namespace safevst3::rack::ui::p3;
    using namespace safevst3::rack::ui::p4;

    if (!text_end && text && std::strcmp(text, "OUTPUT TO OBS") == 0) {
        if (console_frame.split_started) {
            ImGui::EndChild();
            ImGui::SameLine(0.0f, kPremiumConsoleGap);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(17.0f, 15.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, surface_console());
            ImGui::BeginChild("rack-premium-console-live",
                              ImVec2(0.0f, console_frame.pane_height),
                              ImGuiChildFlags_None);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);

            render_live_master_console();
            ImGui::EndChild();
            console_frame.split_started = false;
        } else {
            ImGui::Spacing();
            draw_section_label("OUTPUT", "TO OBS");
        }
        return;
    }

    ImGui::SafeVst3P3TextUnformatted(text, text_end);
}

} // namespace ImGui

#define TextUnformatted SafeVst3P4TextUnformatted

#endif // _WIN32
