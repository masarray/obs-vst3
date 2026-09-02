#ifdef _WIN32

#include "rack/rack_session_snapshot.hpp"

#include "host/hosted_plugin.hpp"

#include <windows.h>

#include <algorithm>
#include <limits>
#include <span>
#include <system_error>
#include <utility>

namespace safevst3::rack {
namespace {

constexpr std::size_t kRackSessionHeaderBytes = 48;
constexpr std::size_t kRackSessionSlotHeaderBytes = 32;
constexpr std::uint32_t kRackSessionBypassFlag = 1u << 0;

enum class FileReadStatus {
    Ok,
    Missing,
    Invalid,
    Error,
};

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
}

void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value)
{
    append_u32(out, static_cast<std::uint32_t>(value & 0xffffffffu));
    append_u32(out, static_cast<std::uint32_t>((value >> 32u) & 0xffffffffu));
}

bool read_u32(std::span<const std::uint8_t> bytes, std::size_t offset, std::uint32_t& value) noexcept
{
    if (offset > bytes.size() || bytes.size() - offset < sizeof(std::uint32_t))
        return false;
    value = static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
    return true;
}

bool read_u64(std::span<const std::uint8_t> bytes, std::size_t offset, std::uint64_t& value) noexcept
{
    std::uint32_t lo = 0;
    std::uint32_t hi = 0;
    if (!read_u32(bytes, offset, lo) || !read_u32(bytes, offset + sizeof(std::uint32_t), hi))
        return false;
    value = static_cast<std::uint64_t>(lo) | (static_cast<std::uint64_t>(hi) << 32u);
    return true;
}

bool rack_id_is_nonzero(const std::array<std::uint8_t, 16>& rack_id) noexcept
{
    return std::any_of(rack_id.begin(), rack_id.end(), [](std::uint8_t value) { return value != 0; });
}

bool health_is_valid(RackPersistedSlotHealth health) noexcept
{
    switch (health) {
    case RackPersistedSlotHealth::Ready:
    case RackPersistedSlotHealth::Missing:
    case RackPersistedSlotHealth::Failed:
    case RackPersistedSlotHealth::Suspect:
    case RackPersistedSlotHealth::Quarantined:
        return true;
    }
    return false;
}

bool slot_id_is_unique(const std::vector<RackSessionSlotSnapshot>& slots, RackSlotId id) noexcept
{
    return std::none_of(slots.begin(), slots.end(), [id](const RackSessionSlotSnapshot& slot) {
        return slot.slot_id == id;
    });
}

bool validate_slot_identity(const RackSessionSlotSnapshot& slot, std::string& error)
{
    if (slot.slot_id == 0) {
        error = "Rack Session Snapshot slot ID is invalid";
        return false;
    }
    if (slot.plugin_path.empty() || slot.plugin_path.size() > kRackSessionMaxPluginPathBytes ||
        slot.plugin_path.find('\0') != std::string::npos) {
        error = "Rack Session Snapshot plug-in path is invalid or oversized";
        return false;
    }
    if (slot.class_id.empty() || slot.class_id.size() > kRackSessionMaxClassIdBytes ||
        slot.class_id.find('\0') != std::string::npos) {
        error = "Rack Session Snapshot class identity is invalid or oversized";
        return false;
    }
    if (!health_is_valid(slot.health)) {
        error = "Rack Session Snapshot slot health is invalid";
        return false;
    }
    if (slot.state.total_bytes() > kMaxStateBytes) {
        error = "Rack Session Snapshot plug-in state exceeds the supported bound";
        return false;
    }
    return true;
}

std::filesystem::path suffixed_path(const std::filesystem::path& path, const wchar_t* suffix)
{
    std::filesystem::path::string_type native = path.native();
    native += suffix;
    return std::filesystem::path(std::move(native));
}

std::string win32_error(const char* operation)
{
    return std::string(operation) + " failed with Win32 error " + std::to_string(GetLastError());
}

