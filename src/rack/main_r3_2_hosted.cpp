#ifdef _WIN32
// Include the vendor-editor manager before rack_hosted_plugin.hpp defines the
// Rack-local HostedPlugin token macro. The manager intentionally accepts the
// protocol-neutral HostedPlugin base; RackHostedPlugin derives from it and can
// therefore reuse the exact Single NativeEditorWindow implementation without
// exporting a macro-renamed manager ABI.
#include "rack/rack_vendor_editor_manager.hpp"
#include "rack/rack_hosted_plugin.hpp"
#endif

#include "rack/main_r3_2.cpp"
