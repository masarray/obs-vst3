#pragma once

#ifdef _WIN32

#include "rack/rack_editor_p4_live_meter.hpp"
#include "rack/rack_broadcast_loudness.hpp"

#undef StyleColorsDark
#undef TextUnformatted

namespace safevst3::rack::ui::p5 {

using namespace safevst3::rack::ui;
using namespace safevst3::rack::ui::p1;
using namespace safevst3::rack::ui::p3;
using namespace safevst3::rack::ui::p4;

inline ImFont* loudness_font = nullptr;

inline ImVec4 true_peak_color(float dbtp) noexcept
{
    if (dbtp >= -0.1f)
        return danger();
    if (dbtp >= -1.0f)
        return warning();
    return text_primary();
}

inline void render_compact_level_surface()
{
    update_ballistics();

    const float width = std::max(220.0f, ImGui::GetContentRegionAvail().x);
    const float meter_height = std::clamp(ImGui::GetWindowHeight() * 0.27f, 132.0f, 164.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const float scale_width = 31.0f;
    const float usable = std::max(160.0f, width - scale_width - 6.0f);
    const float spacing = usable / 3.0f;
    const float track_width = 13.0f;
    const ImU32 grid = ImGui::ColorConvertFloat4ToU32(
        ImVec4(0.13f, 0.15f, 0.15f, 0.52f));
    const ImU32 warm_border = ImGui::ColorConvertFloat4ToU32(
        ImVec4(meter_warm().x, meter_warm().y, meter_warm().z, 0.44f));
    const ImU32 cool_border = ImGui::ColorConvertFloat4ToU32(
        ImVec4(accent().x, accent().y, accent().z, 0.40f));
    const ImU32 muted = ImGui::ColorConvertFloat4ToU32(text_muted());

    struct Tick { const char* label; float db; };
    constexpr Tick ticks[] = {
        {"0", 0.0f}, {"-12", -12.0f}, {"-24", -24.0f},
        {"-48", -48.0f}, {"-60", -60.0f},
    };
    for (const Tick& tick : ticks) {
        const float y = origin.y + meter_height * (1.0f - meter_ratio(tick.db));
        draw->AddLine(ImVec2(origin.x + scale_width, y),
                      ImVec2(origin.x + width - 2.0f, y), grid, 1.0f);
        draw->AddText(ImVec2(origin.x, y - 7.0f), muted, tick.label);
    }

    const float centers[] = {
        origin.x + scale_width + spacing * 0.5f,
        origin.x + scale_width + spacing * 1.5f,
        origin.x + scale_width + spacing * 2.5f,
    };
    const float top = origin.y;
    const float bottom = origin.y + meter_height;

    draw_live_meter(draw, centers[0], top, bottom, track_width,
                    meter_ballistics.input_db,
                    meter_ballistics.input_hold_db,
                    warm_border);
    draw_live_meter(draw, centers[1], top, bottom, track_width,
                    meter_ballistics.output_db,
                    meter_ballistics.output_hold_db,
                    warm_border);

    const ImVec2 gr_min(centers[2] - track_width * 0.5f, top);
    const ImVec2 gr_max(centers[2] + track_width * 0.5f, bottom);
    draw->AddRectFilled(gr_min, gr_max,
                        ImGui::ColorConvertFloat4ToU32(ImVec4(0.010f, 0.014f, 0.015f, 1.0f)),
                        4.0f);
    draw->AddRect(gr_min, gr_max, cool_border, 4.0f, 0, 1.0f);

    const char* labels[] = {"IN", "OUT", "GR"};
    for (int index = 0; index < 3; ++index) {
        const ImVec2 label_size = ImGui::CalcTextSize(labels[index]);
        draw->AddText(ImVec2(centers[index] - label_size.x * 0.5f, bottom + 8.0f),
                      muted, labels[index]);
    }

    char input_value[16] = "--";
    char output_value[16] = "--";
    if (meter_ballistics.valid) {
        _snprintf_s(input_value, sizeof(input_value), _TRUNCATE,
                    "%.1f", meter_ballistics.input_db);
        _snprintf_s(output_value, sizeof(output_value), _TRUNCATE,
                    "%.1f", meter_ballistics.output_db);
    }
    const char* values[] = {input_value, output_value, "N/A"};
    for (int index = 0; index < 3; ++index) {
        const ImVec2 value_size = ImGui::CalcTextSize(values[index]);
        draw->AddText(ImVec2(centers[index] - value_size.x * 0.5f, bottom + 27.0f),
                      index == 2 ? muted : ImGui::ColorConvertFloat4ToU32(text_primary()),
                      values[index]);
    }

    ImGui::Dummy(ImVec2(width, meter_height + 45.0f));
}

inline void render_metric_card(const char* id,
                               const char* value,
                               const char* unit,
                               const ImVec4& value_color,
                               float width,
                               const char* tooltip)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.035f, 0.042f, 0.043f, 1.0f));
    ImGui::BeginChild(id, ImVec2(width, 78.0f), ImGuiChildFlags_None);

    if (loudness_font)
        ImGui::PushFont(loudness_font);
    ImGui::TextColored(value_color, "%s", value);
    if (loudness_font)
        ImGui::PopFont();
    ImGui::TextDisabled("%s", unit);
    if (ImGui::IsItemHovered() && tooltip)
        ImGui::SetTooltip("%s", tooltip);

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

