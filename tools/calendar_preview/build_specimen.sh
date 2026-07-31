#!/usr/bin/env bash
# Build the off-device kerning specimen harness. Run from this directory.
# Separate from build.sh so the calendar harness and this one can be built
# independently; they share the host stubs and sd_font_stub.cpp.
set -euo pipefail
R="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$(dirname "$0")"

# Reuse the calendar harness's C objects when they are already present.
[ -f tinflate.o ] || gcc -c -O1 -I"$R/lib/uzlib/src" "$R/lib/uzlib/src/tinflate.c" -o tinflate.o
[ -f minibidi.o ] || gcc -c -O1 -I"$R/lib/MiniBidi" "$R/lib/MiniBidi/minibidi.c" -o minibidi.o
[ -f uzlib_checksums.o ] || gcc -c -O1 uzlib_checksums.c -o uzlib_checksums.o

g++ -std=gnu++2a -O1 -include Arduino.h -o kern_specimen kern_specimen.cpp \
  "$R/lib/GfxRenderer/GfxRenderer.cpp" "$R/lib/GfxRenderer/Bitmap.cpp" \
  "$R/lib/GfxRenderer/BitmapHelpers.cpp" "$R/lib/GfxRenderer/FontCacheManager.cpp" \
  "$R/lib/EpdFont/EpdFont.cpp" "$R/lib/EpdFont/EpdFontFamily.cpp" \
  "$R/lib/EpdFont/FontDecompressor.cpp" "$R/lib/EpdFont/SdCardFont.cpp" \
  "$R/lib/Utf8/Utf8.cpp" "$R/lib/MiniBidi/BidiUtils.cpp" "$R/lib/Memory/BuildScratch.cpp" \
  "$R"/lib/InflateReader/*.cpp tinflate.o minibidi.o uzlib_checksums.o \
  sd_font_stub.cpp \
  -I. -I"$R/src" -I"$R/lib/GfxRenderer" -I"$R/lib/EpdFont" -I"$R/lib/Utf8" \
  -I"$R/lib/MiniBidi" -I"$R/lib/Memory" -I"$R/lib/InflateReader" -I"$R/lib/uzlib/src"

echo "built ./kern_specimen"
