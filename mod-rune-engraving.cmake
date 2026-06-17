# mod-rune-engraving — build hook (included inline by modules/CMakeLists.txt).
#
# Registers this module's unit tests with the core's `unit_tests` target when the
# server is configured with -DBUILD_TESTING=ON. The core's src/test/CMakeLists.txt
# reads these GLOBAL properties (and links the `modules` library), so the tests
# compile into unit_tests with no core edits. A no-op otherwise.
#
# Test sources live in tests/ — a sibling of src/, so they are NOT collected into
# the `modules` library itself (CollectSourceFiles globs only <module>/src).

if(BUILD_TESTING)
  set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/tests/test_rune_rules.cpp")
  set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_INCLUDES
    "${CMAKE_CURRENT_LIST_DIR}/src")
  message(STATUS "mod-rune-engraving: registered unit tests")
endif()
