# Dark-mode polarity + glyph antialiasing split.
#
# Both units under test are header-only and free of Arduino, the SDK and the
# renderer, deliberately: the decisions they encode are the ones that cannot be
# checked anywhere else off-device.

add_executable(DarkModePolarityTest
  DarkModePolarityTest.cpp
)

target_include_directories(DarkModePolarityTest PRIVATE
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/GfxRenderer
)

target_link_libraries(DarkModePolarityTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DarkModePolarityTest)
