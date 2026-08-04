#!/bin/bash

set -e

cd "$(dirname "$0")"

READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
NOTOSERIF_FONT_SIZES=(12 14 16 18)
NOTOSANS_FONT_SIZES=(12 14 16 18)

for size in ${NOTOSERIF_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notoserif_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSerif/NotoSerif-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum > $output_path
    echo "Generated $output_path"
  done
done

for size in ${NOTOSANS_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notosans_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSans/NotoSans-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum > $output_path
    echo "Generated $output_path"
  done
done

UI_FONT_SIZES=(10 12)
UI_FONT_STYLES=("Regular" "Bold")

# Arabic/Hebrew translations removed in commit 08d5bdee; glyphs for those
# scripts are no longer needed in the built-in UI faces. Ubuntu-Vietnamese is
# also dropped: the Vietnamese interval (0x1EA0-0x1EF9) was removed from
# fontconvert.py's default list at the same time.

# --- System-font matrix -----------------------------------------------------
#
# The chrome draws at three sizes -- 8, 10 and 12 pt -- and the System font
# setting swaps all three to one family at once, so every offered family needs
# regular and bold at each size. Plain 1-bit, no --compress: these are the UI
# faces, and the decompressor cost is not worth paying on every list row.
#
# Libre Franklin is the odd one out in provenance. Upstream ships it as a
# variable font; source/LibreFranklin holds the wght 400 and 700 instances cut
# by build-sd-fonts.py, committed as statics so this script needs no network.
UI_FAMILIES=(
  "ubuntu:Ubuntu/Ubuntu"
  "notosans:NotoSans/NotoSans"
  "notoserif:NotoSerif/NotoSerif"
  "librefranklin:LibreFranklin/LibreFranklin"
)

for entry in ${UI_FAMILIES[@]}; do
  prefix="${entry%%:*}"
  stem="${entry#*:}"
  for size in 8 10 12; do
    for style in ${UI_FONT_STYLES[@]}; do
      font_name="${prefix}_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
      output_path="../builtinFonts/${font_name}.h"
      # Noto Sans and Noto Serif at 12 pt are READER cuts, generated above at
      # 2-bit with --compress --pnum, and the system-font matrix reuses those
      # very symbols. Regenerating them here as plain 1-bit would silently
      # downgrade the reader's 12 pt face.
      if [ "$size" = "12" ] && { [ "$prefix" = "notosans" ] || [ "$prefix" = "notoserif" ]; }; then
        echo "Skipping $output_path (reader cut, generated above)"
        continue
      fi
      python fontconvert.py $font_name $size "../builtinFonts/source/${stem}-${style}.ttf" > $output_path
      echo "Generated $output_path"
    done
  done
done

# --- Hi-res companions for the UI faces -------------------------------------
#
# The same matrix rasterised at double the ppem, for glyph blitting only on a
# CROSSPOINT_RENDER_SCALE=2 host build. Metrics keep coming from the 1x tables
# (see registerHiResBuiltinFont), so these carry bitmaps and nothing else, and
# the name encodes the 1x face each stands in for -- not its own point size.
for entry in ${UI_FAMILIES[@]}; do
  prefix="${entry%%:*}"
  stem="${entry#*:}"
  for size in 8 10 12; do
    for style in ${UI_FONT_STYLES[@]}; do
      font_name="${prefix}_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')_2x"
      output_path="../builtinFonts/${font_name}.h"
      python fontconvert.py $font_name $((size * 2)) "../builtinFonts/source/${stem}-${style}.ttf" > $output_path
      echo "Generated $output_path"
    done
  done
done

echo ""
echo "Running compression verification..."
python verify_compression.py ../builtinFonts/
