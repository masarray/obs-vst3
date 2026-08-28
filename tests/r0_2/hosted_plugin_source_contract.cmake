if(NOT DEFINED HOSTED_PLUGIN_HEADER)
    message(FATAL_ERROR "HOSTED_PLUGIN_HEADER is required")
endif()

file(READ "${HOSTED_PLUGIN_HEADER}" HEADER)

foreach(FORBIDDEN IN ITEMS
    "common/protocol.hpp"
    "AudioSlot"
    "SharedAudioRegion"
    "RackSlotRuntime"
    "RackChainGeneration"
)
    string(FIND "${HEADER}" "${FORBIDDEN}" FOUND)
    if(NOT FOUND EQUAL -1)
        message(FATAL_ERROR "HostedPlugin public seam leaked forbidden transport/topology symbol: ${FORBIDDEN}")
    endif()
endforeach()

foreach(REQUIRED IN ITEMS
    "class HostedPlugin"
    "ProcessBlockView"
    "capture_state"
    "restore_state"
    "latency_samples"
    "edit_controller"
)
    string(FIND "${HEADER}" "${REQUIRED}" FOUND)
    if(FOUND EQUAL -1)
        message(FATAL_ERROR "HostedPlugin public seam missing required responsibility marker: ${REQUIRED}")
    endif()
endforeach()
