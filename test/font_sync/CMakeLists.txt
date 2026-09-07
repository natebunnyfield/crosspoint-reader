add_executable(FontSyncPlanTest
  FontSyncPlanTest.cpp
)

target_include_directories(FontSyncPlanTest PRIVATE
  ${REPO_ROOT}/src/network
)

target_link_libraries(FontSyncPlanTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FontSyncPlanTest)
