# Which extensions the plain-text viewer opens. Pure string logic — FsHelpers
# needs no SD, no Arduino and no renderer for these predicates.
add_executable(PlainTextFilesTest
  PlainTextFilesTest.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
)
target_include_directories(PlainTextFilesTest PRIVATE
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/test/host_stubs
)
target_link_libraries(PlainTextFilesTest PRIVATE GTest::gtest_main)
add_test(NAME PlainTextFilesTest COMMAND PlainTextFilesTest)
