#include "common/state_snapshot.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

int main()
{
    using namespace safevst3;

    PluginStateSnapshot original{};
    original.component = {0x01, 0x02, 0x03, 0x7f, 0xff};
    original.controller = {0x10, 0x20, 0x30};

    std::vector<std::uint8_t> encoded;
    std::string error;
    assert(encode_state_blob(original, encoded, error));
    assert(error.empty());

    PluginStateSnapshot decoded{};
    assert(decode_state_blob(encoded, decoded, error));
    assert(decoded.component == original.component);
    assert(decoded.controller == original.controller);

    auto corrupted = encoded;
    corrupted.back() ^= 0x55u;
    assert(!decode_state_blob(corrupted, decoded, error));
    assert(error.find("checksum") != std::string::npos);

    const std::vector<std::uint8_t> truncated(encoded.begin(), encoded.begin() + 8);
    assert(!decode_state_blob(truncated, decoded, error));

    PluginStateSnapshot oversized{};
    oversized.component.resize(kMaxStateBytes + 1u);
    assert(!encode_state_blob(oversized, encoded, error));

    std::cout << "state snapshot envelope round-trip and corruption guards passed\n";
    return 0;
}
