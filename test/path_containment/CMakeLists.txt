# FsHelpers::isSameOrInside: the guard that stops a folder being moved into its
# own subtree (WebDAV MOVE, Manage Files), which SdFat turns into a detached
# directory loop. Pure string logic.
add_executable(PathContainmentTest
  PathContainmentTest.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
)
target_include_directories(PathContainmentTest PRIVATE
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/test/host_stubs
)
target_link_libraries(PathContainmentTest PRIVATE GTest::gtest_main)
add_test(NAME PathContainmentTest COMMAND PathContainmentTest)
