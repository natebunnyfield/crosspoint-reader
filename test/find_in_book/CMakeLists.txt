# Whole-book Find (docs/find-in-book-options-2026-09-28.md section 9), through
# the whole chain: a real .epub on disk -> Epub::load() -> Section build ->
# pages -> readaloud::buildCapture -> findtext::Matcher, driven by the real
# BookFinder engine the reader's Find activity drives.
#
# The link line is test/chapter_jump's, for the same reasons (a real filesystem
# under HalStorage, miniz, cover-converter stubs); it reuses that suite's
# HalStorage.h and stubs/ rather than a third copy of them, plus BookFinder.cpp.
set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

add_executable(FindInBookTest
  FindInBookTest.cpp
  ${REPO_ROOT}/src/activities/reader/BookFinder.cpp
  ${REPO_ROOT}/lib/Epub/Epub.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Section.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BookMetadataCache.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BookNotes.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/ImageBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/Hyphenator.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LanguageRegistry.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LiangHyphenation.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ContainerParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ContentOpfParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/TocNavParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/TocNcxParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/TableCellLabel.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/TableColumnLayout.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/XmlEncodingSupport.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/XmlEncodingTables.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/ZipFile/ZipFile.cpp
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
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

target_compile_definitions(FindInBookTest PRIVATE XML_GE=0 XML_CONTEXT_BYTES=1024 XML_POOR_ENTROPY
  CROSSPOINT_TEST_EPUB_DIR="${REPO_ROOT}/test/epubs")

target_compile_options(FindInBookTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(FindInBookTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/test/chapter_jump
  ${REPO_ROOT}/test/chapter_jump/stubs
)
target_include_directories(FindInBookTest PRIVATE
  ${REPO_ROOT}/test/activity_input/stubs
  ${REPO_ROOT}/test/host_stubs
  ${CP_PREVIEW}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/activities/reader
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
  ${REPO_ROOT}/lib/ZipFile
  ${REPO_ROOT}/lib/miniz/src
  ${REPO_ROOT}/lib/expat
)

target_link_libraries(FindInBookTest PRIVATE crosspoint_test_common GTest::gtest GTest::gtest_main)
gtest_discover_tests(FindInBookTest)
