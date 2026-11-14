add_executable(FontPreviewLinesTest
  FontPreviewLinesTest.cpp
)

target_include_directories(FontPreviewLinesTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(FontPreviewLinesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FontPreviewLinesTest)
