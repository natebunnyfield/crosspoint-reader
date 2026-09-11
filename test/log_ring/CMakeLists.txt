# The 16-line crash log ring, and specifically its duplicate collapsing.
#
# The ring IS the crash report: HalSystem dumps it verbatim. B-040's report is
# thirteen identical allocation failures out of sixteen slots, so everything
# that led to the crash had been pushed out -- the one report of a crash that
# happens in daily use explained nothing. These tests pin the collapsing that
# fixes that.
add_executable(LogRingTest
  LogRingTest.cpp
  ${REPO_ROOT}/lib/Logging/Logging.cpp
)

target_include_directories(LogRingTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
target_include_directories(LogRingTest PRIVATE
  ${REPO_ROOT}/lib/Logging
)

target_link_libraries(LogRingTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LogRingTest)
