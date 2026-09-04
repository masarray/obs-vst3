#pragma once

#ifdef _WIN32

#include "rack/rack_protocol.hpp"

#include <cstddef>
#include <memory>
#include <string>

namespace safevst3 {
class HostedPlugin;
}

namespace safevst3::rack::ui {

// Control-thread-owned floating vendor window manager. Identity is the stable
// Rack slot ID, never presentation order. No object from this class is exposed
// to the Rack DSP worker or OBS process.
class RackVendorEditorManager {
public:
    RackVendorEditorManager();
    ~RackVendorEditorManager();

    RackVendorEditorManager(const RackVendorEditorManager&) = delete;
    RackVendorEditorManager& operator=(const RackVendorEditorManager&) = delete;

    bool open(RackSlotId slot_id,
              HostedPlugin& plugin,
              const std::string& title,
              std::string& error) noexcept;
    void close(RackSlotId slot_id) noexcept;
    void close_all() noexcept;
    void pump_messages() noexcept;

    std::size_t open_count() const noexcept;
    bool created(RackSlotId slot_id) const noexcept;
    bool visible(RackSlotId slot_id) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace safevst3::rack::ui

#endif
