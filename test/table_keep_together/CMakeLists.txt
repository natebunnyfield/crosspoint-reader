# Table keep-with-next: does a table's header row (and the caption above it)
# travel onto the same page as its first body rows?
#
# THIS IS THE FIRST SUITE THAT LINKS ChapterHtmlSlimParser HOST-SIDE. B-037
# closed with a stated coverage gap -- "no host test links
# ChapterHtmlSlimParser, so the emitter half of this fix is covered by render
# only" -- and table pagination is exactly the kind of thing that regresses
# without anyone seeing it, because the only symptom is a page that looks
# slightly emptier than it should. So the whole chain is built: the parser, the
# CSS parser, the layout engine, the page model and the built-in reader faces,
# over a real XHTML file on disk.
#
# Two symbols are STUBBED in the test file rather than linked, and both are
# reachable only from the <img> path, which no fixture here takes:
#   Epub::readItemContentsToStream       linking Epub.cpp drags in the zip
#                                        reader, the metadata cache and the
#                                        cover pipeline for a call never made.
#   ImageDecoderFactory::getDecoder      the decoders come from PlatformIO's
#                    /isFormatSupported  lib_deps (JPEGDEC, the PNG chain), not
#                                        from the repo.
#
# HalStorage is this suite's OWN stub (stubs live beside the suite that needs
# them, per test/README). tools/calendar_preview's is deliberately not reused:
# it prefixes every path with "./fs_", its available() answers 1 while the file
# is open -- and parseStep() reads `available() == 0` as "this was the last
# buffer", so with that stub expat is never told the document ended -- and it
# has no isOpen(). The reasons are written out at the head of HalStorage.h.

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

add_executable(TableKeepTogetherTest
  TableKeepTogetherTest.cpp
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
target_compile_definitions(TableKeepTogetherTest PRIVATE XML_GE=0 XML_CONTEXT_BYTES=1024 XML_POOR_ENTROPY)

target_compile_options(TableKeepTogetherTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(TableKeepTogetherTest PRIVATE
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

target_link_libraries(TableKeepTogetherTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TableKeepTogetherTest)
