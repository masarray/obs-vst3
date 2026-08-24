if(NOT DEFINED SOURCE_FILE)
    message(FATAL_ERROR "SOURCE_FILE is required")
endif()

file(READ "${SOURCE_FILE}" SOURCE_TEXT)

# OBS 32's Qt properties view owns obs_property_t pointers throughout
# WidgetInfo::ControlChanged()/obs_property_modified(). Rebuilding a source's
# complete property tree from plug-in runtime/recovery/scanner code can
# invalidate those pointers mid-callback. Property callbacks must instead
# return true and let OBS queue RefreshProperties after the callback returns.
string(REGEX MATCH "(^|\n)[ \t]*obs_source_update_properties[ \t]*\\(" UNSAFE_REFRESH "${SOURCE_TEXT}")
if(UNSAFE_REFRESH)
    message(FATAL_ERROR
        "Unsafe obs_source_update_properties() call found in OBS plug-in. "
        "Use a property modified callback that returns true; worker/runtime "
        "threads must never rebuild an open OBS Properties tree.")
endif()

message(STATUS "OBS Properties ownership guard passed")
