# Hangul line breaking (upstream 794ac97f4, #3700): a Korean word is one token
# and wraps at spaces; with hyphenation on it may split at a line end, no hyphen
# drawn; only word spaces stretch. Links the REAL layout engine and the built-in
# faces the way line_break_quality does. Those faces carry no Hangul, so every
# syllable measures as the same missing glyph: widths are read back from the
# renderer, never hard-coded.

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

add_executable(KoreanLineBreakingTest
  KoreanLineBreakingTest.cpp
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
# And the Knuth-Plass switch, which this suite turns OFF: it is the greedy
# breaker's measurement (see kGreedyPinned in KoreanLineBreakingTest.cpp).
target_compile_definitions(KoreanLineBreakingTest PRIVATE CROSSPOINT_KNUTH_PLASS_TUNABLE)

target_compile_options(KoreanLineBreakingTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(KoreanLineBreakingTest PRIVATE
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

target_link_libraries(KoreanLineBreakingTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KoreanLineBreakingTest)
