add_executable(FontActivationTest
  FontActivationTest.cpp
)

target_include_directories(FontActivationTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(FontActivationTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FontActivationTest)
