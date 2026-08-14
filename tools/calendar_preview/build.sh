#!/usr/bin/env bash
# Build the off-device simulator. Run from this directory.
#
# Produces ./sim, which covers both modes that used to be separate binaries:
#   ./sim calendar [YYYY MM DD]
#   ./sim fonts FAMILY_A FAMILY_B
#
# RENDER_SCALE=2 ./build.sh  produces ./render_harness2 instead, a 2x-canvas
# build. That is the ONLY honest way to preview what iOS and the desktop app
# show: at scale 2 the renderer looks up each family's 2x/ companions and draws
# into a doubled framebuffer, so downsampling the output by two gives the real
# page. Rendering a family whose `sizes:` were doubled through the 1x harness
# does NOT do this -- it gives correct glyph proportions on a page with a third
# of the words, which is a specimen of something else. Round 17 was judged that
# way; round 18 was not.
set -euo pipefail
R="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$(dirname "$0")"

SCALE="${RENDER_SCALE:-1}"
if [[ "$SCALE" == "1" ]]; then OUT=render_harness; else OUT="render_harness${SCALE}"; fi

gcc -c -O1 -I"$R/lib/uzlib/src" -I"$R/freeink-sdk/libs/ui/FreeInkUI/include" "$R/lib/uzlib/src/tinflate.c" -o tinflate.o
gcc -c -O1 -I"$R/lib/MiniBidi" "$R/lib/MiniBidi/minibidi.c" -o minibidi.o
gcc -c -O1 uzlib_checksums.c -o uzlib_checksums.o

g++ -std=gnu++2a -O1 -DCROSSPOINT_RENDER_SCALE="$SCALE" -include Arduino.h -o "$OUT" render_harness.cpp \
  "$R/lib/GfxRenderer/GfxRenderer.cpp" "$R/lib/GfxRenderer/Bitmap.cpp" \
  "$R/lib/GfxRenderer/BitmapHelpers.cpp" "$R/lib/GfxRenderer/FontCacheManager.cpp" \
  "$R/lib/EpdFont/EpdFont.cpp" "$R/lib/EpdFont/EpdFontFamily.cpp" \
  "$R/lib/EpdFont/FontDecompressor.cpp" "$R/lib/EpdFont/SdCardFont.cpp" \
  "$R/lib/Utf8/Utf8.cpp" "$R/lib/MiniBidi/BidiUtils.cpp" "$R/lib/Memory/BuildScratch.cpp" \
  "$R"/lib/InflateReader/*.cpp tinflate.o minibidi.o uzlib_checksums.o \
  "$R/src/activities/boot_sleep/CalendarSleepScreen.cpp" daisy_preview.cpp \
  "$R/src/activities/boot_sleep/HolidayCalculator.cpp" sd_font_stub.cpp \
  -I. -I"$R/src" -I"$R/lib/GfxRenderer" -I"$R/lib/EpdFont" -I"$R/lib/Utf8" \
  -I"$R/lib/MiniBidi" -I"$R/lib/Memory" -I"$R/lib/InflateReader" -I"$R/lib/uzlib/src" -I"$R/freeink-sdk/libs/ui/FreeInkUI/include"

mkdir -p fs_
echo "built ./$OUT"
