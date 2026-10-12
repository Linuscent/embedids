# Third-party dependencies for the EdgeML agent and simulator.
#
# The EmbedIDS core library itself stays dependency-free; these are only used
# by the C++ agent, simulator, REST API and tests.

include(FetchContent)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# yaml-cpp 0.8.0 declares cmake_minimum_required(VERSION 3.4), which CMake 4.x
# rejects unless a policy floor is provided.
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
endif()

# --- nlohmann/json -----------------------------------------------------------
find_package(nlohmann_json 3.10 QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    )
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_Install OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# --- yaml-cpp -----------------------------------------------------------------
find_package(yaml-cpp 0.7 QUIET)
if(NOT yaml-cpp_FOUND)
    FetchContent_Declare(yaml-cpp
        URL https://github.com/jbeder/yaml-cpp/archive/refs/tags/0.8.0.tar.gz
        URL_HASH SHA256=fbe74bbdcee21d656715688706da3c8becfd946d92cd44705cc6098bb23b3a16
    )
    set(YAML_CPP_BUILD_TESTS OFF CACHE INTERNAL "")
    set(YAML_CPP_BUILD_TOOLS OFF CACHE INTERNAL "")
    set(YAML_CPP_BUILD_CONTRIB OFF CACHE INTERNAL "")
    set(YAML_CPP_INSTALL OFF CACHE INTERNAL "")
    set(YAML_BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(yaml-cpp)
endif()
if(NOT TARGET yaml-cpp::yaml-cpp)
    add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
endif()

# --- cpp-httplib (header only) ------------------------------------------------
FetchContent_Declare(httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.3.tar.gz
    URL_HASH SHA256=a0567bcd6c3fe5cef1b329b96245119047f876b49e06cc129a36a7a8dffe173e
    # Header-only: populate without running its CMake project.
    SOURCE_SUBDIR do-not-configure
)
FetchContent_MakeAvailable(httplib)
find_package(Threads REQUIRED)
add_library(edgeml_httplib INTERFACE)
target_include_directories(edgeml_httplib SYSTEM INTERFACE ${httplib_SOURCE_DIR})
target_link_libraries(edgeml_httplib INTERFACE Threads::Threads)

# --- SQLite --------------------------------------------------------------------
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    add_library(edgeml_sqlite INTERFACE)
    target_link_libraries(edgeml_sqlite INTERFACE SQLite::SQLite3)
else()
    message(STATUS "System SQLite3 not found - building bundled amalgamation")
    FetchContent_Declare(sqlite_amalgamation
        URL https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip
        URL_HASH SHA256=77823cb110929c2bcb0f5d48e4833b5c59a8a6e40cdea3936b99e199dbbe5784
        SOURCE_SUBDIR do-not-configure
    )
    FetchContent_MakeAvailable(sqlite_amalgamation)
    add_library(edgeml_sqlite STATIC ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c)
    target_include_directories(edgeml_sqlite SYSTEM PUBLIC ${sqlite_amalgamation_SOURCE_DIR})
    target_compile_definitions(edgeml_sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)
    # Third-party code: do not inherit the project's warning flags.
    target_compile_options(edgeml_sqlite PRIVATE -w)
    target_link_libraries(edgeml_sqlite PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()
