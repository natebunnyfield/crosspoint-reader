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
# The in-book cycle lives in an activity that cannot be linked here (Arduino,
# SdFat, the device stack), so one test reads its source instead -- same call
# OmitFontsTest makes, for the same reason.
target_compile_definitions(EditorFontsTest PRIVATE
  CROSSPOINT_EPUB_READER_CPP="${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp")
target_link_libraries(EditorFontsTest PRIVATE GTest::gtest_main)
add_test(NAME EditorFontsTest COMMAND EditorFontsTest)
