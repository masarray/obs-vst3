if(NOT DEFINED RACK_PROTOCOL OR NOT DEFINED RACK_SOURCE OR NOT DEFINED RACK_RECOVERY)
    message(FATAL_ERROR "RACK_PROTOCOL, RACK_SOURCE and RACK_RECOVERY are required")
endif()

file(READ "${RACK_PROTOCOL}" PROTOCOL_TEXT)
file(READ "${RACK_SOURCE}" SOURCE_TEXT)
file(READ "${RACK_RECOVERY}" RECOVERY_TEXT)

string(FIND "${PROTOCOL_TEXT}" "#include \"common/protocol.hpp\"" SINGLE_PROTOCOL_INCLUDE)
if(NOT SINGLE_PROTOCOL_INCLUDE EQUAL -1)
    message(FATAL_ERROR "Rack protocol must remain independent from Single common/protocol.hpp")
endif()
if(NOT PROTOCOL_TEXT MATCHES "kRackProtocolVersion = ([4-9]|[1-9][0-9]+)")
    message(FATAL_ERROR "R1-4 breadcrumb layout must advance the independent Rack protocol beyond v3")
endif()
foreach(REQUIRED
        "enum class RackBreadcrumbPhase"
        "breadcrumb_epoch"
        "breadcrumb_chain_generation"
        "breadcrumb_audio_sequence"
        "breadcrumb_slot_id"
        "breadcrumb_phase"
        "breadcrumb_dsp_progress")
    string(FIND "${PROTOCOL_TEXT}" "${REQUIRED}" FOUND_TOKEN)
    if(FOUND_TOKEN EQUAL -1)
        message(FATAL_ERROR "R1-4 Rack protocol missing breadcrumb token: ${REQUIRED}")
    endif()
endforeach()

string(FIND "${SOURCE_TEXT}" "publish_process_breadcrumb(*endpoint.region" BREADCRUMB_CALL)
string(FIND "${SOURCE_TEXT}" "slot.plugin->process(block)" PROCESS_CALL)
if(BREADCRUMB_CALL EQUAL -1 OR PROCESS_CALL EQUAL -1 OR NOT BREADCRUMB_CALL LESS PROCESS_CALL)
    message(FATAL_ERROR "Rack helper must publish coherent Process breadcrumb before vendor process work")
endif()
string(FIND "${SOURCE_TEXT}" "clear_rack_breadcrumb(*endpoint.region)" CLEAR_CALL)
if(CLEAR_CALL EQUAL -1)
    message(FATAL_ERROR "Rack helper must clear active vendor breadcrumb after successful vendor work/block")
endif()
string(FIND "${SOURCE_TEXT}" "dsp_progress_generation" DSP_PROGRESS)
if(DSP_PROGRESS EQUAL -1)
    message(FATAL_ERROR "Rack DSP must maintain bounded progress generation for breadcrumb evidence")
endif()

string(FIND "${RECOVERY_TEXT}" "common/recovery_policy.hpp" COMMON_POLICY)
if(COMMON_POLICY EQUAL -1)
    message(FATAL_ERROR "R1-4 Rack recovery policy must reuse existing bounded product backoff discipline")
endif()
foreach(REQUIRED
        "RackFailureConfidence"
        "Unknown"
        "Suspect"
        "classify_rack_helper_death"
        "RackRecoveryPolicy"
        "read_rack_breadcrumb")
    string(FIND "${RECOVERY_TEXT}" "${REQUIRED}" FOUND_TOKEN)
    if(FOUND_TOKEN EQUAL -1)
        message(FATAL_ERROR "R1-4 recovery contract missing token: ${REQUIRED}")
    endif()
endforeach()
string(FIND "${RECOVERY_TEXT}" "Quarantined" QUARANTINE_TOKEN)
if(NOT QUARANTINE_TOKEN EQUAL -1)
    message(FATAL_ERROR "R1-4 must not implement R2-2 quarantine state/policy")
endif()

foreach(FORBIDDEN "std::mutex" "std::lock_guard" "std::unique_lock")
    string(FIND "${SOURCE_TEXT}" "${FORBIDDEN}" FOUND_TOKEN)
    if(NOT FOUND_TOKEN EQUAL -1)
        message(FATAL_ERROR "Rack source must not make normal DSP depend on a control mutex: ${FORBIDDEN}")
    endif()
endforeach()
foreach(FORBIDDEN "std::vector" "std::make_unique" "std::make_shared" "new " "malloc(" "calloc(" "realloc(")
    string(FIND "${SOURCE_TEXT}" "${FORBIDDEN}" FOUND_TOKEN)
    if(NOT FOUND_TOKEN EQUAL -1)
        message(FATAL_ERROR "project-owned allocation token '${FORBIDDEN}' found in Rack source")
    endif()
endforeach()

message(STATUS "R1-4 crash breadcrumb and bounded recovery source contract passed")
