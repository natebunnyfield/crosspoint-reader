# The real CssParser.cpp, host-side. Nothing else in test/ compiled it, which is
# how `margin: 1cm` could mean one pixel for the life of the project: the unit
# fall-through was in the one file with no host coverage.
add_executable(CssUnitsTest
  CssUnitsTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BookNotes.cpp
)

# stubs/ BEFORE the library paths so <Arduino.h>, <Logging.h> and <HalStorage.h>
# resolve to the local minimums rather than to ESP-IDF.
target_include_directories(CssUnitsTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
target_include_directories(CssUnitsTest PRIVATE
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Logging
)

target_link_libraries(CssUnitsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CssUnitsTest)
