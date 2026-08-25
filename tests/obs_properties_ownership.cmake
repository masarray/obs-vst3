if(NOT DEFINED SOURCE_FILE)
    message(FATAL_ERROR "SOURCE_FILE is required")
endif()

file(READ "${SOURCE_FILE}" SOURCE_TEXT)

# OBS 32's Qt properties view owns obs_property_t pointers throughout
# WidgetInfo::ControlChanged()/obs_property_modified(). Rebuilding a source's
# complete property tree from plug-in runtime/recovery/scanner code can
# invalidate those pointers mid-callback. Property callbacks must instead
# return true and let OBS queue RefreshProperties after the callback returns.
string(REGEX MATCHALL "obs_source_update_properties[ \t]*\\(" PROPERTY_REFRESH_CALLS "${SOURCE_TEXT}")
list(LENGTH PROPERTY_REFRESH_CALLS PROPERTY_REFRESH_COUNT)
if(PROPERTY_REFRESH_COUNT GREATER 1)
    message(FATAL_ERROR
        "Only the audited OBS UI-task callback may call obs_source_update_properties().")
endif()

# Background scanning may finish after the button callback returns. Its one
# allowed full-tree refresh must be queued onto OBS_TASK_UI and hold only an OBS
# weak source across the worker/UI handoff.
foreach(REQUIRED_REFRESH_TOKEN
        "refresh_scan_waiter_on_ui"
        "obs_queue_task(OBS_TASK_UI"
        "obs_source_get_weak_source"
        "obs_weak_source_get_source"
        "obs_source_update_properties")
    string(FIND "${SOURCE_TEXT}" "${REQUIRED_REFRESH_TOKEN}" TOKEN_POS)
    if(TOKEN_POS EQUAL -1)
        message(FATAL_ERROR "Safe asynchronous Rescan refresh contract missing: ${REQUIRED_REFRESH_TOKEN}")
    endif()
endforeach()

# User-facing Single Host UX is intentionally mutually exclusive: users choose
# an installed plug-in OR manual Browse. Generic parameter walls are not part
# of the normal OBS filter surface.
foreach(REQUIRED_TOKEN
        "kSourceMode"
        "kSourceInstalled"
        "kSourceBrowse"
        "source_mode_modified"
        "update_source_mode_visibility")
    string(FIND "${SOURCE_TEXT}" "${REQUIRED_TOKEN}" TOKEN_POS)
    if(TOKEN_POS EQUAL -1)
        message(FATAL_ERROR "Simple VST3 source-mode UX contract missing: ${REQUIRED_TOKEN}")
    endif()
endforeach()

# Manual Browse is explicitly for selecting a standalone .vst3 file. A
# directory-only picker makes the advertised Browse mode unusable for the
# common single-file Windows VST3 layout.
foreach(REQUIRED_BROWSE_TOKEN
        "OBS_PATH_FILE"
        "VST3FileFilter")
    string(FIND "${SOURCE_TEXT}" "${REQUIRED_BROWSE_TOKEN}" TOKEN_POS)
    if(TOKEN_POS EQUAL -1)
        message(FATAL_ERROR
            "Manual VST3 Browse must use a filtered file picker: ${REQUIRED_BROWSE_TOKEN}")
    endif()
endforeach()

string(FIND "${SOURCE_TEXT}" "OBS_PATH_DIRECTORY" DIRECTORY_PICKER_POS)
if(NOT DIRECTORY_PICKER_POS EQUAL -1)
    message(FATAL_ERROR
        "Manual VST3 Browse must not open the directory-only Select Folder dialog.")
endif()

string(FIND "${SOURCE_TEXT}" "add_generic_parameter_properties(" FALLBACK_UI_POS)
if(NOT FALLBACK_UI_POS EQUAL -1)
    message(FATAL_ERROR
        "Generic VST3 fallback parameter wall must not be rendered in the normal OBS Properties UI.")
endif()

message(STATUS "OBS Properties ownership and simple UX guards passed")
