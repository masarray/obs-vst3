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

// Angle brackets are intentional: the shipping target puts its generated
// R3-4 include root before the checked-in source tree, so persistence wiring
// can be qualified without mutating the already-proven R3-4 fixture source.
#include <rack/main_r3_4.cpp>
