#include "common/state_snapshot.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, std::string_view message)
{
    if (condition)
        return;
    std::cerr << "state-snapshot-test failed: " << message << '\n';
    std::exit(1);
}

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
{
    require(offset + 4 <= bytes.size(), "test header write is in range");
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

} // namespace

int main()
{
    using namespace safevst3;

    PluginStateSnapshot original{};
    original.component = {0x01, 0x02, 0x03, 0x7f, 0xff};
    original.controller = {0x10, 0x20, 0x30};

    std::vector<std::uint8_t> encoded;
    std::string error;
    const bool encoded_ok = encode_state_blob(original, encoded, error);
    require(encoded_ok, "valid state encodes");
    require(error.empty(), "valid encode has no error");
    require(encoded.size() == kStateBlobHeaderBytes + original.total_bytes(), "encoded length is exact");

    PluginStateSnapshot decoded{};
    const bool decoded_ok = decode_state_blob(encoded, decoded, error);
    require(decoded_ok, "valid state decodes");
    require(decoded.component == original.component, "component state round-trips");
    require(decoded.controller == original.controller, "controller state round-trips");

    auto corrupted = encoded;
    corrupted.back() ^= 0x55u;
    require(!decode_state_blob(corrupted, decoded, error), "checksum corruption is rejected");
    require(error.find("checksum") != std::string::npos, "checksum failure is diagnosed");

    const std::vector<std::uint8_t> truncated(encoded.begin(), encoded.begin() + 8);
    require(!decode_state_blob(truncated, decoded, error), "truncated header is rejected");

    auto unsupported = encoded;
    write_u32(unsupported, 4, kStateBlobVersion + 1u);
    require(!decode_state_blob(unsupported, decoded, error), "unsupported version is rejected");
    require(error.find("unsupported") != std::string::npos, "unsupported version is diagnosed");

    auto wrong_length = encoded;
    wrong_length.pop_back();
    require(!decode_state_blob(wrong_length, decoded, error), "declared length mismatch is rejected");
    require(error.find("length") != std::string::npos, "length mismatch is diagnosed");

    auto declared_oversize = encoded;
    write_u32(declared_oversize, 8, static_cast<std::uint32_t>(kMaxStateBytes + 1u));
    require(!decode_state_blob(declared_oversize, decoded, error), "declared oversized payload is rejected");
    require(error.find("oversized") != std::string::npos, "oversized declaration is diagnosed");

    PluginStateSnapshot oversized{};
    oversized.component.resize(kMaxStateBytes + 1u);
    require(!encode_state_blob(oversized, encoded, error), "oversized state is rejected before serialization");

    PluginStateSnapshot empty{};
    require(encode_state_blob(empty, encoded, error), "empty-but-valid VST3 state encodes");
    require(decode_state_blob(encoded, decoded, error), "empty-but-valid VST3 state decodes");
    require(decoded.component.empty() && decoded.controller.empty(), "empty state round-trips");

    std::cout << "state snapshot envelope round-trip and corruption guards passed\n";
    return 0;
}
