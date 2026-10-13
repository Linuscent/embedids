# EmbedIDS EdgeML Runtime Security MVP
#
# Libraries (dependency direction is strictly downward):
#   edgeml_core        schema, config, SHA-256, EmbedIDS bridge, detection,
#                      correlation. Depends only on the EmbedIDS C core.
#   edgeml_sim         EdgeML workload simulator models.
#   edgeml_collectors  Collector HAL + simulation / Linux / Jetson backends.
#   edgeml_agent       Agent pipeline, alert sinks, storage, REST API.

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake)

set(EDGEML_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# --- core --------------------------------------------------------------------
add_library(edgeml_core STATIC
    core/common/clock.cpp
    core/schema/telemetry.cpp
    core/integrity/sha256.c
    core/alert/alert.cpp
    core/config/config.cpp
    core/embedids/signal_engine.cpp
    core/detection/policy.cpp
    core/detection/detection_engine.cpp
    core/correlation/correlator.cpp
)
target_include_directories(edgeml_core PUBLIC ${EDGEML_ROOT})
target_link_libraries(edgeml_core PUBLIC embedids nlohmann_json::nlohmann_json)
target_compile_features(edgeml_core PUBLIC cxx_std_17)

# --- simulator models ----------------------------------------------------------
add_library(edgeml_sim STATIC
    simulator/attacks/scenarios.cpp
    simulator/gpu/gpu_model.cpp
    simulator/inference/inference_model.cpp
    simulator/network/network_model.cpp
    simulator/process/process_model.cpp
    simulator/workload/system_model.cpp
    simulator/workload/snapshot.cpp
    simulator/workload/model_file.cpp
    simulator/workload/edge_device_simulator.cpp
    simulator/workload/control_server.cpp
)
target_link_libraries(edgeml_sim PUBLIC edgeml_core edgeml_httplib)

# --- collectors ------------------------------------------------------------------
add_library(edgeml_collectors STATIC
    collectors/common/file_integrity_collector.cpp
    collectors/common/agent_self_collector.cpp
    collectors/simulation/simulation_feed.cpp
    collectors/simulation/simulated_collectors.cpp
    collectors/linux/proc_parsers.cpp
    collectors/linux/linux_collectors.cpp
    collectors/jetson/tegrastats_parser.cpp
    collectors/jetson/jetson_collectors.cpp
    collectors/collector_factory.cpp
)
target_link_libraries(edgeml_collectors PUBLIC edgeml_core edgeml_sim edgeml_httplib)

# --- agent -----------------------------------------------------------------------
add_library(edgeml_agent STATIC
    agent/config_loader.cpp
    agent/telemetry_manager.cpp
    agent/alert_manager.cpp
    agent/alert_sinks.cpp
    agent/event_store.cpp
    agent/agent.cpp
    api/rest/api_server.cpp
)
target_link_libraries(edgeml_agent PUBLIC
    edgeml_collectors yaml-cpp::yaml-cpp edgeml_sqlite edgeml_httplib)

# --- executables --------------------------------------------------------------------
add_executable(embedids-agent agent/main.cpp)
target_link_libraries(embedids-agent PRIVATE edgeml_agent)

add_executable(edgeml-simulator simulator/main.cpp)
target_link_libraries(edgeml-simulator PRIVATE edgeml_sim edgeml_httplib)

if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE MATCHES "Release|MinSizeRel")
    foreach(target embedids-agent edgeml-simulator)
        target_link_options(${target} PRIVATE -Wl,--gc-sections)
    endforeach()
endif()

install(TARGETS embedids-agent edgeml-simulator RUNTIME DESTINATION bin)
install(DIRECTORY configs/ DESTINATION share/embedids/configs)
install(DIRECTORY dashboard/frontend/ DESTINATION share/embedids/dashboard)

# --- tests -------------------------------------------------------------------------
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
