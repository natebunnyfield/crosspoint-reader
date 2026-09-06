# Where a table-of-contents pick lands.
#
# THE REPORT (owner, 2026-09-06): "chapter selection is going to the page
# before intended place". A TOC entry with a fragment resolves through the
# anchor map ChapterHtmlSlimParser records while it paginates: the id's page,
# with a forced page break in front of it when the id is a TOC anchor. Whether
# that page is the one the heading is on depends on WHERE the id sits -- on the
# heading, on a wrapper, on an empty <a> before it, or on an <a> INSIDE it --
# and on what the widow/orphan holdback does to the paragraph before. Each
# shape is one case here, through the same real chain as definition_list
# (parser, layout engine, built-in reader face, a real XHTML file on disk).
set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

add_executable(TocAnchorPageTest
  TocAnchorPageTest.cpp
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
target_compile_definitions(TocAnchorPageTest PRIVATE XML_GE=0 XML_CONTEXT_BYTES=1024 XML_POOR_ENTROPY)

target_compile_options(TocAnchorPageTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(TocAnchorPageTest PRIVATE
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

target_link_libraries(TocAnchorPageTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TocAnchorPageTest)
