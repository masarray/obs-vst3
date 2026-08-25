#pragma once

#include <optional>
#include <string>

namespace safevst3::obssource {

enum class SourceMode {
    Installed,
    Browse,
};

struct SourceSelectionInput {
    std::optional<SourceMode> explicit_mode;
    std::string installed_selection;
    std::string custom_path;
    std::string legacy_class_id;
};

struct SourceSelection {
    SourceMode mode = SourceMode::Installed;
    bool inferred_from_legacy = false;
    bool show_installed_controls = true;
    bool show_browse_controls = false;
    std::string path;
    std::string class_id;
};

SourceSelection resolve_source_selection(const SourceSelectionInput& input);
bool clears_legacy_class_id_on_browse_change(SourceMode active_mode);

} // namespace safevst3::obssource
