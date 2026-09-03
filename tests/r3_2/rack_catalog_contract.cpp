#include "rack/rack_plugin_catalog.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;
using safevst3::rack::ui::RackPluginCatalog;

int main()
{
#ifdef _WIN32
    std::error_code ec;
    const fs::path root = fs::temp_directory_path(ec) /
        (L"obs-safe-vst3-r3-2-catalog-" + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    const fs::path cache = root / L"plugins.tsv";

    {
        std::ofstream out(cache, std::ios::binary | std::ios::trunc);
        out << "Alpha EQ\tC:/VST3/Alpha.vst3\tclass-alpha\n";
        out << "Beta Comp\tC:/VST3/Beta.vst3\tclass-beta\n";
    }

    RackPluginCatalog catalog;
    assert(catalog.load_cache(cache));
    const auto first = catalog.snapshot();
    assert(first.entry_count == 2);
    const auto first_generation = first.generation;
    const auto first_id = first.entries[0].entry_id;

    // A non-empty cache that is wholly malformed is corruption, not a valid
    // empty discovery result. Keep the previous immutable catalog and generation.
    {
        std::ofstream out(cache, std::ios::binary | std::ios::trunc);
        out << "this-is-not-a-tsv-row\n";
        out << "also\tmissing-path-separator\n";
    }
    assert(!catalog.load_cache(cache));
    assert(catalog.snapshot().generation == first_generation);
    assert(catalog.snapshot().entry_count == 2);
    assert(catalog.snapshot().entries[0].entry_id == first_id);

    // Invalid rows may be ignored when at least one bounded scanner row remains
    // valid; the next published snapshot contains only valid identities.
    {
        std::ofstream out(cache, std::ios::binary | std::ios::trunc);
        out << "broken-row\n";
        out << "Gamma Gate\tC:/VST3/Gamma.vst3\tclass-gamma\n";
    }
    assert(catalog.load_cache(cache));
    assert(catalog.snapshot().generation > first_generation);
    assert(catalog.snapshot().entry_count == 1);

    // A truly empty scanner cache is meaningful: all installed effects may have
    // been removed. It is therefore allowed to publish an empty catalog.
    {
        std::ofstream out(cache, std::ios::binary | std::ios::trunc);
    }
    const auto before_empty = catalog.snapshot().generation;
    assert(catalog.load_cache(cache));
    assert(catalog.snapshot().generation > before_empty);
    assert(catalog.snapshot().entry_count == 0);

    fs::remove_all(root, ec);
#endif
    return 0;
}
