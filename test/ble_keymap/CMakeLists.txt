# HID boot-keyboard decode suite.
#
# Links src/spike/HidKeymap.h only — no Arduino, no NimBLE, no radio. This is
# the part of the BLE path that is pure logic, so it is the part that can be
# verified without a keyboard or a device.
add_executable(BleKeymapTest BleKeymapTest.cpp)
target_include_directories(BleKeymapTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(BleKeymapTest PRIVATE GTest::gtest_main)
add_test(NAME BleKeymapTest COMMAND BleKeymapTest)
