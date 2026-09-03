#pragma once

#ifdef _WIN32

#include "rack/rack_slot_workflow.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>

namespace safevst3::rack::ui {

struct RackPluginCatalogRecord {
    RackCatalogEntryId entry_id = 0;
    std::string name;
    std::string path;
    std::string class_id;
    std::string vendor;
    std::string category;
};

class RackPluginCatalog {
public:
    RackPluginCatalog() noexcept;

    const PluginCatalogSnapshot& snapshot() const noexcept { return snapshot_; }
    const RackPluginCatalogRecord* resolve(std::uint64_t generation,
                                           RackCatalogEntryId entry_id) const noexcept;

    // Candidate parsing is all-or-nothing. A malformed/unreadable refresh never
    // destroys the previous valid catalog used by an open Rack Editor.
    bool load_cache(const std::filesystem::path& cache_path) noexcept;
    void set_scanning(bool scanning) noexcept;

private:
    std::array<RackPluginCatalogRecord, kRackCatalogMaxEntries> records_{};
    PluginCatalogSnapshot snapshot_{};
};

std::filesystem::path rack_catalog_cache_path();
std::filesystem::path rack_scanner_path();
bool run_rack_scanner(const std::filesystem::path& scanner,
                      const std::filesystem::path& cache,
                      std::stop_token stop = {}) noexcept;

} // namespace safevst3::rack::ui

#endif
