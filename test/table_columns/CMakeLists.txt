add_executable(TableColumnLayoutTest
  TableColumnLayoutTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/TableColumnLayout.cpp
)

target_include_directories(TableColumnLayoutTest PRIVATE
  ${REPO_ROOT}/lib/Epub/Epub/parsers
)

target_link_libraries(TableColumnLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TableColumnLayoutTest)
