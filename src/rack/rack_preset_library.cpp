#ifdef _WIN32

#include "rack/rack_preset_library.hpp"

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <span>
#include <system_error>
#include <utility>

namespace safevst3::rack {
namespace {

constexpr std::size_t kRackPresetHeaderBytes = 40;

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

bool read_u32(std::span<const std::uint8_t> bytes,
              std::size_t offset,
              std::uint32_t& value) noexcept
{
    if (offset > bytes.size() || bytes.size() - offset < sizeof(std::uint32_t))
        return false;
    value = static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
    return true;
}

bool id_is_valid(const RackPresetId& id) noexcept
{
    return std::any_of(id.begin(), id.end(), [](std::uint8_t byte) { return byte != 0; });
}

bool name_is_valid(const std::string& name) noexcept
{
    return !name.empty() && name.size() <= kRackPresetMaxNameBytes &&
           name.find('\0') == std::string::npos;
}

std::string preset_id_hex(const RackPresetId& id)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.resize(id.size() * 2);
    for (std::size_t i = 0; i < id.size(); ++i) {
        result[i * 2] = digits[(id[i] >> 4u) & 0x0fu];
        result[i * 2 + 1] = digits[id[i] & 0x0fu];
    }
    return result;
}

std::filesystem::path suffixed_path(const std::filesystem::path& path,
                                    const wchar_t* suffix)
{
    std::filesystem::path::string_type native = path.native();
    native += suffix;
    return std::filesystem::path(std::move(native));
}

std::string win32_error(const char* operation)
{
    return std::string(operation) + " failed with Win32 error " +
           std::to_string(GetLastError());
}

bool write_bytes_flushed(const std::filesystem::path& path,
                         std::span<const std::uint8_t> bytes,
                         std::string& error)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = win32_error("CreateFileW(write Rack Preset)");
        return false;
    }

    bool ok = true;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t remaining = bytes.size() - offset;
        const DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(remaining, 1024u * 1024u));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset, chunk, &written, nullptr) ||
            written != chunk) {
            error = win32_error("WriteFile(Rack Preset)");
            ok = false;
            break;
        }
        offset += written;
    }

    if (ok && !FlushFileBuffers(file)) {
        error = win32_error("FlushFileBuffers(Rack Preset)");
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
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND ||
            GetLastError() == ERROR_PATH_NOT_FOUND)
            return FileReadStatus::Missing;
        error = win32_error("CreateFileW(read Rack Preset)");
        return FileReadStatus::Error;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        error = win32_error("GetFileSizeEx(Rack Preset)");
        CloseHandle(file);
        return FileReadStatus::Error;
    }
    if (size.QuadPart < 0 ||
        static_cast<std::uint64_t>(size.QuadPart) > kRackPresetMaxBytes) {
        error = "Rack Preset file size is invalid or oversized";
        CloseHandle(file);
        return FileReadStatus::Invalid;
    }

    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t remaining = bytes.size() - offset;
        const DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(remaining, 1024u * 1024u));
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + offset, chunk, &read, nullptr) ||
            read != chunk) {
            error = win32_error("ReadFile(Rack Preset)");
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
    error = win32_error("MoveFileExW(Rack Preset)");
    return false;
}

bool read_expected_preset(const std::filesystem::path& path,
                          const RackPresetId& expected_id,
                          RackPreset& preset,
                          std::vector<std::uint8_t>* raw,
                          std::string& error,
                          FileReadStatus& status)
{
    std::vector<std::uint8_t> bytes;
    status = read_file_bytes(path, bytes, error);
    if (status != FileReadStatus::Ok)
        return false;

    RackPreset candidate;
    if (!decode_rack_preset(bytes, candidate, error) ||
        candidate.preset_id != expected_id) {
        if (error.empty())
            error = "Rack Preset identity does not match its library key";
        status = FileReadStatus::Invalid;
        return false;
    }

    preset = std::move(candidate);
    if (raw)
        *raw = std::move(bytes);
    return true;
}