bool write_bytes_flushed(const std::filesystem::path& path,
                         std::span<const std::uint8_t> bytes,
                         std::string& error)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = win32_error("CreateFileW(write Rack Session Snapshot)");
        return false;
    }

    bool ok = true;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t remaining = bytes.size() - offset;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, 1024u * 1024u));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset, chunk, &written, nullptr) || written != chunk) {
            error = win32_error("WriteFile(Rack Session Snapshot)");
            ok = false;
            break;
        }
        offset += written;
    }

    if (ok && !FlushFileBuffers(file)) {
        error = win32_error("FlushFileBuffers(Rack Session Snapshot)");
        ok = false;
    }
    CloseHandle(file);
    if (!ok)
        DeleteFileW(path.c_str());
    return ok;
}

FileReadStatus read_file_bytes(const std::filesystem::path& path,
                               std::vector<std::uint8_t>& bytes,
                               std::string& error)
{
    bytes.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)
            return FileReadStatus::Missing;
        error = win32_error("CreateFileW(read Rack Session Snapshot)");
        return FileReadStatus::Error;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        error = win32_error("GetFileSizeEx(Rack Session Snapshot)");
        CloseHandle(file);
        return FileReadStatus::Error;
    }
    if (size.QuadPart < 0 || static_cast<std::uint64_t>(size.QuadPart) > kRackSessionMaxSnapshotBytes) {
        error = "Rack Session Snapshot file size is invalid or oversized";
        CloseHandle(file);
        return FileReadStatus::Invalid;
    }

    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t remaining = bytes.size() - offset;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, 1024u * 1024u));
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + offset, chunk, &read, nullptr) || read != chunk) {
            error = win32_error("ReadFile(Rack Session Snapshot)");
            CloseHandle(file);
            bytes.clear();
            return FileReadStatus::Error;
        }
        offset += read;
    }
    CloseHandle(file);
    return FileReadStatus::Ok;
}

