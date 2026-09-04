#include "rack/rack_preset_management.hpp"

#include <windows.h>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace safevst3::rack;

namespace {

RackPresetId make_id(unsigned seed)
{
    RackPresetId id{};
    for (std::size_t i = 0; i < id.size(); ++i)
        id[i] = static_cast<std::uint8_t>(seed + i + 1u);
    return id;
}

std::filesystem::path make_temp_dir()
{
    wchar_t root[MAX_PATH]{};
    const DWORD n = GetTempPathW(MAX_PATH, root);
    assert(n != 0 && n < MAX_PATH);
    const auto dir = std::filesystem::path(root) /
                     (L"safevst3-r3-4-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    assert(!ec);
    return dir;
}

const RackPresetSummary* find_summary(const std::vector<RackPresetSummary>& entries,
                                      const RackPresetId& id)
{
    const auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& item) {
        return item.preset_id == id;
    });
    return it == entries.end() ? nullptr : &*it;
}

} // namespace

int main()
{
    const auto library = make_temp_dir();
    const RackPresetId a = make_id(10);
    const RackPresetId b = make_id(50);
    std::string error;

    RackPreset preset_a{};
    preset_a.preset_id = a;
    preset_a.name = "Broadcast Vocal";
    assert(write_rack_preset_atomic(library, preset_a, error));

    RackPreset preset_b{};
    preset_b.preset_id = b;
    preset_b.name = "Music Master";
    assert(write_rack_preset_atomic(library, preset_b, error));

    std::vector<RackPresetSummary> entries;
    assert(list_rack_presets(library, entries, error));
    assert(entries.size() == 2);
    assert(find_summary(entries, a));
    assert(find_summary(entries, b));

    // Rename is identity-preserving and content-preserving.
    assert(rename_rack_preset_atomic(library, a, "Broadcast Vocal v2", error));
    RackPreset loaded{};
    RackPresetLoadSource source = RackPresetLoadSource::None;
    assert(load_rack_preset_lkg(library, a, loaded, source, error));
    assert(loaded.preset_id == a);
    assert(loaded.name == "Broadcast Vocal v2");

    // A corrupt current artifact still lists through its previous valid LKG.
    const auto current = rack_preset_file_path(library, a);
    {
        std::ofstream bad(current, std::ios::binary | std::ios::trunc);
        bad << "bad";
    }
    entries.clear();
    assert(list_rack_presets(library, entries, error));
    const RackPresetSummary* recovered = find_summary(entries, a);
    assert(recovered);
    assert(recovered->name == "Broadcast Vocal");

    // Delete is scoped to the selected preset ID, including its current/LKG
    // siblings, and does not disturb another preset.
    assert(delete_rack_preset(library, a, error));
    entries.clear();
    assert(list_rack_presets(library, entries, error));
    assert(!find_summary(entries, a));
    assert(find_summary(entries, b));

    std::error_code ec;
    std::filesystem::remove_all(library, ec);
    return 0;
}
