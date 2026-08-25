# How much text was on the page -- the reading ledger's denominator.
# PageTextMetrics.h is pure and header-only (it reaches only
# readaloud::tokenIsBlank, itself header-only), so this links one test file and
# nothing else.
add_executable(PageTextMetricsTest
  PageTextMetricsTest.cpp
)
target_include_directories(PageTextMetricsTest PRIVATE
  ${REPO_ROOT}/src/activities/reader
)
target_link_libraries(PageTextMetricsTest PRIVATE GTest::gtest_main)
add_test(NAME PageTextMetricsTest COMMAND PageTextMetricsTest)
