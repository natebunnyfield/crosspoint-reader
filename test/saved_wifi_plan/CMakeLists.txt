# The saved-network join policy. Header-only and clock-free, so this links
# nothing: the caller supplies the observations and the elapsed times.
add_executable(SavedWifiPlanTest SavedWifiPlanTest.cpp)
target_include_directories(SavedWifiPlanTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(SavedWifiPlanTest PRIVATE GTest::gtest_main)
add_test(NAME SavedWifiPlanTest COMMAND SavedWifiPlanTest)
