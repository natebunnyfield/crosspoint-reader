# Markdown-lite span analysis. Pure logic: no renderer, no Arduino, no device.
add_executable(MarkdownSpansTest MarkdownSpansTest.cpp)
target_include_directories(MarkdownSpansTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(MarkdownSpansTest PRIVATE GTest::gtest_main)
add_test(NAME MarkdownSpansTest COMMAND MarkdownSpansTest)