bool atomic_replace(const std::filesystem::path& source,
                    const std::filesystem::path& destination,
                    std::string& error)
{
    if (MoveFileExW(source.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    error = win32_error("MoveFileExW(Rack Session Snapshot)");
    return false;
}

bool read_and_decode(const std::filesystem::path& path,
                     RackSessionSnapshot& snapshot,
                     std::vector<std::uint8_t>* raw,
                     std::string& error,
                     FileReadStatus& status)
{
    std::vector<std::uint8_t> bytes;
    status = read_file_bytes(path, bytes, error);
    if (status != FileReadStatus::Ok)
        return false;
    if (!decode_rack_session_snapshot(bytes, snapshot, error)) {
        status = FileReadStatus::Invalid;
        return false;
    }
    if (raw)
        *raw = std::move(bytes);
    return true;
}

} // namespace

bool capture_rack_session_slot(HostedPlugin& plugin,
                               RackSlotId slot_id,
                               const std::string& plugin_path,
                               bool bypass,
                               RackPersistedSlotHealth health,
                               RackSessionSlotSnapshot& destination,
                               std::string& error)
{
    error.clear();
    RackSessionSlotSnapshot candidate{};
    candidate.slot_id = slot_id;
    candidate.plugin_path = plugin_path;
    candidate.class_id = plugin.loaded_class_id();
    candidate.bypass = bypass;
    candidate.health = health;
    if (!validate_slot_identity(candidate, error))
        return false;
    if (!plugin.capture_state(candidate.state, error))
        return false;
    if (candidate.state.total_bytes() > kMaxStateBytes) {
        error = "Rack Session Snapshot captured plug-in state exceeds the supported bound";
        return false;
    }
    destination = std::move(candidate);
    return true;
}

bool restore_rack_session_slot_state(HostedPlugin& plugin,
                                     const RackSessionSlotSnapshot& slot,
                                     std::string& error)
{
    error.clear();
    if (!validate_slot_identity(slot, error))
        return false;
    if (plugin.loaded_class_id() != slot.class_id) {
        error = "Rack Session Snapshot class identity does not match the opened plug-in";
        return false;
    }
    return plugin.restore_state(slot.state, error);
}

bool encode_rack_session_snapshot(const RackSessionSnapshot& snapshot,
                                  std::vector<std::uint8_t>& destination,
                                  std::string& error)
{
    error.clear();
    destination.clear();
    if (!rack_id_is_nonzero(snapshot.rack_id)) {
        error = "Rack Session Snapshot Rack ID is invalid";
        return false;
    }
    if (snapshot.generation == 0) {
        error = "Rack Session Snapshot generation must be nonzero";
        return false;
    }
    if (snapshot.slots.size() > kRackMaxSlots) {
        error = "Rack Session Snapshot exceeds the maximum slot count";
        return false;
    }

    std::vector<std::uint8_t> body;
    for (std::size_t index = 0; index < snapshot.slots.size(); ++index) {
        const RackSessionSlotSnapshot& slot = snapshot.slots[index];
        if (!validate_slot_identity(slot, error))
            return false;
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (snapshot.slots[prior].slot_id == slot.slot_id) {
                error = "Rack Session Snapshot contains duplicate stable slot IDs";
                return false;
            }
        }

        std::vector<std::uint8_t> state_blob;
        if (!encode_state_blob(slot.state, state_blob, error))
            return false;
        if (slot.plugin_path.size() > std::numeric_limits<std::uint32_t>::max() ||
            slot.class_id.size() > std::numeric_limits<std::uint32_t>::max() ||
            state_blob.size() > std::numeric_limits<std::uint32_t>::max()) {
            error = "Rack Session Snapshot slot payload length is unsupported";
            return false;
        }

        const std::size_t required = kRackSessionSlotHeaderBytes + slot.plugin_path.size() +
                                     slot.class_id.size() + state_blob.size();
        const std::size_t max_body_bytes = kRackSessionMaxSnapshotBytes - kRackSessionHeaderBytes;
        if (required > max_body_bytes || body.size() > max_body_bytes - required) {
            error = "Rack Session Snapshot exceeds the maximum encoded size";
            return false;
        }

        append_u64(body, slot.slot_id);
        append_u32(body, slot.bypass ? kRackSessionBypassFlag : 0u);
        append_u32(body, static_cast<std::uint32_t>(slot.health));
        append_u32(body, static_cast<std::uint32_t>(slot.plugin_path.size()));
        append_u32(body, static_cast<std::uint32_t>(slot.class_id.size()));
        append_u32(body, static_cast<std::uint32_t>(state_blob.size()));
        append_u32(body, 0u);
        body.insert(body.end(), slot.plugin_path.begin(), slot.plugin_path.end());
        body.insert(body.end(), slot.class_id.begin(), slot.class_id.end());
        body.insert(body.end(), state_blob.begin(), state_blob.end());
    }

    if (body.size() > std::numeric_limits<std::uint32_t>::max() ||
        body.size() > kRackSessionMaxSnapshotBytes - kRackSessionHeaderBytes) {
        error = "Rack Session Snapshot body exceeds the supported file size";
        return false;
    }

    std::vector<std::uint8_t> protected_payload;
    protected_payload.reserve(32u + body.size());
    protected_payload.insert(protected_payload.end(), snapshot.rack_id.begin(), snapshot.rack_id.end());
    append_u64(protected_payload, snapshot.generation);
    append_u32(protected_payload, static_cast<std::uint32_t>(snapshot.slots.size()));
    append_u32(protected_payload, 0u);
    protected_payload.insert(protected_payload.end(), body.begin(), body.end());

    destination.reserve(16u + protected_payload.size());
    append_u32(destination, kRackSessionMagic);
    append_u32(destination, kRackSessionFormatVersion);
    append_u32(destination, static_cast<std::uint32_t>(body.size()));
    append_u32(destination, state_detail::crc32(protected_payload));
    destination.insert(destination.end(), protected_payload.begin(), protected_payload.end());
    return true;
}

