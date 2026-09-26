add_executable(UpdateProgressTest
  UpdateProgressTest.cpp
)

target_include_directories(UpdateProgressTest PRIVATE
  ${REPO_ROOT}/src/network
)

target_link_libraries(UpdateProgressTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(UpdateProgressTest)

# The host (SIMULATOR) path of UpdateWorker: a real std::thread.
add_executable(UpdateWorkerTest
  UpdateWorkerTest.cpp
)
target_include_directories(UpdateWorkerTest PRIVATE ${REPO_ROOT}/src)
target_compile_definitions(UpdateWorkerTest PRIVATE SIMULATOR REPO_ROOT_PATH="${REPO_ROOT}")
target_link_libraries(UpdateWorkerTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)
gtest_discover_tests(UpdateWorkerTest)
