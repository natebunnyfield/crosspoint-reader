add_executable(HomeLandingTest
  HomeLandingTest.cpp
)

target_include_directories(HomeLandingTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(HomeLandingTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HomeLandingTest)
