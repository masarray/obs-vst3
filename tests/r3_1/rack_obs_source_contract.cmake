set(PLUGIN_SOURCE "${ROOT}/src/obs-plugin/plugin.cpp")
set(MODULE_ENTRY_SOURCE "${ROOT}/src/obs-plugin/module_entry.cpp")
set(SINGLE_WRAPPER_SOURCE "${ROOT}/src/obs-plugin/plugin_single_wrapper.cpp")
set(RACK_FILTER_SOURCE "${ROOT}/src/obs-plugin/rack_filter.cpp")
set(ROOT_CMAKE "${ROOT}/CMakeLists.txt")
set(INSTALLER "${ROOT}/installer/windows/obs-safe-vst3.iss")
set(LOCALE "${ROOT}/data/locale/en-US.ini")

foreach(REQUIRED IN ITEMS
        "${PLUGIN_SOURCE}" "${MODULE_ENTRY_SOURCE}" "${SINGLE_WRAPPER_SOURCE}"
        "${RACK_FILTER_SOURCE}" "${ROOT_CMAKE}" "${INSTALLER}" "${LOCALE}")
    if(NOT EXISTS "${REQUIRED}")
        message(FATAL_ERROR "R3-1 required source missing: ${REQUIRED}")
    endif()
endforeach()

file(READ "${PLUGIN_SOURCE}" PLUGIN)
file(READ "${MODULE_ENTRY_SOURCE}" MODULE_ENTRY)
file(READ "${SINGLE_WRAPPER_SOURCE}" SINGLE_WRAPPER)
file(READ "${RACK_FILTER_SOURCE}" RACK_FILTER)
file(READ "${ROOT_CMAKE}" CMAKE_TEXT)
file(READ "${INSTALLER}" INSTALLER_TEXT)
file(READ "${LOCALE}" LOCALE_TEXT)

if(NOT PLUGIN MATCHES "obs_safe_vst3_filter")
    message(FATAL_ERROR "R3-1 must preserve the existing Single internal filter ID")
endif()
if(PLUGIN MATCHES "obs_safe_vst3_rack_filter")
    message(FATAL_ERROR "R3-1 must not inject Rack identity into the proven Single implementation")
endif()
if(NOT SINGLE_WRAPPER MATCHES "single_obs_module_load" OR
   NOT SINGLE_WRAPPER MATCHES "extern \"C\" bool single_obs_module_load" OR
   NOT SINGLE_WRAPPER MATCHES "extern \"C\" void single_obs_module_unload" OR
   NOT SINGLE_WRAPPER MATCHES "#include \"plugin.cpp\"")
    message(FATAL_ERROR "R3-1 must compose unchanged Single code with explicit internal entrypoint linkage")
endif()
if(NOT RACK_FILTER MATCHES "obs_safe_vst3_rack_filter")
    message(FATAL_ERROR "R3-1 must freeze a distinct Rack internal filter ID")
endif()
if(NOT MODULE_ENTRY MATCHES "single_obs_module_load\\(\\)" OR
   NOT MODULE_ENTRY MATCHES "obs_register_source\\(&rack_source_info\\)")
    message(FATAL_ERROR "module entry must load the proven Single surface and register the separate Rack source")
endif()
if(NOT RACK_FILTER MATCHES "OBS_SOURCE_TYPE_FILTER" OR NOT RACK_FILTER MATCHES "OBS_SOURCE_AUDIO")
    message(FATAL_ERROR "VST3 Rack must register as an OBS audio filter")
endif()
if(NOT RACK_FILTER MATCHES "obs_properties_create" OR
   NOT RACK_FILTER MATCHES "obs_properties_add_button2" OR
   NOT RACK_FILTER MATCHES "RackOpen" OR
   NOT RACK_FILTER MATCHES "RackSummary")
    message(FATAL_ERROR "R3-1 Rack Properties must expose summary/status plus the public Open Rack launcher")
endif()
if(NOT RACK_FILTER MATCHES "RackAudioReadGuard" OR
   NOT RACK_FILTER MATCHES "audio_readers" OR
   NOT RACK_FILTER MATCHES "bridge_mutex")
    message(FATAL_ERROR "R3-1 Rack adapter must protect helper lifetime across realtime audio and non-RT Properties teardown")
endif()
foreach(FORBIDDEN IN ITEMS "QWidget" "QObject" "Qt6::" "ImGui" "D3D11" "InstalledVST3" "RescanVST3" "CustomVST3Path")
    if(RACK_FILTER MATCHES "${FORBIDDEN}")
        message(FATAL_ERROR "R3-1 Rack OBS adapter leaked out-of-scope/private UI dependency: ${FORBIDDEN}")
    endif()
endforeach()
if(NOT CMAKE_TEXT MATCHES "src/obs-plugin/rack_filter.cpp" OR
   NOT CMAKE_TEXT MATCHES "src/platform/windows/win_rack_bridge.cpp" OR
   NOT CMAKE_TEXT MATCHES "src/obs-plugin/module_entry.cpp")
    message(FATAL_ERROR "normal OBS module build must compile the Rack adapter, parent bridge and composed module entry")
endif()
if(NOT CMAKE_TEXT MATCHES "obs-safe-vst3-rack-host")
    message(FATAL_ERROR "normal Windows build/package must include the Rack helper executable")
endif()
if(CMAKE_TEXT MATCHES "target_link_libraries\\(obs-safe-vst3[^\n]*safevst3_rack_imgui")
    message(FATAL_ERROR "Dear ImGui must never be linked into the OBS module")
endif()
if(NOT INSTALLER_TEXT MATCHES "obs-safe-vst3-rack-host.exe")
    message(FATAL_ERROR "Windows installer must install/clean the Rack helper")
endif()
if(NOT LOCALE_TEXT MATCHES "SafeVST3RackFilter" OR
   NOT LOCALE_TEXT MATCHES "RackSummary" OR
   NOT LOCALE_TEXT MATCHES "RackOpen" OR
   NOT LOCALE_TEXT MATCHES "RackStatus")
    message(FATAL_ERROR "R3-1 must provide normal-user Rack summary/status/launcher strings")
endif()

message(STATUS "R3-1 separate OBS Rack launcher/status source contract passed")
