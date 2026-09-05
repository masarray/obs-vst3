if(NOT EXISTS "${RACK_FILTER}" OR
   NOT EXISTS "${RACK_BRIDGE_HEADER}" OR
   NOT EXISTS "${RACK_BRIDGE_SOURCE}" OR
   NOT EXISTS "${RACK_SESSION_RUNTIME}" OR
   NOT EXISTS "${RACK_PERSISTENT_ENTRY}" OR
   NOT EXISTS "${RACK_PROTOCOL}" OR
   NOT EXISTS "${RACK_HOSTED_PLUGIN}")
    message(FATAL_ERROR "P9 production Rack persistence seam is missing")
endif()

file(READ "${RACK_FILTER}" FILTER_TEXT)
file(READ "${RACK_BRIDGE_HEADER}" BRIDGE_HEADER_TEXT)
file(READ "${RACK_BRIDGE_SOURCE}" BRIDGE_SOURCE_TEXT)
file(READ "${RACK_SESSION_RUNTIME}" RUNTIME_TEXT)
file(READ "${RACK_PERSISTENT_ENTRY}" ENTRY_TEXT)
file(READ "${RACK_PROTOCOL}" PROTOCOL_TEXT)
file(READ "${RACK_HOSTED_PLUGIN}" HOSTED_TEXT)

function(require_text HAYSTACK NEEDLE DESCRIPTION)
    string(FIND "${HAYSTACK}" "${NEEDLE}" FOUND)
    if(FOUND EQUAL -1)
        message(FATAL_ERROR "P9 Rack persistence contract missing: ${DESCRIPTION}")
    endif()
endfunction()

function(reject_text HAYSTACK NEEDLE DESCRIPTION)
    string(FIND "${HAYSTACK}" "${NEEDLE}" FOUND)
    if(NOT FOUND EQUAL -1)
        message(FATAL_ERROR "P9 Rack persistence contract rejected: ${DESCRIPTION}")
    endif()
endfunction()

# Each OBS Rack filter gets its own stable storage identity and explicitly
# flushes the helper when OBS serializes the scene collection. Public production
# startup must not silently fall back to a nonpersistent Rack.
require_text("${FILTER_TEXT}" "obs_source_get_uuid" "stable OBS source UUID identity")
require_text("${FILTER_TEXT}" "obs_module_config_path(\"rack-sessions\")" "OBS module config persistence directory")
require_text("${FILTER_TEXT}" "durable recall is required" "nonpersistent startup is surfaced as Needs Attention")
require_text("${FILTER_TEXT}" "bridge->start(\n        helper, filter.sample_rate, filter.channels, filter.session_path" "session-aware helper startup")
reject_text("${FILTER_TEXT}" "filter.session_path.empty()\n        ? bridge->start" "legacy nonpersistent production fallback")
require_text("${FILTER_TEXT}" "bridge->save_session" "bounded OBS save flush")
require_text("${FILTER_TEXT}" "last_session_save_succeeded" "explicit save outcome check")
require_text("${FILTER_TEXT}" "info.save = rack_save" "OBS source save callback")

# Parent/helper save handshakes are non-realtime named events and the legacy
# bounded shutdown constants remain unchanged. Completion and outcome are
# separate so a completed capture/write failure does not burn the timeout.
require_text("${BRIDGE_HEADER_TEXT}" "save_session(DWORD timeout_ms = 750)" "bounded session save API")
require_text("${BRIDGE_HEADER_TEXT}" "last_session_save_succeeded" "parent-visible save outcome")
require_text("${BRIDGE_SOURCE_TEXT}" "OBS_SAFE_VST3_RACK_SESSION_FILE" "per-child session file environment")
require_text("${BRIDGE_SOURCE_TEXT}" "session_save_event_" "save request event")
require_text("${BRIDGE_SOURCE_TEXT}" "session_saved_event_" "save completion event")
require_text("${BRIDGE_SOURCE_TEXT}" "kRackHelperGracefulShutdownTimeoutMs = 250" "qualified graceful shutdown bound")
require_text("${BRIDGE_SOURCE_TEXT}" "kRackHelperForcedShutdownWaitMs = 250" "qualified forced shutdown bound")
require_text("${PROTOCOL_TEXT}" "RackSessionSaveResult" "separate Rack session save result enum")
require_text("${PROTOCOL_TEXT}" "session_save_result" "shared save result publication")

# Helper runtime recovers the session identity/generation and never touches the
# audio callback; all persistence runs on the control worker.
require_text("${RUNTIME_TEXT}" "open_from_environment" "isolated helper session discovery")
require_text("${RUNTIME_TEXT}" "take_save_request" "nonblocking control-thread save request")
require_text("${RUNTIME_TEXT}" "signal_save_complete" "save completion acknowledgement")
require_text("${RUNTIME_TEXT}" "rack_session_id_for_path" "stable per-Rack session identity")

# Production Rack startup materializes VST instances and restores complete state
# on the same control owner that later owns native editors. Slow saved chains do
# not extend the fixed helper startup budget: the helper publishes a coherent
# empty/dry generation as Ready, builds the saved chain off the realtime path,
# then atomically swaps the complete restored generation.
require_text("${ENTRY_TEXT}" "load_rack_session_snapshot_lkg" "startup primary/LKG session restore")
require_text("${ENTRY_TEXT}" "materialize_preset_candidate" "VST chain materialization")
require_text("${ENTRY_TEXT}" "current_index ^ 1u" "restore builds in unpublished generation")
require_text("${ENTRY_TEXT}" "dry/Ready generation" "slow restore is admitted behind a coherent dry generation")
require_text("${ENTRY_TEXT}" "store.published_index.store(next_index" "atomic restored generation publication")
reject_text("${ENTRY_TEXT}" "bootstrap_cv.wait" "Ready must not block on aggregate vendor restore time")
require_text("${ENTRY_TEXT}" "std::thread command_worker" "dedicated Rack control worker")
require_text("${ENTRY_TEXT}" "restore_working_rack_session(\n            session_runtime" "session materialization executes inside control worker")

# Live component/controller capture is durable on topology changes and OBS save.
# State getState must establish a bounded DSP-safe frontier without a mutex that
# normal Rack DSP is required to acquire.
require_text("${ENTRY_TEXT}" "capture_current_rack" "live VST component/controller state capture")
require_text("${ENTRY_TEXT}" "write_rack_session_snapshot_atomic" "atomic durable working Rack write")
require_text("${ENTRY_TEXT}" "session_runtime.take_save_request()" "OBS save request polling")
require_text("${ENTRY_TEXT}" "RackSessionSaveResult::Failed" "save failure outcome publication")
require_text("${ENTRY_TEXT}" "session_runtime.signal_save_complete()" "completion signaled on every save outcome")
require_text("${ENTRY_TEXT}" "topology change" "immediate topology autosave")
require_text("${ENTRY_TEXT}" "preset load" "preset-load working Rack autosave")
require_text("${HOSTED_TEXT}" "capture_requested_" "atomic state-capture gate")
require_text("${HOSTED_TEXT}" "dsp_vendor_calls_" "in-flight DSP vendor-call frontier")
require_text("${HOSTED_TEXT}" "Timed out waiting for a DSP-safe Rack state capture frontier" "bounded capture wait")
require_text("${HOSTED_TEXT}" "DspVendorCallGuard" "lock-free DSP-side capture exclusion")

message(STATUS "P9 OBS Rack working-session persistence contract passed")