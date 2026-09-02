if(NOT WIN32)
    return()
endif()

add_executable(obs-safe-vst3-rack-host
    main.cpp
    ${PROJECT_SOURCE_DIR}/src/host/hosted_plugin.cpp
)
target_include_directories(obs-safe-vst3-rack-host PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(obs-safe-vst3-rack-host PRIVATE
    safevst3_vst3_hosting_runtime
    safevst3_lifecycle_policy
    safevst3_state_restore_policy
    safevst3_latency_restart_transaction
    safevst3_parameter_refresh_transaction
    safevst3_io_restart_transaction
    safevst3_reload_component_transaction
    safevst3_process_context_policy
)
target_compile_definitions(obs-safe-vst3-rack-host PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
