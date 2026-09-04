foreach(FILE_VAR IN ITEMS
        RACK_FILTER
        RACK_BRIDGE_HEADER
        RACK_BRIDGE_SOURCE
        RACK_STABLE_MAIN
        RACK_HOSTED
        RACK_SHIPPING_ENTRY)
    if(NOT EXISTS "${${FILE_VAR}}")
        message(FATAL_ERROR "R5-1 required production source is missing: ${FILE_VAR}")
    endif()
endforeach()

file(READ "${RACK_FILTER}" FILTER_TEXT)
file(READ "${RACK_BRIDGE_HEADER}" BRIDGE_HEADER_TEXT)
file(READ "${RACK_BRIDGE_SOURCE}" BRIDGE_SOURCE_TEXT)
file(READ "${RACK_STABLE_MAIN}" STABLE_TEXT)
file(READ "${RACK_HOSTED}" HOSTED_TEXT)
file(READ "${RACK_SHIPPING_ENTRY}" ENTRY_TEXT)

foreach(REQUIRED IN ITEMS
        "rack_session_id"
        "obs_data_set_string"
        "rack_session_path"
        "RackRecoveryPolicy"
        "rack_supervisor_loop"
        "bridge_restarting"
        "kMaxRecoveryAttempts"
        "recovery_quarantined"
        "RackAudioReadGuard"
        "restart_rack_bridge")
    string(FIND "${FILTER_TEXT}" "${REQUIRED}" FOUND)
    if(FOUND EQUAL -1)
        message(FATAL_ERROR "R5-1 OBS Rack filter missing stable recovery/session seam: ${REQUIRED}")
    endif()
endforeach()

foreach(REQUIRED IN ITEMS
        "RackBridgeHealthSnapshot"
        "health_snapshot"
        "session_snapshot"
        "session_id")
    string(FIND "${BRIDGE_HEADER_TEXT}" "${REQUIRED}" FOUND)
    if(FOUND EQUAL -1)
        message(FATAL_ERROR "R5-1 Rack bridge header missing stable seam: ${REQUIRED}")
    endif()
endforeach()

foreach(REQUIRED IN ITEMS
        "--session-snapshot"
        "--session-id"
        "dsp_progress_generation"
        "deadline_misses")
    string(FIND "${BRIDGE_SOURCE_TEXT}" "${REQUIRED}" FOUND)
    if(FOUND EQUAL -1)
        message(FATAL_ERROR "R5-1 Rack bridge source missing stable launch/health behavior: ${REQUIRED}")
    endif()
endforeach()

foreach(REQUIRED IN ITEMS
        "load_rack_session_snapshot_lkg"
        "write_rack_session_snapshot_atomic"
        "restore_session_snapshot"
        "checkpoint_session_snapshot"
        "kSessionDirtyDebounce"
        "kSessionFallbackCheckpoint"
        "take_rack_state_dirty"
        "build_r3_5_ui_snapshot"
        "preset load"
        "Rack edit")
    string(FIND "${STABLE_TEXT}" "${REQUIRED}" FOUND)
    if(FOUND EQUAL -1)
        message(FATAL_ERROR "R5-1 stable helper missing automatic Session Snapshot behavior: ${REQUIRED}")
    endif()
endforeach()

foreach(REQUIRED IN ITEMS
        "state_dirty_"
        "performEdit"
        "take_state_dirty")
    string(FIND "${HOSTED_TEXT}" "${REQUIRED}" FOUND)
    if(FOUND EQUAL -1)
        message(FATAL_ERROR "R5-1 Rack HostedPlugin missing native-editor dirty tracking: ${REQUIRED}")
    endif()
endforeach()

string(FIND "${ENTRY_TEXT}" "rack/main_r3_5.cpp" STABLE_ENTRY)
if(STABLE_ENTRY EQUAL -1)
    message(FATAL_ERROR "R5-1 shipping Rack helper does not enter the stable R3-5 composition")
endif()

# Realtime path must remain a bounded reader of the already-running bridge. Do
# not allow recovery/start/session filesystem work to leak into filter_audio.
string(FIND "${FILTER_TEXT}" "obs_audio_data* rack_filter_audio" AUDIO_START)
string(FIND "${FILTER_TEXT}" "} // namespace" AUDIO_END)
if(AUDIO_START EQUAL -1 OR AUDIO_END EQUAL -1 OR AUDIO_END LESS AUDIO_START)
    message(FATAL_ERROR "R5-1 could not isolate rack_filter_audio source")
endif()
math(EXPR AUDIO_LENGTH "${AUDIO_END} - ${AUDIO_START}")
string(SUBSTRING "${FILTER_TEXT}" ${AUDIO_START} ${AUDIO_LENGTH} AUDIO_TEXT)
foreach(FORBIDDEN IN ITEMS
        "restart_rack_bridge"
        "start_rack_bridge"
        "rack_session_path"
        "std::filesystem"
        "std::lock_guard"
        "std::unique_lock")
    string(FIND "${AUDIO_TEXT}" "${FORBIDDEN}" FOUND)
    if(NOT FOUND EQUAL -1)
        message(FATAL_ERROR "R5-1 realtime Rack callback contains forbidden control work: ${FORBIDDEN}")
    endif()
endforeach()

message(STATUS "R5-1 stable Rack production source contract passed")
