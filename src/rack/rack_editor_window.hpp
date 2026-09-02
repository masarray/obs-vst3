#pragma once

#ifdef _WIN32

#include "rack/rack_ui_contract.hpp"

#include <functional>
#include <memory>

namespace safevst3::rack::ui {

inline constexpr wchar_t kRackEditorWindowClassName[] = L"SafeVst3RackEditorWindow";

using RackUiCommandHandler = std::function<RackUiCommandAck(const RackUiCommand&)>;

class RackEditorWindow {
public:
    explicit RackEditorWindow(RackUiCommandHandler command_handler = {});
    ~RackEditorWindow();

    RackEditorWindow(const RackEditorWindow&) = delete;
    RackEditorWindow& operator=(const RackEditorWindow&) = delete;

    bool publish_snapshot(const RackUiSnapshot& snapshot) noexcept;
    bool apply_ack(const RackUiCommandAck& ack) noexcept;

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
