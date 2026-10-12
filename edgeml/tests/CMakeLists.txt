set(EDGEML_TEST_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# GoogleTest: reuse the repository's copy when tests/ already fetched it.
if(NOT TARGET gtest_main AND NOT TARGET GTest::gtest_main)
    find_package(GTest QUIET)
endif()
if(TARGET GTest::gtest_main)
    set(EDGEML_GTEST GTest::gtest GTest::gtest_main)
elseif(TARGET gtest_main)
    set(EDGEML_GTEST gtest gtest_main)
else()
    include(FetchContent)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.18.0
    )
    FetchContent_MakeAvailable(googletest)
    set(EDGEML_GTEST gtest gtest_main)
endif()

add_executable(edgeml_unit_tests
    unit/test_sha256.cpp
    unit/test_schema.cpp
    unit/test_config.cpp
    unit/test_signal_engine.cpp
    unit/test_detection_rules.cpp
    unit/test_correlator.cpp
    unit/test_telemetry_manager.cpp
    unit/test_file_integrity.cpp
    unit/test_proc_parsers.cpp
    unit/test_tegrastats.cpp
    unit/test_simulator.cpp
    unit/test_event_store.cpp
    unit/test_alert_sinks.cpp
)
target_include_directories(edgeml_unit_tests PRIVATE ${EDGEML_TEST_ROOT})
target_link_libraries(edgeml_unit_tests PRIVATE edgeml_agent ${EDGEML_GTEST})

add_executable(edgeml_integration_tests
    integration/test_scenarios.cpp
    integration/test_api.cpp
    integration/test_http_simulation.cpp
)
target_include_directories(edgeml_integration_tests PRIVATE ${EDGEML_TEST_ROOT})
target_link_libraries(edgeml_integration_tests PRIVATE edgeml_agent ${EDGEML_GTEST})

add_test(NAME edgeml_unit_tests COMMAND edgeml_unit_tests)
add_test(NAME edgeml_integration_tests COMMAND edgeml_integration_tests)
set_tests_properties(edgeml_unit_tests edgeml_integration_tests
    PROPERTIES LABELS "edgeml")

# Real-process end-to-end run: simulator + agent over HTTP, attack scripts.
find_program(EDGEML_CURL curl)
find_program(EDGEML_PYTHON3 python3)
if(EDGEML_CURL AND EDGEML_PYTHON3)
    add_test(NAME edgeml_e2e_attack_scenarios
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/attack-scenarios/run_local_e2e.sh
                $<TARGET_FILE:edgeml-simulator> $<TARGET_FILE:embedids-agent>)
    set_tests_properties(edgeml_e2e_attack_scenarios PROPERTIES
        LABELS "edgeml;e2e" TIMEOUT 300)
endif()

foreach(target edgeml_unit_tests edgeml_integration_tests)
    target_compile_definitions(${target} PRIVATE EDGEML_SOURCE_DIR="${EDGEML_ROOT}")
endforeach()
