# BOOX Picco keyboard touch: the screen-point-to-key mapping in
# src/components/KeyboardTouch.h. Links the SDK's keyboard component and layout
# tables (FreeInkUI.cpp) plus the project's shared layout headers, the same set
# KeyboardPanelTest links. No renderer, no Arduino, no device.
add_executable(KeyboardTouchTest
  KeyboardTouchTest.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp
)
target_include_directories(KeyboardTouchTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
)
target_link_libraries(KeyboardTouchTest PRIVATE GTest::gtest_main)
add_test(NAME KeyboardTouchTest COMMAND KeyboardTouchTest)
