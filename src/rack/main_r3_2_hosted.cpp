#ifdef _WIN32
// Keep protocol-neutral preset/session declarations visible before
// rack_hosted_plugin.hpp defines the Rack-local HostedPlugin token macro.
// This preserves the established base HostedPlugin ABI while allowing the
// dynamic Rack translation unit to use RackHostedPlugin instances.
#include "rack/rack_vendor_editor_manager.hpp"
#include "rack/rack_preset_management.hpp"
#include "rack/rack_preset_ui_contract.hpp"
#include "rack/rack_session_snapshot.hpp"
#include "rack/rack_session_runtime.hpp"
#include "rack/rack_hosted_plugin.hpp"
#endif

// The persistence wrapper includes the qualified R3-4 implementation in the
// same translation unit, renames only its product entrypoint, then layers the
// OBS-session restore/autosave lifecycle on top of those proven primitives.
#include "rack/main_r3_4_persistent.cpp"
