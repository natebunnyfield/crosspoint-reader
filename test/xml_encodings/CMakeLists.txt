add_executable(XmlEncodingsTest
  XmlEncodingsTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/XmlEncodingTables.cpp
)

target_include_directories(XmlEncodingsTest PRIVATE
  ${REPO_ROOT}/lib/Epub/Epub/parsers
)

target_link_libraries(XmlEncodingsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(XmlEncodingsTest)
