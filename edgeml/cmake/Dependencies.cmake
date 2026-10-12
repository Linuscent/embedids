# Third-party dependencies for the EdgeML agent and simulator.
#
# The EmbedIDS core library itself stays dependency-free; these are only used
# by the C++ agent, simulator, REST API and tests.

include(FetchContent)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# --- nlohmann/json -----------------------------------------------------------
find_package(nlohmann_json 3.10 QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    )
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_Install OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# --- yaml-cpp -----------------------------------------------------------------
find_package(yaml-cpp 0.7 QUIET)
if(NOT yaml-cpp_FOUND)
    FetchContent_Declare(yaml-cpp
        URL https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-0.9.0.tar.gz
        URL_HASH SHA256=25cb043240f828a8c51beb830569634bc7ac603978e0f69d6b63558dadefd49a
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
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.60.1.tar.gz
    URL_HASH SHA256=f42fc046e55f968a9dd43b272251f033b08538d3e359235608f980f551053215
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
        URL https://www.sqlite.org/2026/sqlite-amalgamation-3540000.zip
        URL_HASH SHA256=68e913b0fe8ec6b4e5f391c594a28872884e80d6cd042a1e50c957be46b5aa4d
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
