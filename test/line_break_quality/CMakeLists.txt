# Line-breaker quality + cost suite.
#
# Links the REAL layout engine host-side -- ParsedText, TextBlock, hyphenation,
# GfxRenderer with the builtin Libre Franklin reader faces -- the same way
# word_anchor does, so the line geometry measured here is the geometry the
# paginator bakes into a section file.

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

add_executable(LineBreakQualityTest
  LineBreakQualityTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/Hyphenator.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LanguageRegistry.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LiangHyphenation.cpp
  ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp
  ${REPO_ROOT}/lib/GfxRenderer/Bitmap.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/LigatureControl.cpp
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

# The ragged hyphenation gate becomes settable at runtime for THIS TARGET ONLY
# (lib/Epub/Epub/LineBreakMode.h). DISABLED_RaggedGateSweep walks it 40..100 in
# single-point steps inside one process; rebuilding the layout engine and the
# built-in faces sixty times to answer one question is the alternative. No other
# target may define it -- on a device a mutable layout parameter is a way for two
# pages of the same book to disagree about where the lines go.
target_compile_definitions(LineBreakQualityTest PRIVATE CROSSPOINT_RAGGED_GATE_TUNABLE)

target_compile_options(LineBreakQualityTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(LineBreakQualityTest PRIVATE
  ${CP_PREVIEW}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/uzlib/src
  ${REPO_ROOT}/lib/Serialization
)

target_link_libraries(LineBreakQualityTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LineBreakQualityTest)
