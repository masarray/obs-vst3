if(NOT DEFINED ROOT)
    message(FATAL_ERROR "ROOT is required")
endif()

file(READ "${ROOT}/src/rack/rack_editor_window.cpp" EDITOR)
file(READ "${ROOT}/src/rack/main_r3_2.cpp" MAIN)
file(READ "${ROOT}/src/rack/rack_plugin_catalog.cpp" CATALOG)
file(READ "${ROOT}/src/rack/CMakeLists.txt" RACK_CMAKE)

foreach(REQUIRED_TEXT
    "Search plug-ins"
    "Add Effect"
    "Move Up"
    "Move Down"
    "Replace"
    "Remove"
    "Bypass")
    string(FIND "${EDITOR}" "${REQUIRED_TEXT}" POS)
    if(POS EQUAL -1)
        message(FATAL_ERROR "R3-2 editor contract missing: ${REQUIRED_TEXT}")
    endif()
endforeach()

foreach(REQUIRED_MAIN_TEXT
    "RackUiCommandType::AddSlot"
    "RackUiCommandType::ReplaceSlot"
    "RackUiCommandType::RemoveSlot"
    "RackUiCommandType::SetBypass"
    "safevst3_r3_1_legacy_run"
    "run_r3_2_product")
    string(FIND "${MAIN}" "${REQUIRED_MAIN_TEXT}" POS)
    if(POS EQUAL -1)
        message(FATAL_ERROR "R3-2 helper/control contract missing: ${REQUIRED_MAIN_TEXT}")
    endif()
endforeach()

foreach(REQUIRED_CATALOG_TEXT
    "obs-safe-vst3-scanner.exe"
    "OBS Safe VST3 Host"
    "plugins.tsv"
    "--scan-to")
    string(FIND "${CATALOG}" "${REQUIRED_CATALOG_TEXT}" POS)
    if(POS EQUAL -1)
        message(FATAL_ERROR "R3-2 isolated catalog contract missing: ${REQUIRED_CATALOG_TEXT}")
    endif()
endforeach()

string(FIND "${EDITOR}" "OpenVendorEditor" VENDOR_POS)
if(NOT VENDOR_POS EQUAL -1)
    message(FATAL_ERROR "R3-3 vendor editor orchestration leaked into R3-2")
endif()

string(FIND "${EDITOR}" "Save as Preset" PRESET_POS)
if(NOT PRESET_POS EQUAL -1)
    message(FATAL_ERROR "R3-4 preset UX leaked into R3-2")
endif()

string(FIND "${RACK_CMAKE}" "safevst3_rack_imgui" IMGUI_POS)
if(IMGUI_POS EQUAL -1)
    message(FATAL_ERROR "helper-only Rack editor dependency boundary disappeared")
endif()
