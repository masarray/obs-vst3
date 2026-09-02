if(NOT EXISTS "${SNAPSHOT_HEADER}" OR NOT EXISTS "${SNAPSHOT_SOURCE}")
    message(FATAL_ERROR "R2-1 production Rack Session Snapshot seam is missing")
endif()

file(READ "${SNAPSHOT_HEADER}" HEADER_TEXT)
file(READ "${SNAPSHOT_SOURCE}" SOURCE_TEXT)

foreach(REQUIRED
        "kRackSessionFormatVersion"
        "kRackSessionMaxPluginPathBytes"
        "RackPersistedSlotHealth"
        "RackSessionSlotSnapshot"
        "RackSessionSnapshot"
        "RackSessionLoadSource"
        "capture_rack_session_slot"
        "restore_rack_session_slot_state"
        "encode_rack_session_snapshot"
        "decode_rack_session_snapshot"
        "write_rack_session_snapshot_atomic"
        "load_rack_session_snapshot_lkg")
    string(FIND "${HEADER_TEXT}" "${REQUIRED}" FOUND)
    if(FOUND EQUAL -1)
        message(FATAL_ERROR "R2-1 snapshot header missing required contract: ${REQUIRED}")
    endif()
endforeach()

foreach(REQUIRED
        "kRackMaxSlots"
        "kMaxStateBytes"
        "FlushFileBuffers"
        "MoveFileExW"
        "MOVEFILE_REPLACE_EXISTING"
        "MOVEFILE_WRITE_THROUGH"
        ".previous"
        "encode_state_blob"
        "decode_state_blob")
    string(FIND "${SOURCE_TEXT}" "${REQUIRED}" FOUND)
    if(FOUND EQUAL -1)
        message(FATAL_ERROR "R2-1 snapshot source missing durability/bounds primitive: ${REQUIRED}")
    endif()
endforeach()

foreach(FORBIDDEN
        "editor_open"
        "rack_editor_open"
        "vendor_window_open"
        "popup_state"
        "drag_state"
        "pending_command"
        "preset_uuid"
        "Save as Preset")
    string(FIND "${HEADER_TEXT}${SOURCE_TEXT}" "${FORBIDDEN}" FOUND)
    if(NOT FOUND EQUAL -1)
        message(FATAL_ERROR "R2-1 Session Snapshot must not persist transient UI or named-preset state: ${FORBIDDEN}")
    endif()
endforeach()

string(FIND "${HEADER_TEXT}" "common/protocol.hpp" SINGLE_PROTOCOL)
if(NOT SINGLE_PROTOCOL EQUAL -1)
    message(FATAL_ERROR "Rack Session Snapshot must not depend on Single protocol layout")
endif()

message(STATUS "R2-1 Rack Session Snapshot source contract passed")
