if(NOT DEFINED ROOT)
    message(FATAL_ERROR "ROOT is required")
endif()

file(READ "${ROOT}/src/rack/rack_editor_window.cpp" EDITOR)
file(READ "${ROOT}/src/rack/main.cpp" MAIN)
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
    "obs-safe-vst3-scanner.exe"
    "plugins.tsv"
    "RackUiCommandType::AddSlot"
    "RackUiCommandType::ReplaceSlot"
    "RackUiCommandType::RemoveSlot"
    "RackUiCommandType::SetBypass")
    string(FIND "${MAIN}" "${REQUIRED_MAIN_TEXT}" POS)
    if(POS EQUAL -1)
        message(FATAL_ERROR "R3-2 helper/control contract missing: ${REQUIRED_MAIN_TEXT}")
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
