if(NOT DEFINED RACK_PROTOCOL OR NOT DEFINED RACK_SOURCE)
    message(FATAL_ERROR "RACK_PROTOCOL and RACK_SOURCE are required")
endif()

file(READ "${RACK_PROTOCOL}" protocol)
file(READ "${RACK_SOURCE}" source)

if(protocol MATCHES "common/protocol\\.hpp")
    message(FATAL_ERROR "Rack protocol must not depend on the Single protocol")
endif()
if(NOT protocol MATCHES "kRackProtocolVersion = 1")
    message(FATAL_ERROR "R1-1 Rack protocol must have its own explicit version")
endif()
if(NOT source MATCHES "ProcessBlockView block_a\\{rack_input, ping")
    message(FATAL_ERROR "Rack DSP must process slot A from transport input into ping")
endif()
if(NOT source MATCHES "ProcessBlockView block_b\\{ping, pong")
    message(FATAL_ERROR "Rack DSP must process slot B from ping into pong")
endif()
if(NOT source MATCHES "std::array<std::array<float, kMaxFrames>, kMaxChannels> ping")
    message(FATAL_ERROR "Rack DSP must own preallocated ping storage")
endif()
if(NOT source MATCHES "std::array<std::array<float, kMaxFrames>, kMaxChannels> pong")
    message(FATAL_ERROR "Rack DSP must own preallocated pong storage")
endif()

# R1-1's Rack source is intentionally small. Guard the whole file against
# project-owned heap APIs; startup's std::string/std::thread runtime internals
# are not project-owned DSP allocations and are outside the block loop.
foreach(forbidden IN ITEMS "std::vector" "push_back" "resize(" "new " "malloc(" "calloc(" "realloc(" "make_unique" "make_shared")
    string(FIND "${source}" "${forbidden}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "Rack source contains allocation-capable token: ${forbidden}")
    endif()
endforeach()

message(STATUS "R1-1 Rack protocol isolation and DSP allocation source contract passed")