bool decode_rack_session_snapshot(const std::vector<std::uint8_t>& bytes,
                                  RackSessionSnapshot& destination,
                                  std::string& error)
{
    error.clear();
    if (bytes.size() < kRackSessionHeaderBytes || bytes.size() > kRackSessionMaxSnapshotBytes) {
        error = "Rack Session Snapshot file is truncated or oversized";
        return false;
    }

    const std::span<const std::uint8_t> all(bytes.data(), bytes.size());
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t body_size = 0;
    std::uint32_t expected_crc = 0;
    std::uint64_t generation = 0;
    std::uint32_t slot_count = 0;
    std::uint32_t header_reserved = 0;
    if (!read_u32(all, 0, magic) || !read_u32(all, 4, version) ||
        !read_u32(all, 8, body_size) || !read_u32(all, 12, expected_crc) ||
        !read_u64(all, 32, generation) || !read_u32(all, 40, slot_count) ||
        !read_u32(all, 44, header_reserved)) {
        error = "Rack Session Snapshot header is invalid";
        return false;
    }
    if (magic != kRackSessionMagic || version != kRackSessionFormatVersion) {
        error = "Rack Session Snapshot format is unsupported";
        return false;
    }
    if (generation == 0 || slot_count > kRackMaxSlots || header_reserved != 0 ||
        body_size != bytes.size() - kRackSessionHeaderBytes) {
        error = "Rack Session Snapshot header metadata is invalid";
        return false;
    }

    const auto checksum_payload = all.subspan(16);
    if (state_detail::crc32(checksum_payload) != expected_crc) {
        error = "Rack Session Snapshot checksum failed";
        return false;
    }

    RackSessionSnapshot candidate{};
    std::copy_n(bytes.begin() + 16, candidate.rack_id.size(), candidate.rack_id.begin());
    if (!rack_id_is_nonzero(candidate.rack_id)) {
        error = "Rack Session Snapshot Rack ID is invalid";
        return false;
    }
    candidate.generation = generation;

    const auto body = all.subspan(kRackSessionHeaderBytes, body_size);
    std::size_t offset = 0;
    candidate.slots.reserve(slot_count);
    for (std::uint32_t index = 0; index < slot_count; ++index) {
        if (offset > body.size() || body.size() - offset < kRackSessionSlotHeaderBytes) {
            error = "Rack Session Snapshot slot header is truncated";
            return false;
        }

        std::uint64_t slot_id = 0;
        std::uint32_t flags = 0;
        std::uint32_t health_value = 0;
        std::uint32_t path_size = 0;
        std::uint32_t class_size = 0;
        std::uint32_t state_size = 0;
        std::uint32_t slot_reserved = 0;
        if (!read_u64(body, offset, slot_id) ||
            !read_u32(body, offset + 8, flags) ||
            !read_u32(body, offset + 12, health_value) ||
            !read_u32(body, offset + 16, path_size) ||
            !read_u32(body, offset + 20, class_size) ||
            !read_u32(body, offset + 24, state_size) ||
            !read_u32(body, offset + 28, slot_reserved)) {
            error = "Rack Session Snapshot slot header is invalid";
            return false;
        }
        offset += kRackSessionSlotHeaderBytes;

        const auto health = static_cast<RackPersistedSlotHealth>(health_value);
        if (slot_id == 0 || !slot_id_is_unique(candidate.slots, slot_id) ||
            (flags & ~kRackSessionBypassFlag) != 0 || !health_is_valid(health) || slot_reserved != 0 ||
            path_size == 0 || path_size > kRackSessionMaxPluginPathBytes ||
            class_size == 0 || class_size > kRackSessionMaxClassIdBytes ||
            state_size < kStateBlobHeaderBytes ||
            state_size > kStateBlobHeaderBytes + kMaxStateBytes) {
            error = "Rack Session Snapshot slot metadata is invalid";
            return false;
        }

        const std::size_t payload_size = static_cast<std::size_t>(path_size) + class_size + state_size;
        if (offset > body.size() || payload_size > body.size() - offset) {
            error = "Rack Session Snapshot slot payload is truncated";
            return false;
        }

        RackSessionSlotSnapshot slot{};
        slot.slot_id = slot_id;
        slot.bypass = (flags & kRackSessionBypassFlag) != 0;
        slot.health = health;
        slot.plugin_path.assign(reinterpret_cast<const char*>(body.data() + offset), path_size);
        offset += path_size;
        slot.class_id.assign(reinterpret_cast<const char*>(body.data() + offset), class_size);
        offset += class_size;
        if (slot.plugin_path.find('\0') != std::string::npos ||
            slot.class_id.find('\0') != std::string::npos) {
            error = "Rack Session Snapshot plug-in identity contains embedded NUL data";
            return false;
        }

        PluginStateSnapshot state;
        if (!decode_state_blob(body.subspan(offset, state_size), state, error))
            return false;
        offset += state_size;
        slot.state = std::move(state);
        candidate.slots.push_back(std::move(slot));
    }

    if (offset != body.size()) {
        error = "Rack Session Snapshot has unexpected trailing data";
        return false;
    }

    destination = std::move(candidate);
    return true;
}

