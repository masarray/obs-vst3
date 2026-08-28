if(NOT DEFINED HEADER OR NOT EXISTS "${HEADER}")
    message(FATAL_ERROR "R0-2 HostedPlugin header is missing")
endif()

file(READ "${HEADER}" source)

foreach(forbidden IN ITEMS "AudioSlot" "SharedAudioRegion" "common/protocol.hpp")
    string(FIND "${source}" "${forbidden}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "HostedPlugin public seam must remain protocol-neutral; found forbidden token: ${forbidden}")
    endif()
endforeach()

string(FIND "${source}" "class HostedPlugin" class_position)
if(class_position EQUAL -1)
    message(FATAL_ERROR "HostedPlugin public seam must declare class HostedPlugin")
endif()

string(FIND "${source}" "ProcessBlockView" process_view_position)
if(process_view_position EQUAL -1)
    message(FATAL_ERROR "HostedPlugin public seam must expose ProcessBlockView processing")
endif()

message(STATUS "R0-2 HostedPlugin public seam is protocol-neutral")
