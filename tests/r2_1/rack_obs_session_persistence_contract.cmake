if(NOT EXISTS "${RACK_FILTER}" OR
   NOT EXISTS "${RACK_BRIDGE_HEADER}" OR
   NOT EXISTS "${RACK_BRIDGE_SOURCE}" OR
   NOT EXISTS "${RACK_SESSION_RUNTIME}" OR
   NOT EXISTS "${RACK_PERSISTENT_ENTRY}")
    message(FATAL_ERROR "P9 production Rack persistence seam is missing")
endif()

file(READ "${RACK_FILTER}" FILTER_TEXT)
file(READ "${RACK_BRIDGE_HEADER}" BRIDGE_HEADER_TEXT)
file(READ "${RACK_BRIDGE_SOURCE}" BRIDGE_SOURCE_TEXT)
file(READ "${RACK_SESSION_RUNTIME}" RUNTIME_TEXT)
file(READ "${RACK_PERSISTENT_ENTRY}" ENTRY_TEXT)

function(require_text HAYSTACK NEEDLE DESCRIPTION)
    string(FIND "${HAYSTACK}" "${NEEDLE}" FOUND)
    if(FOUND EQUAL -1)
        message(FATAL_ERROR "P9 Rack persistence contract missing: ${DESCRIPTION}")
    endif()
endfunction()

# Each OBS Rack filter gets its own stable storage identity and explicitly
# flushes the helper when OBS serializes the scene collection.
require_text("${FILTER_TEXT}" "obs_source_get_uuid" "stable OBS source UUID identity")
require_text("${FILTER_TEXT}" "obs_module_config_path(\"rack-sessions\")" "OBS module config persistence directory")
require_text("${FILTER_TEXT}" "bridge->start(helper, filter.sample_rate, filter.channels," "session-aware helper startup")
require_text("${FILTER_TEXT}" "bridge->save_session" "bounded OBS save flush")
require_text("${FILTER_TEXT}" "info.save = rack_save" "OBS source save callback")

# Parent/helper save handshakes are non-realtime named events and the legacy
# bounded shutdown constants remain unchanged.
require_text("${BRIDGE_HEADER_TEXT}" "save_session(DWORD timeout_ms = 750)" "bounded session save API")
require_text("${BRIDGE_SOURCE_TEXT}" "OBS_SAFE_VST3_RACK_SESSION_FILE" "per-child session file environment")
require_text("${BRIDGE_SOURCE_TEXT}" "session_save_event_" "save request event")
require_text("${BRIDGE_SOURCE_TEXT}" "session_saved_event_" "save completion event")
require_text("${BRIDGE_SOURCE_TEXT}" "kRackHelperGracefulShutdownTimeoutMs = 250" "qualified graceful shutdown bound")
require_text("${BRIDGE_SOURCE_TEXT}" "kRackHelperForcedShutdownWaitMs = 250" "qualified forced shutdown bound")

# Helper runtime recovers the session identity/generation and never touches the
# audio callback; all persistence runs on the control worker.
require_text("${RUNTIME_TEXT}" "open_from_environment" "isolated helper session discovery")
require_text("${RUNTIME_TEXT}" "take_save_request" "nonblocking control-thread save request")
require_text("${RUNTIME_TEXT}" "signal_save_complete" "save completion acknowledgement")
require_text("${RUNTIME_TEXT}" "rack_session_id_for_path" "stable per-Rack session identity")

# Production Rack startup must materialize VST instances and restore their
# component/controller state. Topology changes autosave immediately, while OBS
# save captures the latest live VST state even without a topology edit.
require_text("${ENTRY_TEXT}" "load_rack_session_snapshot_lkg" "startup primary/LKG session restore")
require_text("${ENTRY_TEXT}" "materialize_preset_candidate" "VST chain materialization")
require_text("${ENTRY_TEXT}" "capture_current_rack" "live VST component/controller state capture")
require_text("${ENTRY_TEXT}" "write_rack_session_snapshot_atomic" "atomic durable working Rack write")
require_text("${ENTRY_TEXT}" "session_runtime.take_save_request()" "OBS save request polling")
require_text("${ENTRY_TEXT}" "topology change" "immediate topology autosave")
require_text("${ENTRY_TEXT}" "preset load" "preset-load working Rack autosave")

message(STATUS "P9 OBS Rack working-session persistence contract passed")
