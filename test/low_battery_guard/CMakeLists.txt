# The sustained-empty-battery decision behind the clean low-battery sleep.
# Header-only and pure: no HAL, no Arduino.
add_executable(LowBatteryGuardTest LowBatteryGuardTest.cpp)
target_link_libraries(LowBatteryGuardTest PRIVATE crosspoint_test_common GTest::gtest_main)
add_test(NAME LowBatteryGuardTest COMMAND LowBatteryGuardTest)