bool validate_preset(const RackPreset& preset, std::string& error)
{
    if (!id_is_valid(preset.preset_id)) {
        error = "Rack Preset UUID is invalid";
        return false;
    }
    if (!name_is_valid(preset.name)) {
        error = "Rack Preset name is empty, invalid or oversized";
        return false;
    }
    if (preset.slots.size() > kRackMaxSlots) {
        error = "Rack Preset exceeds the maximum slot count";
        return false;
    }
    return true;
}

} // namespace

bool generate_rack_preset_id(RackPresetId& destination, std::string& error) noexcept
{
    error.clear();
    GUID guid{};
    const HRESULT result = CoCreateGuid(&guid);
    if (FAILED(result)) {
        error = "CoCreateGuid(Rack Preset UUID) failed with HRESULT " +
                std::to_string(static_cast<long>(result));
        return false;
    }

    RackPresetId candidate{};
    static_assert(sizeof(guid) == candidate.size());
    std::memcpy(candidate.data(), &guid, candidate.size());
    if (!id_is_valid(candidate)) {
        error = "Generated Rack Preset UUID is invalid";
        return false;
    }
    destination = candidate;
    return true;
}

std::filesystem::path rack_preset_library_path()
{
    wchar_t buffer[32768]{};
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer))
        return {};
    return std::filesystem::path(std::wstring(buffer, length)) /
           L"OBS Safe VST3 Host" / L"Rack Presets";
}

std::filesystem::path rack_preset_file_path(const std::filesystem::path& library,
                                            const RackPresetId& preset_id)
{
    if (library.empty() || !id_is_valid(preset_id))
        return {};
    return library / (preset_id_hex(preset_id) + ".rack-preset");
}

bool encode_rack_preset(const RackPreset& preset,
                        std::vector<std::uint8_t>& destination,
                        std::string& error)
{
    error.clear();
    destination.clear();
    if (!validate_preset(preset, error))
        return false;

    RackSessionSnapshot embedded{};
    embedded.rack_id = preset.preset_id;
    embedded.generation = 1;
    embedded.slots = preset.slots;

    std::vector<std::uint8_t> snapshot_bytes;
    if (!encode_rack_session_snapshot(embedded, snapshot_bytes, error))
        return false;
    if (preset.name.size() > std::numeric_limits<std::uint32_t>::max() ||
        snapshot_bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        error = "Rack Preset payload length is unsupported";
        return false;
    }

    const std::size_t total = kRackPresetHeaderBytes + preset.name.size() +
                              snapshot_bytes.size();
    if (total > kRackPresetMaxBytes) {
        error = "Rack Preset exceeds the maximum encoded size";
        return false;
    }

    std::vector<std::uint8_t> protected_payload;
    protected_payload.reserve(preset.preset_id.size() + preset.name.size() +
                              snapshot_bytes.size());
    protected_payload.insert(protected_payload.end(), preset.preset_id.begin(),
                             preset.preset_id.end());
    protected_payload.insert(protected_payload.end(), preset.name.begin(), preset.name.end());
    protected_payload.insert(protected_payload.end(), snapshot_bytes.begin(),
                             snapshot_bytes.end());

    destination.reserve(total);
    append_u32(destination, kRackPresetMagic);
    append_u32(destination, kRackPresetFormatVersion);
    append_u32(destination, static_cast<std::uint32_t>(preset.name.size()));
    append_u32(destination, static_cast<std::uint32_t>(snapshot_bytes.size()));
    append_u32(destination, state_detail::crc32(protected_payload));
    append_u32(destination, 0u);
    destination.insert(destination.end(), protected_payload.begin(), protected_payload.end());
    return true;
}

