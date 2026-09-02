add_executable(ButtonHoldTimerTest
  ButtonHoldTimerTest.cpp
)

target_include_directories(ButtonHoldTimerTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ButtonHoldTimerTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ButtonHoldTimerTest)
