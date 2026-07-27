add_executable(HolidayCalculatorTest
  HolidayCalculatorTest.cpp
  ${REPO_ROOT}/src/activities/boot_sleep/HolidayCalculator.cpp
)

target_link_libraries(HolidayCalculatorTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HolidayCalculatorTest)
