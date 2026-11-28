# Upstream 87f9c34c6's suite. Fork: no HalMemory unit (see ../inflate_stream),
# so HalMemory.cpp and the hal include dir are dropped.
add_executable(ZipFileTest
  ZipFileTest.cpp
  ${REPO_ROOT}/lib/ZipFile/ZipFile.cpp
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
)
target_include_directories(ZipFileTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/ZipFile
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/miniz/src
)
target_link_libraries(ZipFileTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ZipFileTest)