inline void render_loudness_surface()
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
    const float gap = 8.0f;
    const float card_width = std::max(92.0f, (width - gap) * 0.5f);
    render_metric_card(
        "broadcast-lufs-i", integrated, "LUFS-I", text_primary(), card_width,
        "Integrated programme loudness (BS.1770 gating) since this Rack helper started.");
    ImGui::SameLine(0.0f, gap);
    render_metric_card(
        "broadcast-dbtp", true_peak, "dBTP",
        loudness.true_peak_valid ? true_peak_color(loudness.true_peak_dbtp) : text_muted(),
        0.0f,
        "Maximum reconstructed true peak since this Rack helper started.");
}

inline void render_broadcast_master_console()
{
    const char* health = aggregate_health_text();
    const ImVec4 health_colour = aggregate_health_color();

    if (semibold_font)
        ImGui::PushFont(semibold_font);
    ImGui::TextColored(text_primary(), "SAFE VST3");
    if (semibold_font)
        ImGui::PopFont();

    const std::string status = std::string("● ") + health;
    const float status_width = ImGui::CalcTextSize(status.c_str()).x;
    ImGui::SameLine();
    const float right = ImGui::GetWindowContentRegionMax().x;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - status_width));
    ImGui::TextColored(health_colour, "%s", status.c_str());

    ImGui::TextDisabled("%u FX  ·  %u smp",
                        console_frame.slot_count,
                        console_frame.latency_samples);
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    ImGui::TextDisabled("LEVEL");
    render_compact_level_surface();

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    ImGui::TextDisabled("LOUDNESS");
    render_loudness_surface();

    ImGui::Dummy(ImVec2(0.0f, 7.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    if (semibold_font)
        ImGui::PushFont(semibold_font);
    ImGui::TextColored(accent(), "TO OBS");
    if (semibold_font)
        ImGui::PopFont();
}

} // namespace safevst3::rack::ui::p5

namespace ImGui {

inline void SafeVst3P5StyleColorsDark(ImGuiStyle* dst = nullptr)
{
    ImGui::SafeVst3P3StyleColorsDark(dst);
    ImGuiIO& io = ImGui::GetIO();
    safevst3::rack::ui::p5::loudness_font =
        safevst3::rack::ui::p1::add_windows_font(
            io, "C:\\Windows\\Fonts\\seguisb.ttf", 25.0f);
    if (!safevst3::rack::ui::p5::loudness_font)
        safevst3::rack::ui::p5::loudness_font = safevst3::rack::ui::p1::heading_font;
}

inline void SafeVst3P5TextUnformatted(const char* text, const char* text_end = nullptr)
{
    using namespace safevst3::rack::ui::p1;
    using namespace safevst3::rack::ui::p3;
    using namespace safevst3::rack::ui::p5;

    if (!text_end && text && std::strcmp(text, "OUTPUT TO OBS") == 0) {
        if (console_frame.split_started) {
            ImGui::EndChild();
            ImGui::SameLine(0.0f, kPremiumConsoleGap);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(17.0f, 14.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, surface_console());
            ImGui::BeginChild("rack-premium-console-broadcast",
                              ImVec2(0.0f, console_frame.pane_height),
                              ImGuiChildFlags_None);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);

            render_broadcast_master_console();
            ImGui::EndChild();
            console_frame.split_started = false;
        } else {
            ImGui::Spacing();
            draw_section_label("OUTPUT", "TO OBS");
        }
        return;
    }

    ImGui::SafeVst3P4TextUnformatted(text, text_end);
}

} // namespace ImGui

#define StyleColorsDark SafeVst3P5StyleColorsDark
#define TextUnformatted SafeVst3P5TextUnformatted

#endif // _WIN32
