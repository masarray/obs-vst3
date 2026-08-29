#ifdef _WIN32

#include "host/vst3_engine.hpp"

namespace safevst3 {

bool Vst3Engine::process(AudioSlot& slot) noexcept
{
    float* input[kMaxChannels] = {slot.input[0], slot.input[1]};
    float* output[kMaxChannels] = {slot.output[0], slot.output[1]};
    const ProcessBlockView block{
        input,
        output,
        slot.channels,
        slot.frames,
        slot.sequence,
    };
    return HostedPlugin::process(block);
}

} // namespace safevst3

#endif
