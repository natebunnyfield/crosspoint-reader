# Editor Font specimen + availability. Pure table and span logic — MarkdownSpans
# is header-only and EditorFonts has no renderer, Arduino or SD dependency, so
# this links one file and nothing else. The DRAWING is not tested here: that
# needs a renderer, and is verified on the simulator instead.
add_executable(EditorFontPreviewTest
  EditorFontPreviewTest.cpp
  ${REPO_ROOT}/src/notes/EditorFonts.cpp
)
target_include_directories(EditorFontPreviewTest PRIVATE
  ${REPO_ROOT}/src
)
target_link_libraries(EditorFontPreviewTest PRIVATE GTest::gtest_main)
add_test(NAME EditorFontPreviewTest COMMAND EditorFontPreviewTest)
