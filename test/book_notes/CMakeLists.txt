add_executable(BookNotesTest
  BookNotesTest.cpp
)

target_include_directories(BookNotesTest PRIVATE
  ${REPO_ROOT}/lib/Epub
)

target_link_libraries(BookNotesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookNotesTest)
