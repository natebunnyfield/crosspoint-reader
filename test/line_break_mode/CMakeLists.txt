add_executable(LineBreakModeTest
  LineBreakModeTest.cpp
)

target_include_directories(LineBreakModeTest PRIVATE
  ${REPO_ROOT}/lib/Epub/Epub
)

target_link_libraries(LineBreakModeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LineBreakModeTest)
