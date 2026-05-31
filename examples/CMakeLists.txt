cmake_minimum_required(VERSION 3.16)

project(EmbedIDS_Examples
    VERSION 1.0.0
    DESCRIPTION "Examples for EmbedIDS - Embedded Intrusion Detection System"
    LANGUAGES C
)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# Include FetchContent module
include(FetchContent)

# Try to find EmbedIDS package first
find_package(EmbedIDS QUIET)

if(NOT TARGET EmbedIDS::embedids)
    # If we're in the source tree, use local source
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../src/CMakeLists.txt")
        message(STATUS "Using local EmbedIDS source")
        
        # Disable building examples in the fetched content to avoid conflicts
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        
        FetchContent_Declare(
            EmbedIDS_Local
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..
        )
        FetchContent_MakeAvailable(EmbedIDS_Local)
    else()
        message(FATAL_ERROR "EmbedIDS library not found. Please install EmbedIDS or build from the EmbedIDS source tree.")
    endif()
endif()

# Define example targets
set(EXAMPLE_TARGETS
    embedids_example
    user_application_example
    extensible_example
    tutorial_example
)

set(EXAMPLE_SOURCES
    simple_example.c
    user_application.c
    extensible_example.c
    tutorial_example.c
)

# Create executables
list(LENGTH EXAMPLE_TARGETS num_targets)
math(EXPR last_index "${num_targets} - 1")

foreach(i RANGE ${last_index})
    list(GET EXAMPLE_TARGETS ${i} target)
    list(GET EXAMPLE_SOURCES ${i} source)
    
    add_executable(${target} ${source})
    target_link_libraries(${target} embedids)
    
    # Add math library for examples that need it
    if(target MATCHES "user_application|extensible")
        target_link_libraries(${target} m)
    endif()
endforeach()

# Installation rules
install(TARGETS ${EXAMPLE_TARGETS}
    RUNTIME DESTINATION bin/examples
)

install(FILES ${EXAMPLE_SOURCES} CMakeLists.txt
    DESTINATION share/embedids/examples
)
