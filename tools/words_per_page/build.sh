#!/usr/bin/env bash
set -e
R=/Users/natebunnyfield/src/crosspoint-reader
CP=$R/tools/calendar_preview
SCALE=${SCALE:-1}
g++ -std=gnu++2a -O1 -DCROSSPOINT_RENDER_SCALE=$SCALE -DXML_GE=0 -DXML_CONTEXT_BYTES=1024 -DXML_POOR_ENTROPY \
  -include Arduino.h -o wpp$SCALE WordsPerPage.cpp \
  $R/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp $R/lib/Epub/Epub/parsers/TableCellLabel.cpp \
  $R/lib/Epub/Epub/parsers/XmlEncodingSupport.cpp $R/lib/Epub/Epub/parsers/XmlEncodingTables.cpp \
  $R/lib/Epub/Epub/converters/ImageDimsProbe.cpp $R/lib/FsHelpers/FsHelpers.cpp \
  $R/lib/Epub/Epub/parsers/TableColumnLayout.cpp $R/lib/Epub/Epub/css/CssParser.cpp \
  $R/lib/Epub/Epub/htmlEntities.cpp $R/lib/Epub/Epub/Page.cpp $R/lib/Epub/Epub/ParsedText.cpp \
  $R/lib/Epub/Epub/BookNotes.cpp $R/lib/Epub/Epub/blocks/TextBlock.cpp $R/lib/Epub/Epub/blocks/ImageBlock.cpp \
  $R/lib/Epub/Epub/hyphenation/Hyphenator.cpp $R/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp \
  $R/lib/Epub/Epub/hyphenation/LanguageRegistry.cpp $R/lib/Epub/Epub/hyphenation/LiangHyphenation.cpp \
  $R/lib/GfxRenderer/GfxRenderer.cpp $R/lib/GfxRenderer/Bitmap.cpp $R/lib/GfxRenderer/BitmapHelpers.cpp \
  $R/lib/GfxRenderer/FontCacheManager.cpp $R/lib/EpdFont/EpdFont.cpp $R/lib/EpdFont/LigatureControl.cpp \
  $R/lib/EpdFont/EpdFontFamily.cpp $R/lib/EpdFont/FontDecompressor.cpp $R/lib/EpdFont/SdCardFont.cpp \
  $R/lib/Utf8/Utf8.cpp $R/lib/MiniBidi/BidiUtils.cpp $R/lib/Memory/BuildScratch.cpp \
  $R/lib/InflateReader/InflateReader.cpp \
  tinflate.o minibidi.o uzlib_checksums.o xmlparse.o xmlrole.o xmltok.o \
  -I$R/test/table_keep_together -I$R/test/activity_input/stubs -I$R/test/host_stubs -I$CP \
  -I$R -I$R/lib -I$R/src -I$R/lib/Epub -I$R/lib/Epub/Epub/parsers -I$R/lib/GfxRenderer -I$R/lib/EpdFont \
  -I$R/lib/Utf8 -I$R/lib/MiniBidi -I$R/lib/Memory -I$R/lib/InflateReader -I$R/lib/uzlib/src \
  -I$R/lib/Serialization -I$R/lib/FsHelpers -I$R/lib/XmlParserUtils -I$R/lib/I18n -I$R/lib/Txt -I$R/lib/Logging \
  -I$R/lib/expat -I$R/freeink-sdk/libs/ui/FreeInkUI/include
echo "built ./wpp$SCALE"
