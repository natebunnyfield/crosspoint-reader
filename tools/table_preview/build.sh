#!/usr/bin/env bash
# Build the table-layout mockup harness. Run from this directory.
#
# Reuses tools/calendar_preview's host stubs (Arduino.h, HalDisplay.h, HalGPIO.h,
# HalStorage.h, Logging.h, sd_font_stub.cpp) via -I rather than copying them --
# they are the same stubs, and two divergent copies of a display stub is exactly
# the kind of drift that makes a harness lie.
set -euo pipefail
R="$(cd "$(dirname "$0")/../.." && pwd)"
C="$R/tools/calendar_preview"
cd "$(dirname "$0")"

SCALE="${RENDER_SCALE:-1}"

# The three C objects the calendar harness also builds. Reused from its
# directory when they are already there, so a mockup run does not rebuild them.
for o in tinflate.o minibidi.o uzlib_checksums.o; do
  [ -f "$C/$o" ] || { echo "missing $C/$o -- run ../calendar_preview/build.sh first"; exit 1; }
done

g++ -std=gnu++2a -O1 -DCROSSPOINT_RENDER_SCALE="$SCALE" -include "$C/Arduino.h" -o table_preview table_preview.cpp \
  "$R/lib/GfxRenderer/GfxRenderer.cpp" "$R/lib/GfxRenderer/Bitmap.cpp" \
  "$R/lib/GfxRenderer/BitmapHelpers.cpp" "$R/lib/GfxRenderer/FontCacheManager.cpp" \
  "$R/lib/EpdFont/EpdFont.cpp" "$R/lib/EpdFont/EpdFontFamily.cpp" \
  "$R/lib/EpdFont/LigatureControl.cpp" \
  "$R/lib/EpdFont/FontDecompressor.cpp" "$R/lib/EpdFont/SdCardFont.cpp" \
  "$R/lib/Utf8/Utf8.cpp" "$R/lib/MiniBidi/BidiUtils.cpp" "$R/lib/Memory/BuildScratch.cpp" \
  "$R"/lib/InflateReader/*.cpp "$C/tinflate.o" "$C/minibidi.o" "$C/uzlib_checksums.o" \
  "$C/sd_font_stub.cpp" \
  -I"$C" -I"$R/src" -I"$R/lib/GfxRenderer" -I"$R/lib/EpdFont" -I"$R/lib/Utf8" \
  -I"$R/lib/MiniBidi" -I"$R/lib/Memory" -I"$R/lib/InflateReader" -I"$R/lib/uzlib/src" \
  -I"$R/freeink-sdk/libs/ui/FreeInkUI/include"

mkdir -p fs_
echo "built ./table_preview"
