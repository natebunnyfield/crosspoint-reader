# Unicode arrows in the SD-card reading faces.
#
# Links the real registry + manager + renderer host-side, the same stack as
# test/font_switch_churn, because the point is to exercise the .cpfont files
# that actually ship rather than a mock.
#
# The stub HalStorage is SHARED with font_switch_churn rather than copied:
# SdCardFontRegistry needs Storage.open()/openNextFile(), which
# tools/calendar_preview's read-only stub does not provide, and a second copy of
# a stub is a second thing to keep in step. It comes FIRST on the include path
# so it supersedes the harness one.
#
# WORKING_DIRECTORY is the repo root because the stub rebases device paths onto
# ./fs_, where the real .cpfont tree lives.

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

add_executable(SdFontArrowsTest
  SdFontArrowsTest.cpp
  # For editorfonts::isWritingOnlyFamily: the reading picker's own test for
  # whether a discovered family is a writing-only editor face. FontDisplayNames
  # comes with it (EditorFonts.cpp includes it).
  ${REPO_ROOT}/src/notes/EditorFonts.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFontManager.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFontRegistry.cpp
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

target_compile_options(SdFontArrowsTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(SdFontArrowsTest BEFORE PRIVATE
  ${REPO_ROOT}/test/font_switch_churn/stubs
)
target_include_directories(SdFontArrowsTest PRIVATE
  ${CP_PREVIEW}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/uzlib/src
)

target_link_libraries(SdFontArrowsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SdFontArrowsTest
  WORKING_DIRECTORY ${REPO_ROOT}
  PROPERTIES ENVIRONMENT "CROSSPOINT_TEST_SD=${CROSSPOINT_TEST_SD_DIR}"
)
