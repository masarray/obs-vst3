if(NOT DEFINED SCANNER OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "SCANNER and WORK_DIR are required")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/root")
set(CACHE_FILE "${WORK_DIR}/plugins.tsv")
set(PLUGIN_FILE "${WORK_DIR}/root/TestDiscovery.vst3")

file(WRITE "${PLUGIN_FILE}" "deterministic discovery fixture")
file(WRITE "${CACHE_FILE}" "Stale\tC:/Removed/Stale.vst3\t00112233445566778899AABBCCDDEEFF\n")

execute_process(
    COMMAND "${SCANNER}" --discover-to "${CACHE_FILE}" --root "${WORK_DIR}/root"
    RESULT_VARIABLE DISCOVER_RESULT
    OUTPUT_VARIABLE DISCOVER_OUTPUT
    ERROR_VARIABLE DISCOVER_ERROR
    TIMEOUT 5
)
if(NOT DISCOVER_RESULT EQUAL 0)
    message(FATAL_ERROR
        "scanner discovery-only CLI failed: ${DISCOVER_RESULT}\n${DISCOVER_OUTPUT}\n${DISCOVER_ERROR}")
endif()

file(READ "${CACHE_FILE}" DISCOVERED_CACHE)
string(FIND "${DISCOVERED_CACHE}" "TestDiscovery" DISCOVERED_POS)
string(FIND "${DISCOVERED_CACHE}" "Stale" STALE_POS)
if(DISCOVERED_POS EQUAL -1 OR NOT STALE_POS EQUAL -1)
    message(FATAL_ERROR
        "discovery-only CLI must atomically replace stale cache with the discovered bundle")
endif()

file(REMOVE "${PLUGIN_FILE}")
execute_process(
    COMMAND "${SCANNER}" --discover-to "${CACHE_FILE}" --root "${WORK_DIR}/root"
    RESULT_VARIABLE EMPTY_RESULT
    OUTPUT_VARIABLE EMPTY_OUTPUT
    ERROR_VARIABLE EMPTY_ERROR
    TIMEOUT 5
)
if(NOT EMPTY_RESULT EQUAL 0)
    message(FATAL_ERROR
        "empty scanner discovery-only CLI failed: ${EMPTY_RESULT}\n${EMPTY_OUTPUT}\n${EMPTY_ERROR}")
endif()

file(SIZE "${CACHE_FILE}" EMPTY_CACHE_SIZE)
if(NOT EMPTY_CACHE_SIZE EQUAL 0)
    message(FATAL_ERROR
        "zero discovered bundles must atomically publish an empty cache")
endif()

message(STATUS "scanner discovery-only CLI replacement behavior passed")
