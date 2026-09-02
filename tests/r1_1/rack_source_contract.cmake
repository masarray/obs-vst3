if(NOT DEFINED RACK_PROTOCOL OR NOT DEFINED RACK_SOURCE)
    message(FATAL_ERROR "RACK_PROTOCOL and RACK_SOURCE are required")
endif()

file(READ "${RACK_PROTOCOL}" protocol)
file(READ "${RACK_SOURCE}" source)

if(protocol MATCHES "#include[ \t]+\"common/protocol\\.hpp\"")
    message(FATAL_ERROR "Rack protocol must not depend on the Single protocol")
endif()
if(NOT protocol MATCHES "kRackProtocolVersion = [1-9][0-9]*")
    message(FATAL_ERROR "Rack protocol must have its own explicit nonzero version")
endif()

# R1-1's enduring contract is strict A -> B serial processing through the
# protocol-neutral HostedPlugin seam, not a frozen v1 transport layout. R1-2
# may add bypass/fail-dry control while the active A/B path stays ordered.
if(NOT source MATCHES "ProcessBlockView block_a\\{current, ping")
    message(FATAL_ERROR "Rack DSP active slot A must process current input into ping")
endif()
if(NOT source MATCHES "current = ping")
    message(FATAL_ERROR "Rack DSP must publish slot A output as the next serial input")
endif()
if(NOT source MATCHES "ProcessBlockView block_b\\{current, b_output")
    message(FATAL_ERROR "Rack DSP active slot B must consume the serial current input")
endif()
if(NOT source MATCHES "std::array<std::array<float, kMaxFrames>, kMaxChannels> ping")
    message(FATAL_ERROR "Rack DSP must own preallocated ping storage")
endif()
if(NOT source MATCHES "std::array<std::array<float, kMaxFrames>, kMaxChannels> pong")
    message(FATAL_ERROR "Rack DSP must own preallocated pong storage")
endif()

# Guard the small Rack source against project-owned heap APIs. Startup's
# std::string/std::thread runtime internals are not project-owned DSP
# allocations and are outside the normal block loop.
foreach(forbidden IN ITEMS "std::vector" "push_back" "resize(" "new " "malloc(" "calloc(" "realloc(" "make_unique" "make_shared")
    string(FIND "${source}" "${forbidden}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "Rack source contains allocation-capable token: ${forbidden}")
    endif()
endforeach()

message(STATUS "R1-1 Rack protocol isolation and serial DSP allocation contract passed")
