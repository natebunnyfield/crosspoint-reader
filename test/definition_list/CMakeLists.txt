# Definition lists: <dl>, <dt>, <dd>.
#
# THE REPORT (owner, 2026-08-28, a screenshot of a quiz book): a question ran
# straight into its answer and then into the note under it, all on one line --
# "...opens which novel?A Tale of Two Cities (Dickens, 1859)The two cities are
# London and Paris...". The three tags were unknown to ChapterHtmlSlimParser,
# so they fell through to its INLINE branch and no block ever opened.
#
# WHAT THIS SUITE IS FOR, and why it is a second one rather than cases added to
# table_keep_together: the same real chain (parser, CSS parser, layout engine,
# built-in reader faces, a real XHTML file on disk), but every case here is
# about a <dl>, and one of them needs a real CssParser loaded from a real
# stylesheet -- which no case there does. The two stubs and the HalStorage
# story are identical and the reasons are written out at the head of
# HalStorage.h.

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

add_executable(DefinitionListTest
  DefinitionListTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/TableCellLabel.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/XmlEncodingSupport.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/XmlEncodingTables.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/TableColumnLayout.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BookNotes.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/ImageBlock.cpp
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
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

# Verbatim from platformio.ini's [env] build_flags -- expat is configured by
# -D, so a host build that omits these is not the parser the firmware ships.
target_compile_definitions(DefinitionListTest PRIVATE XML_GE=0 XML_CONTEXT_BYTES=1024 XML_POOR_ENTROPY)

target_compile_options(DefinitionListTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(DefinitionListTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/test/activity_input/stubs
  ${REPO_ROOT}/test/host_stubs
  ${CP_PREVIEW}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub/parsers
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/uzlib/src
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/I18n
  ${REPO_ROOT}/lib/Txt
  ${REPO_ROOT}/lib/Logging
  ${REPO_ROOT}/lib/expat
)

target_link_libraries(DefinitionListTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DefinitionListTest)
