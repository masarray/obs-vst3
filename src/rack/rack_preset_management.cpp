#ifdef _WIN32

#include "rack/rack_preset_management.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <system_error>

namespace safevst3::rack {
namespace {

int hex_value(wchar_t value) noexcept
{
    if (value >= L'0' && value <= L'9')
        return static_cast<int>(value - L'0');
    if (value >= L'a' && value <= L'f')
        return static_cast<int>(value - L'a') + 10;
    if (value >= L'A' && value <= L'F')
        return static_cast<int>(value - L'A') + 10;
    return -1;
}

bool parse_preset_id_stem(const std::filesystem::path& path,
                          RackPresetId& destination) noexcept
{
    const std::wstring stem = path.stem().wstring();
    if (stem.size() != destination.size() * 2)
        return false;

    RackPresetId candidate{};
    for (std::size_t index = 0; index < candidate.size(); ++index) {
        const int high = hex_value(stem[index * 2]);
        const int low = hex_value(stem[index * 2 + 1]);
        if (high < 0 || low < 0)
            return false;
        candidate[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    if (std::none_of(candidate.begin(), candidate.end(),
                     [](std::uint8_t byte) { return byte != 0; }))
        return false;
    destination = candidate;
    return true;
}

std::filesystem::path suffixed_path(const std::filesystem::path& path,
                                    const wchar_t* suffix)
{
    std::filesystem::path::string_type native = path.native();
    native += suffix;
    return std::filesystem::path(std::move(native));
}

std::string folded_ascii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

} // namespace

bool list_rack_presets(const std::filesystem::path& library,
                       std::vector<RackPresetSummary>& destination,
                       std::string& error)
{
    destination.clear();
    error.clear();
    if (library.empty()) {
        error = "Rack Preset Library path is empty";
        return false;
    }

    std::error_code ec;
    const bool exists = std::filesystem::exists(library, ec);
    if (ec) {
        error = "Rack Preset Library existence check failed: " + ec.message();
        return false;
    }
    if (!exists)
        return true;
    if (!std::filesystem::is_directory(library, ec) || ec) {
        error = ec ? "Rack Preset Library directory check failed: " + ec.message()
                   : "Rack Preset Library path is not a directory";
        return false;
    }

    std::filesystem::directory_iterator iterator(library, ec);
    const std::filesystem::directory_iterator end;
    while (!ec && iterator != end) {
        const auto& entry = *iterator;
        std::error_code item_error;
        const bool regular = entry.is_regular_file(item_error);
        if (!item_error && regular && entry.path().extension() == L".rack-preset") {
            RackPresetId preset_id{};
            if (parse_preset_id_stem(entry.path(), preset_id)) {
                RackPreset preset{};
                RackPresetLoadSource source = RackPresetLoadSource::None;
                std::string load_error;
                if (load_rack_preset_lkg(library, preset_id, preset, source, load_error)) {
                    if (destination.size() >= kRackPresetLibraryMaxEntries) {
                        error = "Rack Preset Library exceeds the supported preset count";
                        destination.clear();
                        return false;
                    }
                    destination.push_back({preset.preset_id, preset.name});
                }
            }
        }
        iterator.increment(ec);
    }
    if (ec) {
        error = "Rack Preset Library enumeration failed: " + ec.message();
        destination.clear();
        return false;
    }

    std::sort(destination.begin(), destination.end(), [](const auto& left, const auto& right) {
        const std::string left_name = folded_ascii(left.name);
        const std::string right_name = folded_ascii(right.name);
        if (left_name != right_name)
            return left_name < right_name;
        return left.preset_id < right.preset_id;
    });
    return true;
}

bool rename_rack_preset_atomic(const std::filesystem::path& library,
                               const RackPresetId& preset_id,
                               const std::string& new_name,
                               std::string& error)
{
    error.clear();
    RackPreset preset{};
    RackPresetLoadSource source = RackPresetLoadSource::None;
    if (!load_rack_preset_lkg(library, preset_id, preset, source, error))
        return false;
    preset.name = new_name;
    return write_rack_preset_atomic(library, preset, error);
}

bool delete_rack_preset(const std::filesystem::path& library,
                        const RackPresetId& preset_id,
                        std::string& error)
{
    error.clear();
    const std::filesystem::path current = rack_preset_file_path(library, preset_id);
    if (current.empty()) {
        error = "Rack Preset path could not be derived from UUID";
        return false;
    }

    const std::array<std::filesystem::path, 4> paths = {
        current,
        suffixed_path(current, L".previous"),
        suffixed_path(current, L".tmp"),
        suffixed_path(current, L".previous.tmp"),
    };

    bool removed_any = false;
    for (const auto& path : paths) {
        std::error_code ec;
        const bool removed = std::filesystem::remove(path, ec);
        if (ec) {
            error = "Rack Preset delete failed: " + ec.message();
            return false;
        }
        removed_any = removed_any || removed;
    }
    if (!removed_any) {
        error = "Rack Preset does not exist";
        return false;
    }
    return true;
}

} // namespace safevst3::rack

#endif
