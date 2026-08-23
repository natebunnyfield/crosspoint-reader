add_executable(LibrarySyncPlanTest
  LibrarySyncPlanTest.cpp
)

target_include_directories(LibrarySyncPlanTest PRIVATE
  ${REPO_ROOT}/src/network
)

target_link_libraries(LibrarySyncPlanTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LibrarySyncPlanTest)
