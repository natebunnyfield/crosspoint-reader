add_executable(SpacedSignWhiteTest
  SpacedSignWhiteTest.cpp
)

target_link_libraries(SpacedSignWhiteTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SpacedSignWhiteTest)
