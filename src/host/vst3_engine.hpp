#pragma once

#ifdef _WIN32

#include "common/protocol.hpp"
#include "host/hosted_plugin.hpp"

namespace safevst3 {

// Single-transport compatibility adapter. All VST3 lifecycle/state/process
// ownership lives in HostedPlugin; this class only maps AudioSlot buffers into
// the protocol-neutral ProcessBlockView seam.
class Vst3Engine final : public HostedPlugin {
public:
    Vst3Engine() = default;
    ~Vst3Engine() = default;

    Vst3Engine(const Vst3Engine&) = delete;
    Vst3Engine& operator=(const Vst3Engine&) = delete;

    using HostedPlugin::process;
    bool process(AudioSlot& slot) noexcept;
};

} // namespace safevst3

#endif
