#pragma once

#include "rack/rack_ui_contract.hpp"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace safevst3::rack::ui {

inline constexpr std::uint32_t kRackCatalogSnapshotVersion = 1;
inline constexpr std::uint32_t kRackCatalogMaxEntries = 256;
inline constexpr std::size_t kRackCatalogNameBytes = 96;
inline constexpr std::size_t kRackCatalogVendorBytes = 64;
inline constexpr std::size_t kRackCatalogCategoryBytes = 64;

using RackCatalogEntryId = std::uint64_t;

struct PluginCatalogEntrySnapshot {
    RackCatalogEntryId entry_id = 0;
    std::array<char, kRackCatalogNameBytes> name{};
    std::array<char, kRackCatalogVendorBytes> vendor{};
    std::array<char, kRackCatalogCategoryBytes> category{};
};

struct PluginCatalogSnapshot {
    std::uint32_t version = kRackCatalogSnapshotVersion;
    std::uint64_t generation = 0;
    std::uint32_t entry_count = 0;
    bool scanning = false;
    std::array<PluginCatalogEntrySnapshot, kRackCatalogMaxEntries> entries{};
};

inline bool bounded_text_terminated(const char* data, std::size_t capacity) noexcept
{
    if (!data || capacity == 0)
        return false;
    for (std::size_t i = 0; i < capacity; ++i) {
        if (data[i] == '\0')
            return true;
    }
    return false;
}

inline bool bounded_text_nonempty(const char* data, std::size_t capacity) noexcept
{
    return data && capacity != 0 && data[0] != '\0' &&
           bounded_text_terminated(data, capacity);
}

inline bool validate_plugin_catalog_snapshot(const PluginCatalogSnapshot& snapshot) noexcept
{
    if (snapshot.version != kRackCatalogSnapshotVersion || snapshot.generation == 0 ||
        snapshot.entry_count > kRackCatalogMaxEntries)
        return false;

    for (std::uint32_t i = 0; i < snapshot.entry_count; ++i) {
        const auto& entry = snapshot.entries[i];
        if (entry.entry_id == 0 ||
            !bounded_text_nonempty(entry.name.data(), entry.name.size()) ||
            !bounded_text_terminated(entry.vendor.data(), entry.vendor.size()) ||
            !bounded_text_terminated(entry.category.data(), entry.category.size()))
            return false;
        for (std::uint32_t prior = 0; prior < i; ++prior) {
            if (snapshot.entries[prior].entry_id == entry.entry_id)
                return false;
        }
    }
    return true;
}

inline char ascii_lower(char value) noexcept
{
    const auto byte = static_cast<unsigned char>(value);
    if (byte >= static_cast<unsigned char>('A') && byte <= static_cast<unsigned char>('Z'))
        return static_cast<char>(byte + ('a' - 'A'));
    return value;
}

inline bool bounded_contains_ascii_case_insensitive(const char* data, std::size_t capacity,
                                                    std::string_view needle) noexcept
{
    if (needle.empty())
        return true;
    if (!data || capacity == 0)
        return false;

    std::size_t length = 0;
    while (length < capacity && data[length] != '\0')
        ++length;
    if (length == capacity || needle.size() > length)
        return false;

    for (std::size_t start = 0; start + needle.size() <= length; ++start) {
        bool match = true;
        for (std::size_t i = 0; i < needle.size(); ++i) {
            if (ascii_lower(data[start + i]) != ascii_lower(needle[i])) {
                match = false;
                break;
            }
        }
        if (match)
            return true;
    }
    return false;
}

inline std::uint32_t filter_plugin_catalog(
    const PluginCatalogSnapshot& snapshot, std::string_view query,
    std::array<std::uint32_t, kRackCatalogMaxEntries>& output_indices) noexcept
{
    output_indices.fill(0);
    if (!validate_plugin_catalog_snapshot(snapshot))
        return 0;

    std::uint32_t count = 0;
    for (std::uint32_t i = 0; i < snapshot.entry_count; ++i) {
        const auto& entry = snapshot.entries[i];
        if (bounded_contains_ascii_case_insensitive(entry.name.data(), entry.name.size(), query) ||
            bounded_contains_ascii_case_insensitive(entry.vendor.data(), entry.vendor.size(), query) ||
            bounded_contains_ascii_case_insensitive(entry.category.data(), entry.category.size(), query))
            output_indices[count++] = i;
    }
    return count;
}

class RackUiCommandReplayGuard {
public:
    bool lookup(const RackUiCommand& command, RackUiCommandAck& ack) const noexcept
    {
        if (command.command_id == 0)
            return false;
        for (const auto& entry : entries_) {
            if (entry.command_id == command.command_id && entry.command_id != 0) {
                ack = entry.ack;
                return true;
            }
        }
        return false;
    }

    void remember(const RackUiCommand& command, const RackUiCommandAck& ack) noexcept
    {
        if (command.command_id == 0 || ack.command_id != command.command_id ||
            ack.result == RackUiCommandResult::Idle)
            return;

        for (auto& entry : entries_) {
            if (entry.command_id == command.command_id) {
                entry.ack = ack;
                return;
            }
        }

        entries_[next_] = {command.command_id, ack};
        next_ = (next_ + 1) % entries_.size();
    }

private:
    struct Entry {
        RackUiCommandId command_id = 0;
        RackUiCommandAck ack{};
    };

    // R3-2 has one pending mutating UI command at a time. A small bounded replay
    // window is sufficient to make an ambiguous duplicate request idempotent
    // without allowing an unbounded command history to accumulate.
    std::array<Entry, 32> entries_{};
    std::size_t next_ = 0;
};

} // namespace safevst3::rack::ui
