add_executable(AutoJustifyTest
  AutoJustifyTest.cpp
)

target_include_directories(AutoJustifyTest PRIVATE
  ${REPO_ROOT}/lib/Epub/Epub
)

target_link_libraries(AutoJustifyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AutoJustifyTest)
