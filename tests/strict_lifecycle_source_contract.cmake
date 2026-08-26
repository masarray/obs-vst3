if(NOT DEFINED ENGINE_SOURCE OR NOT DEFINED MAIN_SOURCE OR NOT DEFINED WIN_IPC_SOURCE)
  message(FATAL_ERROR "ENGINE_SOURCE, MAIN_SOURCE and WIN_IPC_SOURCE are required")
endif()

file(READ "${ENGINE_SOURCE}" ENGINE)
file(READ "${MAIN_SOURCE}" MAIN)
file(READ "${WIN_IPC_SOURCE}" WIN_IPC)

string(FIND "${ENGINE}" "new PlugProvider" PROVIDER_POS)
if(NOT PROVIDER_POS EQUAL -1)
  message(FATAL_ERROR "Runtime engine must not delegate strict lifecycle ordering to PlugProvider")
endif()

string(FIND "${ENGINE}" "PluginContextFactory::instance().setPluginContext(host_.get())" HOST_CONTEXT)
string(FIND "${ENGINE}" "component_->initialize(host_.get())" COMPONENT_INIT)
string(FIND "${ENGINE}" "controller_->setComponentHandler(component_handler)" HANDLER)
string(FIND "${ENGINE}" "component_connection_->connect(controller_cp)" CONNECT_FORWARD)
string(FIND "${ENGINE}" "controller_connection_->connect(component_cp)" CONNECT_REVERSE)
string(FIND "${ENGINE}" "MemoryStream initial_component_state;" INITIAL_STATE_STREAM)
string(FIND "${ENGINE}" "component_->getState(&initial_component_state)" INITIAL_STATE_GET)
string(FIND "${ENGINE}" "initial_component_state.rewind()" INITIAL_STATE_REWIND)
string(FIND "${ENGINE}" "controller_->setComponentState(&initial_component_state)" INITIAL_STATE_SET)
string(FIND "${ENGINE}" "if (!enumerate_parameters(error))" PARAM_ENUM)
string(FIND "${ENGINE}" "processor_->setupProcessing(process_setup_)" SETUP)
string(FIND "${ENGINE}" "activate_configured_buses(error)" ACTIVATE_BUSES)
string(FIND "${ENGINE}" "component_->setActive(true)" SET_ACTIVE)
if(HOST_CONTEXT LESS 0 OR COMPONENT_INIT LESS 0 OR HANDLER LESS 0 OR CONNECT_FORWARD LESS 0 OR CONNECT_REVERSE LESS 0 OR
   INITIAL_STATE_STREAM LESS 0 OR INITIAL_STATE_GET LESS 0 OR INITIAL_STATE_REWIND LESS 0 OR INITIAL_STATE_SET LESS 0 OR
   PARAM_ENUM LESS 0 OR SETUP LESS 0 OR ACTIVATE_BUSES LESS 0 OR SET_ACTIVE LESS 0)
  message(FATAL_ERROR "Could not find strict lifecycle markers")
endif()
if(NOT (HOST_CONTEXT LESS COMPONENT_INIT AND COMPONENT_INIT LESS HANDLER AND HANDLER LESS CONNECT_FORWARD AND
        CONNECT_FORWARD LESS CONNECT_REVERSE AND CONNECT_REVERSE LESS INITIAL_STATE_STREAM AND
        INITIAL_STATE_STREAM LESS INITIAL_STATE_GET AND INITIAL_STATE_GET LESS INITIAL_STATE_REWIND AND
        INITIAL_STATE_REWIND LESS INITIAL_STATE_SET AND INITIAL_STATE_SET LESS PARAM_ENUM AND
        PARAM_ENUM LESS SETUP AND SETUP LESS ACTIVATE_BUSES AND ACTIVATE_BUSES LESS SET_ACTIVE))
  message(FATAL_ERROR "Strict lifecycle ordering regressed: initialize -> handler -> connect -> component-state sync -> parameter scan -> setup -> bus activation -> active")
endif()

string(FIND "${ENGINE}" "bool Vst3Engine::configure_buses" CONFIG_BEGIN)
string(FIND "${ENGINE}" "bool Vst3Engine::activate_configured_buses" ACTIVATE_BEGIN)
if(CONFIG_BEGIN LESS 0 OR ACTIVATE_BEGIN LESS 0 OR NOT CONFIG_BEGIN LESS ACTIVATE_BEGIN)
  message(FATAL_ERROR "Could not isolate initial bus negotiation")
endif()
math(EXPR CONFIG_LEN "${ACTIVATE_BEGIN} - ${CONFIG_BEGIN}")
string(SUBSTRING "${ENGINE}" ${CONFIG_BEGIN} ${CONFIG_LEN} CONFIG_BODY)
string(FIND "${CONFIG_BODY}" "activateBus(" EARLY_ACTIVATE)
if(NOT EARLY_ACTIVATE EQUAL -1)
  message(FATAL_ERROR "Initial bus negotiation must not activate buses before setupProcessing")
endif()

string(FIND "${MAIN}" "ComponentHandler component_handler(" MAIN_HANDLER)
string(FIND "${MAIN}" "Vst3Engine engine;" MAIN_ENGINE)
string(FIND "${MAIN}" "&component_handler, error" MAIN_OPEN_HANDLER)
if(MAIN_HANDLER LESS 0 OR MAIN_ENGINE LESS 0 OR MAIN_OPEN_HANDLER LESS 0 OR
   NOT (MAIN_HANDLER LESS MAIN_ENGINE AND MAIN_ENGINE LESS MAIN_OPEN_HANDLER))
  message(FATAL_ERROR "Helper must create ComponentHandler before initial Vst3Engine::open")
endif()

string(FIND "${MAIN}" "&component_handler_, error" RELOAD_HANDLER)
if(RELOAD_HANDLER LESS 0)
  message(FATAL_ERROR "Full reload must pass the handler into the recreated VST3 lifecycle")
endif()

string(FIND "${ENGINE}" "PluginContextFactory::instance().setPluginContext(nullptr)" CLEAR_HOST_CONTEXT)
string(FIND "${ENGINE}" "component_connection_->disconnect()" DISCONNECT_COMPONENT)
string(FIND "${ENGINE}" "controller_connection_->disconnect()" DISCONNECT_CONTROLLER)
string(FIND "${ENGINE}" "controller_->terminate()" TERMINATE_CONTROLLER)
string(FIND "${ENGINE}" "component_->terminate()" TERMINATE_COMPONENT)
if(CLEAR_HOST_CONTEXT LESS 0 OR DISCONNECT_COMPONENT LESS 0 OR DISCONNECT_CONTROLLER LESS 0 OR TERMINATE_CONTROLLER LESS 0 OR TERMINATE_COMPONENT LESS 0)
  message(FATAL_ERROR "Strict close lifecycle markers missing")
endif()
if(NOT (DISCONNECT_COMPONENT LESS TERMINATE_COMPONENT AND DISCONNECT_CONTROLLER LESS TERMINATE_COMPONENT AND
        TERMINATE_COMPONENT LESS TERMINATE_CONTROLLER))
  message(FATAL_ERROR "Strict close must disconnect before Steinberg-compatible component -> controller termination")
endif()

message(STATUS "Strict separated-component VST3 lifecycle source contract ok")