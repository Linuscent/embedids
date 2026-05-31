cmake_minimum_required(VERSION 3.16)

project(EmbedIDS 
    VERSION 0.1.0
    DESCRIPTION "Lightweight Runtime Intrusion Detection SDK for embedded IoT devices"
    LANGUAGES C CXX
)

# Standards
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(BUILD_TESTS "Build unit tests" OFF)
option(BUILD_EXAMPLES "Build examples" OFF)
option(ENABLE_COVERAGE "Enable code coverage" OFF)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Os -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Os -ffunction-sections -fdata-sections")

# Coverage configuration
if(ENABLE_COVERAGE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    include(CTest)
    message(STATUS "Enabling code coverage with ${CMAKE_C_COMPILER_ID}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g --coverage")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g --coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
elseif(ENABLE_COVERAGE)
    message(WARNING "Coverage only supported with GCC or Clang")
endif()

# Configure main header with version info
configure_file(include/embedids.h.in ${CMAKE_CURRENT_BINARY_DIR}/include/embedids.h @ONLY)

# Include directories
include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)

# Build targets
add_subdirectory(src)
add_library(EmbedIDS::embedids ALIAS embedids)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Installation
install(TARGETS embedids
    ARCHIVE DESTINATION lib
    PUBLIC_HEADER DESTINATION include
)

install(FILES 
    ${CMAKE_CURRENT_BINARY_DIR}/include/embedids.h
    DESTINATION include
)

# Package configuration
include(CMakePackageConfigHelpers)
write_basic_package_version_file("EmbedIDSConfigVersion.cmake" 
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
configure_package_config_file("cmake/EmbedIDSConfig.cmake.in" "EmbedIDSConfig.cmake" 
    INSTALL_DESTINATION lib/cmake/EmbedIDS)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/EmbedIDSConfig.cmake" 
    "${CMAKE_CURRENT_BINARY_DIR}/EmbedIDSConfigVersion.cmake" 
    DESTINATION lib/cmake/EmbedIDS)

# Coverage targets
if(ENABLE_COVERAGE AND BUILD_TESTS)
    add_custom_target(coverage
        COMMAND ${CMAKE_CTEST_COMMAND} -T Test -T Coverage --output-on-failure
        DEPENDS embedids_tests
        COMMENT "Running tests and generating coverage report"
    )
    message(STATUS "Coverage target added: make coverage")
endif()
