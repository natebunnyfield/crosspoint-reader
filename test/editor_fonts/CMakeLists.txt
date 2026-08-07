# Editor-font group invariants. Pure table logic — EditorFonts.cpp has no
# renderer, no Arduino and no SD dependency, so this links two files and
# fontIds.h and nothing else.
add_executable(EditorFontsTest
  EditorFontsTest.cpp
  ${REPO_ROOT}/src/notes/EditorFonts.cpp
)
target_include_directories(EditorFontsTest PRIVATE
  ${REPO_ROOT}/src
)
target_link_libraries(EditorFontsTest PRIVATE GTest::gtest_main)
add_test(NAME EditorFontsTest COMMAND EditorFontsTest)
