# ProgressFile::writeAtomic over an in-memory card that counts operations.
# The point under test is the page-turn cost: a same-length save must touch
# only the file's own data sector, never remove/rename/truncate (each of
# which is a FAT write with a reset window on this target). Header-only
# firmware code; stubs/ supplies HalStorage and Logging.
add_executable(ProgressFileTest ProgressFileTest.cpp)
target_include_directories(ProgressFileTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
target_include_directories(ProgressFileTest PRIVATE
  ${REPO_ROOT}/src
)
target_link_libraries(ProgressFileTest PRIVATE crosspoint_test_common GTest::gtest_main)
add_test(NAME ProgressFileTest COMMAND ProgressFileTest)
