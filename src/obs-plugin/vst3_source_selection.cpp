#include "obs-plugin/vst3_source_selection.hpp"

namespace safevst3::obssource {

SourceSelection resolve_source_selection(const SourceSelectionInput& input)
{
    SourceSelection result;
    if (input.explicit_mode) {
        result.mode = *input.explicit_mode;
    } else {
        result.inferred_from_legacy = true;
        result.mode = !input.custom_path.empty() && input.installed_selection.empty()
                          ? SourceMode::Browse
                          : SourceMode::Installed;
    }

    result.show_installed_controls = result.mode == SourceMode::Installed;
    result.show_browse_controls = !result.show_installed_controls;

    if (result.mode == SourceMode::Browse) {
        result.path = input.custom_path;
        result.class_id = input.legacy_class_id;
        return result;
    }

    const auto tab = input.installed_selection.find('\t');
    if (tab == std::string::npos) {
        result.path = input.installed_selection;
        result.class_id = input.legacy_class_id;
    } else {
        result.path = input.installed_selection.substr(0, tab);
        result.class_id = input.installed_selection.substr(tab + 1);
    }
    return result;
}

bool clears_legacy_class_id_on_browse_change(SourceMode active_mode)
{
    return active_mode == SourceMode::Browse;
}

} // namespace safevst3::obssource
