#pragma once

#include "common/protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace safevst3 {

inline constexpr std::uint32_t kStateBlobMagic = 0x31545356u; // "VST1"
inline constexpr std::uint32_t kStateBlobVersion = 1;
inline constexpr std::size_t kStateBlobHeaderBytes = 5u * sizeof(std::uint32_t);

struct PluginStateSnapshot {
    std::vector<std::uint8_t> component;
    std::vector<std::uint8_t> controller;

    std::size_t total_bytes() const noexcept
    {
        return component.size() + controller.size();
    }
};

namespace state_detail {

inline void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
}

inline bool read_u32(std::span<const std::uint8_t> bytes, std::size_t offset, std::uint32_t& value) noexcept
{
    if (offset > bytes.size() || bytes.size() - offset < sizeof(std::uint32_t))
        return false;
    value = static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
    return true;
}

inline std::uint32_t crc32(std::span<const std::uint8_t> bytes) noexcept
{
    std::uint32_t crc = 0xffffffffu;
    for (std::uint8_t byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

} // namespace state_detail

inline bool encode_state_blob(const PluginStateSnapshot& snapshot,
                              std::vector<std::uint8_t>& out,
                              std::string& error)
{
    error.clear();
    out.clear();

    if (snapshot.component.size() > std::numeric_limits<std::uint32_t>::max() ||
        snapshot.controller.size() > std::numeric_limits<std::uint32_t>::max() ||
        snapshot.total_bytes() > kMaxStateBytes) {
        error = "VST3 state exceeds the supported snapshot size";
        return false;
    }

    out.reserve(kStateBlobHeaderBytes + snapshot.total_bytes());
    state_detail::append_u32(out, kStateBlobMagic);
    state_detail::append_u32(out, kStateBlobVersion);
    state_detail::append_u32(out, static_cast<std::uint32_t>(snapshot.component.size()));
    state_detail::append_u32(out, static_cast<std::uint32_t>(snapshot.controller.size()));

    std::vector<std::uint8_t> payload;
    payload.reserve(snapshot.total_bytes());
    payload.insert(payload.end(), snapshot.component.begin(), snapshot.component.end());
    payload.insert(payload.end(), snapshot.controller.begin(), snapshot.controller.end());
    state_detail::append_u32(out, state_detail::crc32(payload));
    out.insert(out.end(), payload.begin(), payload.end());
    return true;
}

inline bool decode_state_blob(std::span<const std::uint8_t> bytes,
                              PluginStateSnapshot& snapshot,
                              std::string& error)
{
    error.clear();
    snapshot = {};

    if (bytes.size() < kStateBlobHeaderBytes) {
        error = "VST3 state snapshot is truncated";
        return false;
    }

    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t component_size = 0;
    std::uint32_t controller_size = 0;
    std::uint32_t expected_crc = 0;
    if (!state_detail::read_u32(bytes, 0, magic) ||
        !state_detail::read_u32(bytes, 4, version) ||
        !state_detail::read_u32(bytes, 8, component_size) ||
        !state_detail::read_u32(bytes, 12, controller_size) ||
        !state_detail::read_u32(bytes, 16, expected_crc)) {
        error = "VST3 state snapshot header is invalid";
        return false;
    }

    if (magic != kStateBlobMagic || version != kStateBlobVersion) {
        error = "VST3 state snapshot format is unsupported";
        return false;
    }

    const std::size_t component_bytes = component_size;
    const std::size_t controller_bytes = controller_size;
    if (component_bytes > kMaxStateBytes || controller_bytes > kMaxStateBytes - component_bytes) {
        error = "VST3 state snapshot declares an oversized payload";
        return false;
    }

    const std::size_t payload_size = component_bytes + controller_bytes;
    if (bytes.size() != kStateBlobHeaderBytes + payload_size) {
        error = "VST3 state snapshot length does not match its header";
        return false;
    }

    const auto payload = bytes.subspan(kStateBlobHeaderBytes, payload_size);
    if (state_detail::crc32(payload) != expected_crc) {
        error = "VST3 state snapshot checksum failed";
        return false;
    }

    snapshot.component.assign(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(component_bytes));
    snapshot.controller.assign(payload.begin() + static_cast<std::ptrdiff_t>(component_bytes), payload.end());
    return true;
}

} // namespace safevst3
