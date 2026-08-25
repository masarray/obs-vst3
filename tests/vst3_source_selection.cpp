#include "obs-plugin/vst3_source_selection.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "vst3-source-selection-test failed: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main()
{
    using namespace safevst3::obssource;

    const SourceSelection installed = resolve_source_selection({
        SourceMode::Installed,
        "C:\\VST3\\InstalledEffect.vst3\tAABBCCDDEEFF00112233445566778899",
        R"(D:\Custom\ManualEffect.vst3)",
        "FFEEDDCCBBAA99887766554433221100",
    });
    require(installed.show_installed_controls && !installed.show_browse_controls,
            "Installed mode must expose only the installed selector and Rescan");
    require(installed.path == R"(C:\VST3\InstalledEffect.vst3)" &&
                installed.class_id == "AABBCCDDEEFF00112233445566778899",
            "Installed mode must ignore the retained manual Browse value");

    const SourceSelection browsed = resolve_source_selection({
        SourceMode::Browse,
        "C:\\VST3\\InstalledEffect.vst3\tAABBCCDDEEFF00112233445566778899",
        R"(D:\Custom\ManualEffect.vst3)",
        "",
    });
    require(!browsed.show_installed_controls && browsed.show_browse_controls,
            "Browse mode must expose only the manual Browse control");
    require(browsed.path == R"(D:\Custom\ManualEffect.vst3)" && browsed.class_id.empty(),
            "Browse mode must ignore the retained installed selection");

    const SourceSelection migrated = resolve_source_selection({
        std::nullopt,
        "",
        R"(C:\VST3\LegacyEffect.vst3)",
        "00112233445566778899AABBCCDDEEFF",
    });
    require(migrated.mode == SourceMode::Browse,
            "a legacy custom-only scene must migrate to Browse mode");
    require(migrated.inferred_from_legacy,
            "legacy migration must be exposed so the adapter can persist the selector value");
    require(!migrated.show_installed_controls && migrated.show_browse_controls,
            "Browse migration must expose only the manual Browse controls");
    require(migrated.path == R"(C:\VST3\LegacyEffect.vst3)" &&
                migrated.class_id == "00112233445566778899AABBCCDDEEFF",
            "legacy Browse migration must preserve its existing identity");

    const SourceSelection migrated_installed = resolve_source_selection({
        std::nullopt,
        "C:\\VST3\\LegacyInstalled.vst3\tAABBCCDDEEFF00112233445566778899",
        R"(D:\Retained\ManualEffect.vst3)",
        "FFEEDDCCBBAA99887766554433221100",
    });
    require(migrated_installed.mode == SourceMode::Installed &&
                migrated_installed.inferred_from_legacy,
            "a legacy scene with an installed selection must migrate to Installed mode");
    require(migrated_installed.path == R"(C:\VST3\LegacyInstalled.vst3)" &&
                migrated_installed.class_id == "AABBCCDDEEFF00112233445566778899",
            "legacy Installed migration must ignore a retained manual path");

    const SourceSelection legacy_path_only = resolve_source_selection({
        SourceMode::Installed,
        R"(C:\VST3\PathOnly.vst3)",
        "",
        "00112233445566778899AABBCCDDEEFF",
    });
    require(legacy_path_only.class_id == "00112233445566778899AABBCCDDEEFF",
            "a legacy installed path without a class field must inherit its saved ClassID");

    const SourceSelection explicit_empty_class = resolve_source_selection({
        SourceMode::Installed,
        "C:\\VST3\\FirstAudioEffect.vst3\t",
        "",
        "00112233445566778899AABBCCDDEEFF",
    });
    require(explicit_empty_class.class_id.empty(),
            "an explicit empty scanner ClassID must not inherit a stale legacy ClassID");

    require(clears_legacy_class_id_on_browse_change(SourceMode::Browse),
            "an explicit new Browse path must not reuse a legacy bundle ClassID");
    require(!clears_legacy_class_id_on_browse_change(SourceMode::Installed),
            "an inactive Browse field must not mutate the active installed identity");

    std::cout << "legacy VST3 source-mode migration behavior passed\n";
    return 0;
}
