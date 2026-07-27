#!/usr/bin/env bash
# Build the off-device calendar render harness. Run from this directory.
set -euo pipefail
R="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$(dirname "$0")"

gcc -c -O1 -I"$R/lib/uzlib/src" "$R/lib/uzlib/src/tinflate.c" -o tinflate.o
gcc -c -O1 -I"$R/lib/MiniBidi" "$R/lib/MiniBidi/minibidi.c" -o minibidi.o
gcc -c -O1 uzlib_checksums.c -o uzlib_checksums.o

g++ -std=gnu++2a -O1 -include Arduino.h -o render_test render_harness.cpp \
  "$R/lib/GfxRenderer/GfxRenderer.cpp" "$R/lib/GfxRenderer/Bitmap.cpp" \
  "$R/lib/GfxRenderer/BitmapHelpers.cpp" "$R/lib/GfxRenderer/FontCacheManager.cpp" \
  "$R/lib/EpdFont/EpdFont.cpp" "$R/lib/EpdFont/EpdFontFamily.cpp" \
  "$R/lib/EpdFont/FontDecompressor.cpp" "$R/lib/EpdFont/SdCardFont.cpp" \
  "$R/lib/Utf8/Utf8.cpp" "$R/lib/MiniBidi/BidiUtils.cpp" "$R/lib/Memory/BuildScratch.cpp" \
  "$R"/lib/InflateReader/*.cpp tinflate.o minibidi.o uzlib_checksums.o \
  "$R/src/activities/boot_sleep/CalendarSleepScreen.cpp" \
  "$R/src/activities/boot_sleep/HolidayCalculator.cpp" \
  -I. -I"$R/src" -I"$R/lib/GfxRenderer" -I"$R/lib/EpdFont" -I"$R/lib/Utf8" \
  -I"$R/lib/MiniBidi" -I"$R/lib/Memory" -I"$R/lib/InflateReader" -I"$R/lib/uzlib/src"

mkdir -p fs_
echo "built ./render_test"
