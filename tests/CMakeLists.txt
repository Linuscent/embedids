# Find or fetch GoogleTest
find_package(GTest QUIET)

if(NOT GTest_FOUND)
    # If GoogleTest is not found, fetch it
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.17.0
    )
    FetchContent_MakeAvailable(googletest)
endif()

# Create test executable with organized test files
add_executable(embedids_tests
    test_core.cpp
    test_metrics.cpp
    test_algorithms.cpp
    test_analysis.cpp
    test_extensible.cpp
)

# Link test executable with library and gtest
target_link_libraries(embedids_tests
    embedids
    gtest
    gtest_main
)

# Add tests to CTest
add_test(NAME embedids_tests COMMAND embedids_tests)

# Enable test coverage if requested
option(ENABLE_COVERAGE "Enable test coverage" OFF)
if(ENABLE_COVERAGE)
    target_compile_options(embedids_tests PRIVATE --coverage)
    target_link_options(embedids_tests PRIVATE --coverage)
endif()

# Add individual test suites that can be run separately
add_test(NAME core_tests COMMAND embedids_tests --gtest_filter="EmbedIDSCoreTest.*")
add_test(NAME metrics_tests COMMAND embedids_tests --gtest_filter="EmbedIDSMetricsTest.*")
add_test(NAME algorithms_tests COMMAND embedids_tests --gtest_filter="EmbedIDSAlgorithmsTest.*")
add_test(NAME analysis_tests COMMAND embedids_tests --gtest_filter="EmbedIDSAnalysisTest.*")
add_test(NAME extensible_tests COMMAND embedids_tests --gtest_filter="EmbedIDSExtensibleTest.*")
