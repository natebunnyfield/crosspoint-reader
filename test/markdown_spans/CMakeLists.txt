# Markdown-lite span analysis. Pure logic: no renderer, no Arduino, no device.
add_executable(MarkdownSpansTest MarkdownSpansTest.cpp)
target_include_directories(MarkdownSpansTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(MarkdownSpansTest PRIVATE GTest::gtest_main)
add_test(NAME MarkdownSpansTest COMMAND MarkdownSpansTest)

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")
add_executable(MarkdownRenderTest
  MarkdownRenderTest.cpp
  ${REPO_ROOT}/src/notes/MarkdownRender.cpp
  ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp
  ${REPO_ROOT}/lib/GfxRenderer/Bitmap.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/EpdFont/FontDecompressor.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/uzlib/src/tinflate.c
  ${CP_PREVIEW}/uzlib_checksums.c
)
target_compile_options(MarkdownRenderTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)
target_include_directories(MarkdownRenderTest PRIVATE
  ${CP_PREVIEW}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/EpdFont/builtinFonts
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/uzlib/src
)
target_link_libraries(MarkdownRenderTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(MarkdownRenderTest)