bool decode_rack_preset(const std::vector<std::uint8_t>& bytes,
                        RackPreset& destination,
                        std::string& error)
{
    error.clear();
    if (bytes.size() < kRackPresetHeaderBytes || bytes.size() > kRackPresetMaxBytes) {
        error = "Rack Preset file is truncated or oversized";
        return false;
    }

    const std::span<const std::uint8_t> all(bytes.data(), bytes.size());
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t name_size = 0;
    std::uint32_t snapshot_size = 0;
    std::uint32_t expected_crc = 0;
    std::uint32_t reserved = 0;
    if (!read_u32(all, 0, magic) || !read_u32(all, 4, version) ||
        !read_u32(all, 8, name_size) || !read_u32(all, 12, snapshot_size) ||
        !read_u32(all, 16, expected_crc) || !read_u32(all, 20, reserved)) {
        error = "Rack Preset header is invalid";
        return false;
    }
    if (magic != kRackPresetMagic || version != kRackPresetFormatVersion) {
        error = "Rack Preset format is unsupported";
        return false;
    }
    if (reserved != 0 || name_size == 0 || name_size > kRackPresetMaxNameBytes ||
        snapshot_size < 48u ||
        static_cast<std::size_t>(name_size) + snapshot_size !=
            bytes.size() - kRackPresetHeaderBytes) {
        error = "Rack Preset header metadata is invalid";
        return false;
    }

    const auto checksum_payload = all.subspan(24);
    if (state_detail::crc32(checksum_payload) != expected_crc) {
        error = "Rack Preset checksum failed";
        return false;
    }

    RackPreset candidate{};
    std::copy_n(bytes.begin() + 24, candidate.preset_id.size(),
                candidate.preset_id.begin());
    if (!id_is_valid(candidate.preset_id)) {
        error = "Rack Preset UUID is invalid";
        return false;
    }

    const std::size_t name_offset = kRackPresetHeaderBytes;
    candidate.name.assign(reinterpret_cast<const char*>(bytes.data() + name_offset),
                          name_size);
    if (!name_is_valid(candidate.name)) {
        error = "Rack Preset name is invalid";
        return false;
    }

    const std::size_t snapshot_offset = name_offset + name_size;
    std::vector<std::uint8_t> snapshot_bytes(
        bytes.begin() + static_cast<std::ptrdiff_t>(snapshot_offset), bytes.end());
    RackSessionSnapshot embedded;
    if (!decode_rack_session_snapshot(snapshot_bytes, embedded, error))
        return false;
    if (embedded.rack_id != candidate.preset_id || embedded.generation != 1) {
        error = "Rack Preset embedded topology identity is invalid";
        return false;
    }
    candidate.slots = std::move(embedded.slots);

    destination = std::move(candidate);
    return true;
}

