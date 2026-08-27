#pragma once

#include <obs-module.h>

#include <cstring>
#include <string>

// S1.11 keeps the Properties UI on libobs' public property API. OBS 32.x does
// not expose a custom segmented-control property or a horizontal multi-property
// row, so this adapter makes the safest native approximation without coupling
// the plug-in to OBS frontend/Qt internals:
//   * the two source modes render as explicit radio choices instead of a combo;
//   * Rescan is deferred until after the primary Open Plug-in action;
//   * successful status text is compact because the selected plug-in name is
//     already visible directly above it.
//
// The adapter is force-included only into the OBS module target via
// obs_compat_floor.hpp. Exact property-name checks make unrelated libobs
// property calls pass through unchanged.
namespace safevst3::obsux {

inline bool property_name_is(const char* actual, const char* expected) noexcept
{
    return actual && expected && std::strcmp(actual, expected) == 0;
}

inline obs_property_t* add_list(obs_properties_t* props,
                                const char* name,
                                const char* description,
                                enum obs_combo_type type,
                                enum obs_combo_format format)
{
    if (property_name_is(name, "vst3_source_mode"))
        type = OBS_COMBO_TYPE_RADIO;
    return ::obs_properties_add_list(props, name, description, type, format);
}

struct PendingRescanButton {
    obs_properties_t* props = nullptr;
    const char* name = nullptr;
    const char* text = nullptr;
    obs_property_clicked_t callback = nullptr;
    void* priv = nullptr;
};

inline thread_local PendingRescanButton pending_rescan{};

inline void flush_pending_rescan(obs_properties_t* props)
{
    if (pending_rescan.props != props || !pending_rescan.name)
        return;

    (void)::obs_properties_add_button2(
        pending_rescan.props,
        pending_rescan.name,
        pending_rescan.text,
        pending_rescan.callback,
        pending_rescan.priv);
    pending_rescan = {};
}

inline obs_property_t* add_button2(obs_properties_t* props,
                                   const char* name,
                                   const char* text,
                                   obs_property_clicked_t callback,
                                   void* priv)
{
    if (property_name_is(name, "rescan_vst3")) {
        // filter_properties() adds Rescan before the custom-path and Open
        // controls. Delay materialization so the novice workflow reads:
        // choose source -> choose plug-in -> Open Plug-in -> maintenance.
        pending_rescan = {props, name, text, callback, priv};
        return nullptr;
    }

    obs_property_t* property = ::obs_properties_add_button2(props, name, text, callback, priv);
    if (property_name_is(name, "open_plugin_ui"))
        flush_pending_rescan(props);
    return property;
}

inline std::string compact_plugin_status(const char* description)
{
    std::string status = description ? description : "";
    constexpr const char* ready_marker = " — Ready — ";
    const std::size_t ready = status.find(ready_marker);
    if (ready != std::string::npos)
        return "Ready · " + status.substr(ready + std::strlen(ready_marker));
    return status;
}

inline obs_property_t* add_text(obs_properties_t* props,
                                const char* name,
                                const char* description,
                                enum obs_text_type type)
{
    if (!property_name_is(name, "plugin_status"))
        return ::obs_properties_add_text(props, name, description, type);

    const std::string compact = compact_plugin_status(description);
    obs_property_t* property = ::obs_properties_add_text(props, name, compact.c_str(), type);
    if (property && compact.rfind("Plug-in unavailable", 0) == 0)
        obs_property_text_set_info_type(property, OBS_TEXT_INFO_WARNING);
    return property;
}

} // namespace safevst3::obsux

// Keep the existing plugin.cpp implementation focused on host behavior while
// adapting only these three public libobs property constructors for S1.11.
#define obs_properties_add_list safevst3::obsux::add_list
#define obs_properties_add_button2 safevst3::obsux::add_button2
#define obs_properties_add_text safevst3::obsux::add_text
