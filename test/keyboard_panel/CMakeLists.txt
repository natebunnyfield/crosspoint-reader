# KeyboardPanel key semantics. Links the SDK's layout tables (FreeInkUI.cpp
# holds builtinKeyboardLayout / keyboardOutputFor) plus the project's shared
# layout headers. No renderer, no Arduino, no device.
add_executable(KeyboardPanelTest
  KeyboardPanelTest.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp
)
target_include_directories(KeyboardPanelTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
)
target_link_libraries(KeyboardPanelTest PRIVATE GTest::gtest_main)
add_test(NAME KeyboardPanelTest COMMAND KeyboardPanelTest)
