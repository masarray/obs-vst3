#pragma once

#ifdef _WIN32

#include "rack/rack_preset_ui_contract.hpp"
#include "rack/rack_slot_workflow.hpp"
#include "rack/rack_ui_contract.hpp"

#include <cstdint>
#include <functional>
#include <memory>

namespace safevst3::rack::ui {

inline constexpr wchar_t kRackEditorWindowClassName[] = L"SafeVst3RackEditorWindow";

using RackUiCommandHandler = std::function<RackUiCommandAck(const RackUiCommand&)>;
using RackPresetUiCommandHandler = std::function<RackPresetUiAck(const RackPresetUiCommand&)>;

// Separate from RackUiSnapshot on purpose: topology/state snapshots advance on
// command generations, while metering advances every audio block. Keeping this
// stream independent avoids manufacturing Rack generations just to animate UI.
struct RackUiMeterSnapshot {
    std::uint64_t sequence = 0;
    float input_peak_linear = 0.0f;
    float output_peak_linear = 0.0f;
    bool valid = false;
};

class RackEditorWindow {
public:
    explicit RackEditorWindow(RackUiCommandHandler command_handler = {},
                              RackPresetUiCommandHandler preset_command_handler = {});
    ~RackEditorWindow();

    RackEditorWindow(const RackEditorWindow&) = delete;
    RackEditorWindow& operator=(const RackEditorWindow&) = delete;

    bool publish_snapshot(const RackUiSnapshot& snapshot) noexcept;
    bool publish_catalog(const PluginCatalogSnapshot& snapshot) noexcept;
    bool publish_presets(const RackPresetUiSnapshot& snapshot) noexcept;
    bool apply_ack(const RackUiCommandAck& ack) noexcept;
    bool apply_preset_ack(const RackPresetUiAck& ack) noexcept;

    // DSP -> UI telemetry seam. Implementation is lock-free/atomic only; it
    // must never acquire the Rack model mutex or block realtime progress.
    void publish_meter(const RackUiMeterSnapshot& snapshot) noexcept;

    // Creates the helper-owned editor on demand or foregrounds the one existing
    // editor window. Construction of RackEditorWindow itself never opens UI.
    bool open_or_foreground() noexcept;

    // UI/rendering teardown only. Rack DSP/runtime ownership remains external.
    void shutdown() noexcept;
    bool visible() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace safevst3::rack::ui

// A skin may observe the current meter snapshot without coupling the qualified
// Rack Editor source to a particular product presentation. Default is no-op.
#ifndef SAFEVST3_RACK_UI_OBSERVE_METER
#define SAFEVST3_RACK_UI_OBSERVE_METER(snapshot) ((void)0)
#endif

#endif