bool write_rack_preset_atomic(const std::filesystem::path& library,
                              const RackPreset& preset,
                              std::string& error)
{
    error.clear();
    if (library.empty()) {
        error = "Rack Preset Library path is empty";
        return false;
    }

    std::vector<std::uint8_t> candidate_bytes;
    if (!encode_rack_preset(preset, candidate_bytes, error))
        return false;
    RackPreset validated_candidate;
    if (!decode_rack_preset(candidate_bytes, validated_candidate, error))
        return false;

    const auto path = rack_preset_file_path(library, preset.preset_id);
    if (path.empty()) {
        error = "Rack Preset path could not be derived from UUID";
        return false;
    }

    std::error_code fs_error;
    std::filesystem::create_directories(library, fs_error);
    if (fs_error) {
        error = "Rack Preset Library directory creation failed: " + fs_error.message();
        return false;
    }

    const auto temp_path = suffixed_path(path, L".tmp");
    const auto previous_path = suffixed_path(path, L".previous");
    const auto previous_temp_path = suffixed_path(path, L".previous.tmp");
    DeleteFileW(temp_path.c_str());
    DeleteFileW(previous_temp_path.c_str());

    RackPreset current;
    std::vector<std::uint8_t> current_bytes;
    std::string current_error;
    FileReadStatus current_status = FileReadStatus::Missing;
    const bool current_valid = read_expected_preset(
        path, preset.preset_id, current, &current_bytes, current_error, current_status);
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

    RackPreset temp_validation;
    std::vector<std::uint8_t> temp_bytes;
    FileReadStatus temp_status = FileReadStatus::Missing;
    if (!read_expected_preset(temp_path, preset.preset_id, temp_validation,
                              &temp_bytes, error, temp_status) ||
        temp_bytes != candidate_bytes) {
        if (error.empty())
            error = "Rack Preset temp read-back did not match candidate bytes";
        DeleteFileW(temp_path.c_str());
        return false;
    }

    if (!atomic_replace(temp_path, path, error)) {
        DeleteFileW(temp_path.c_str());
        return false;
    }

    RackPreset readback;
    std::vector<std::uint8_t> readback_bytes;
    FileReadStatus readback_status = FileReadStatus::Missing;
    if (!read_expected_preset(path, preset.preset_id, readback, &readback_bytes,
                              error, readback_status) ||
        readback_bytes != candidate_bytes) {
        if (error.empty())
            error = "Rack Preset atomic read-back did not match candidate bytes";
        return false;
    }
    return true;
}

bool load_rack_preset_lkg(const std::filesystem::path& library,
                          const RackPresetId& preset_id,
                          RackPreset& destination,
                          RackPresetLoadSource& source,
                          std::string& error)
{
    error.clear();
    source = RackPresetLoadSource::None;
    if (library.empty() || !id_is_valid(preset_id)) {
        error = "Rack Preset Library path or UUID is invalid";
        return false;
    }

    const auto path = rack_preset_file_path(library, preset_id);
    RackPreset current;
    FileReadStatus current_status = FileReadStatus::Missing;
    std::string current_error;
    if (read_expected_preset(path, preset_id, current, nullptr,
                             current_error, current_status)) {
        destination = std::move(current);
        source = RackPresetLoadSource::Current;
        return true;
    }

    const auto previous_path = suffixed_path(path, L".previous");
    RackPreset previous;
    FileReadStatus previous_status = FileReadStatus::Missing;
    std::string previous_error;
    if (read_expected_preset(previous_path, preset_id, previous, nullptr,
                             previous_error, previous_status)) {
        destination = std::move(previous);
        source = RackPresetLoadSource::Previous;
        return true;
    }

    if (current_status == FileReadStatus::Missing &&
        previous_status == FileReadStatus::Missing) {
        error = "Rack Preset and previous valid copy do not exist";
    } else if (!current_error.empty() && !previous_error.empty()) {
        error = "Rack Preset current invalid/unreadable (" + current_error +
                "); previous valid copy invalid/unreadable (" + previous_error + ")";
    } else if (!current_error.empty()) {
        error = current_error;
    } else {
        error = previous_error;
    }
    return false;
}

bool make_working_rack_from_preset(
    const RackPreset& preset,
    const std::array<std::uint8_t, 16>& destination_rack_id,
    std::uint64_t destination_generation,
    RackSessionSnapshot& destination,
    std::string& error)
{
    error.clear();
    std::vector<std::uint8_t> preset_validation;
    if (!encode_rack_preset(preset, preset_validation, error))
        return false;
    if (!id_is_valid(destination_rack_id) || destination_generation == 0) {
        error = "Destination Rack identity or generation is invalid";
        return false;
    }

    RackSessionSnapshot candidate{};
    candidate.rack_id = destination_rack_id;
    candidate.generation = destination_generation;
    candidate.slots = preset.slots;

    std::vector<std::uint8_t> working_validation;
    if (!encode_rack_session_snapshot(candidate, working_validation, error))
        return false;

    destination = std::move(candidate);
    return true;
}

} // namespace safevst3::rack

#endif