bool write_rack_session_snapshot_atomic(const std::filesystem::path& path,
                                        const RackSessionSnapshot& snapshot,
                                        std::string& error)
{
    error.clear();
    if (path.empty()) {
        error = "Rack Session Snapshot path is empty";
        return false;
    }

    std::vector<std::uint8_t> candidate_bytes;
    if (!encode_rack_session_snapshot(snapshot, candidate_bytes, error))
        return false;
    RackSessionSnapshot validated_candidate;
    if (!decode_rack_session_snapshot(candidate_bytes, validated_candidate, error))
        return false;

    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code fs_error;
        std::filesystem::create_directories(parent, fs_error);
        if (fs_error) {
            error = "Rack Session Snapshot directory creation failed: " + fs_error.message();
            return false;
        }
    }

    const auto temp_path = suffixed_path(path, L".tmp");
    const auto previous_path = suffixed_path(path, L".previous");
    const auto previous_temp_path = suffixed_path(path, L".previous.tmp");
    DeleteFileW(temp_path.c_str());
    DeleteFileW(previous_temp_path.c_str());

    RackSessionSnapshot current_snapshot;
    std::vector<std::uint8_t> current_bytes;
    std::string current_error;
    FileReadStatus current_status = FileReadStatus::Missing;
    const bool current_valid = read_and_decode(path, current_snapshot, &current_bytes,
                                               current_error, current_status);
    if (!current_valid && current_status == FileReadStatus::Error) {
        error = current_error;
        return false;
    }

    if (current_valid) {
        if (!write_bytes_flushed(previous_temp_path, current_bytes, error))
            return false;
        if (!atomic_replace(previous_temp_path, previous_path, error)) {
            DeleteFileW(previous_temp_path.c_str());
            return false;
        }
    }

    if (!write_bytes_flushed(temp_path, candidate_bytes, error))
        return false;

    RackSessionSnapshot temp_validation;
    std::vector<std::uint8_t> temp_bytes;
    FileReadStatus temp_status = FileReadStatus::Missing;
    if (!read_and_decode(temp_path, temp_validation, &temp_bytes, error, temp_status) ||
        temp_bytes != candidate_bytes) {
        if (error.empty())
            error = "Rack Session Snapshot temp read-back did not match the candidate bytes";
        DeleteFileW(temp_path.c_str());
        return false;
    }

    if (!atomic_replace(temp_path, path, error)) {
        DeleteFileW(temp_path.c_str());
        return false;
    }

    RackSessionSnapshot readback;
    std::vector<std::uint8_t> readback_bytes;
    FileReadStatus readback_status = FileReadStatus::Missing;
    if (!read_and_decode(path, readback, &readback_bytes, error, readback_status) ||
        readback_bytes != candidate_bytes) {
        if (error.empty())
            error = "Rack Session Snapshot atomic read-back did not match the candidate bytes";
        return false;
    }
    return true;
}

bool load_rack_session_snapshot_lkg(const std::filesystem::path& path,
                                    RackSessionSnapshot& destination,
                                    RackSessionLoadSource& source,
                                    std::string& error)
{
    error.clear();
    source = RackSessionLoadSource::None;

    RackSessionSnapshot current;
    FileReadStatus current_status = FileReadStatus::Missing;
    std::string current_error;
    if (read_and_decode(path, current, nullptr, current_error, current_status)) {
        destination = std::move(current);
        source = RackSessionLoadSource::Current;
        return true;
    }

    const auto previous_path = suffixed_path(path, L".previous");
    RackSessionSnapshot previous;
    FileReadStatus previous_status = FileReadStatus::Missing;
    std::string previous_error;
    if (read_and_decode(previous_path, previous, nullptr, previous_error, previous_status)) {
        destination = std::move(previous);
        source = RackSessionLoadSource::Previous;
        return true;
    }

    if (current_status == FileReadStatus::Missing && previous_status == FileReadStatus::Missing)
        error = "Rack Session Snapshot and previous LKG do not exist";
    else if (!current_error.empty() && !previous_error.empty())
        error = "Rack Session Snapshot current invalid/unreadable (" + current_error +
                "); previous LKG invalid/unreadable (" + previous_error + ")";
    else if (!current_error.empty())
        error = current_error;
    else
        error = previous_error;
    return false;
}

} // namespace safevst3::rack

#endif
