#pragma once

#ifdef _WIN32

#include "rack/rack_preset_ui_contract.hpp"
#include "rack/rack_slot_workflow.hpp"
#include "rack/rack_ui_contract.hpp"

#include <functional>
#include <memory>

namespace safevst3::rack::ui {

inline constexpr wchar_t kRackEditorWindowClassName[] = L"SafeVst3RackEditorWindow";

using RackUiCommandHandler = std::function<RackUiCommandAck(const RackUiCommand&)>;
using RackPresetUiCommandHandler = std::function<RackPresetUiAck(const RackPresetUiCommand&)>;

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

#endif
