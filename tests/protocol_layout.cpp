#include "common/protocol.hpp"

#include <cassert>
#include <iostream>

int main()
{
    using namespace safevst3;
    static_assert(kSlotCount >= 4);
    static_assert(kMaxFrames >= 1024);
    static_assert(alignof(AudioSlot) >= 64);
    static_assert(alignof(SharedAudioRegion) >= 64);
    assert(sizeof(SharedAudioRegion) < 256 * 1024);
    SharedAudioRegion region{};
    assert(region.magic == kProtocolMagic);
    assert(region.version == kProtocolVersion);
    std::cout << "protocol v" << region.version << ", shared region " << sizeof(region) << " bytes\n";
    return 0;
}
