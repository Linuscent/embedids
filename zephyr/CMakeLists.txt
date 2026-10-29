# SPDX-License-Identifier: Apache-2.0
if(CONFIG_EMBEDIDS)
  # Do not add the standalone root project: Zephyr owns flags and languages.
  # Scope the template's PROJECT_VERSION variables to this directory.
  file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../CMakeLists.txt" version_line
       REGEX "^[ \t]*VERSION [0-9]+\\.[0-9]+\\.[0-9]+")
  string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.([0-9]+)" PROJECT_VERSION "${version_line}")
  if(NOT PROJECT_VERSION)
    message(FATAL_ERROR "Cannot determine EmbedIDS version")
  endif()
  set(PROJECT_VERSION_MAJOR "${CMAKE_MATCH_1}")
  set(PROJECT_VERSION_MINOR "${CMAKE_MATCH_2}")
  set(PROJECT_VERSION_PATCH "${CMAKE_MATCH_3}")
  configure_file(../include/embedids.h.in
                 ${CMAKE_CURRENT_BINARY_DIR}/include/embedids.h @ONLY)

  zephyr_include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)
  zephyr_compile_definitions(
    EMBEDIDS_MAX_METRICS=${CONFIG_EMBEDIDS_MAX_METRICS}
    EMBEDIDS_MAX_METRIC_NAME_LEN=${CONFIG_EMBEDIDS_MAX_METRIC_NAME_LEN}
    EMBEDIDS_MAX_ALGORITHMS_PER_METRIC=${CONFIG_EMBEDIDS_MAX_ALGORITHMS_PER_METRIC}
    EMBEDIDS_ENABLE_FLOATING_POINT=$<BOOL:${CONFIG_EMBEDIDS_FLOATING_POINT}>
    EMBEDIDS_ENABLE_DOUBLE_PRECISION=$<BOOL:${CONFIG_EMBEDIDS_DOUBLE_PRECISION}>)
  zephyr_library()
  zephyr_library_sources(../src/embedids.c)
  if(CONFIG_EMBEDIDS_ZEPHYR_ADAPTER)
    zephyr_include_directories(../ports/zephyr/include)
    zephyr_library_sources(../ports/zephyr/embedids_zephyr.c)
  endif()
endif()
