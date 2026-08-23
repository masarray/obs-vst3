#include "common/protocol.hpp"

#include <cassert>
#include <iostream>

int main()
{
    using namespace safevst3;
    static_assert(kProtocolVersion == 4);
    static_assert(kSlotCount >= 4);
    static_assert(kMaxFrames >= 1024);
    static_assert(kMaxParameters >= 128);
    static_assert(kPluginNameBytes >= 64);
    static_assert(kMaxStateBytes >= 8 * 1024 * 1024);
    static_assert(alignof(ParameterDescriptor) >= 64);
    static_assert(alignof(AudioSlot) >= 64);
    static_assert(alignof(SharedAudioRegion) >= 64);
    static_assert(alignof(StateTransferRegion) >= 64);
    assert(sizeof(SharedAudioRegion) < 256 * 1024);
    assert(sizeof(StateTransferRegion) >= kMaxStateBytes);
    SharedAudioRegion region{};
    assert(region.magic == kProtocolMagic);
    assert(region.version == kProtocolVersion);
    assert(region.parameter_count == 0);
    assert(region.parameter_total_count == 0);
    assert(region.latency_samples == 0);
    assert(region.plugin_name[0] == '\0');
    assert(region.editor_command == static_cast<long>(EditorCommand::None));
    assert(region.editor_status == static_cast<long>(EditorStatus::Unknown));
    assert(region.editor_request_generation == 0);
    assert(region.editor_applied_generation == 0);
    assert(region.state_command == static_cast<long>(StateCommand::None));
    assert(region.state_status == static_cast<long>(StateStatus::Idle));
    assert(region.state_request_generation == 0);
    assert(region.state_applied_generation == 0);
    assert(region.state_dirty_generation == 0);
    std::cout << "protocol v" << region.version << ", audio/control region " << sizeof(region)
              << " bytes, state transfer capacity " << kMaxStateBytes << " bytes\n";
    return 0;
}
