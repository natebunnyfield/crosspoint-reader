#!/bin/bash

set -e

cd "$(dirname "$0")"

READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")

# --- Reader fallback family --------------------------------------------------
#
# Libre Franklin is the ONLY built-in reading family (owner ruling 2026-08-07;
# Noto Serif, Noto Sans and Ubuntu were removed outright). These cuts back the
# reader when no SD font resolves, so all four styles are real — the Italic and
# BoldItalic statics in source/LibreFranklin were instanced from upstream's
# LibreFranklin-Italic[wght].ttf at wght 400/700, the same way
# build-sd-fonts.py cut the Regular/Bold from the roman VF. Committed as
# statics so this script needs no network.
#
# The `librefranklin_reader_` prefix is deliberate: the system-font matrix
# below emits `librefranklin_` UI cuts at DIFFERENT flags (2-bit but
# uncompressed, no --pnum), and distinct symbols mean regenerating one set can
# never silently downgrade the other — the trap the old shared Noto 12 pt cut
# carried.
LIBREFRANKLIN_READER_SIZES=(12 14 16 18)

for size in ${LIBREFRANKLIN_READER_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="librefranklin_reader_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/LibreFranklin/LibreFranklin-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum > $output_path
    echo "Generated $output_path"
  done
done

# --- Editor-group monospace ------------------------------------------------
#
# Owner ruling 2026-08-06: Space Mono and IBM Plex Mono are compiled INTO the
# firmware rather than shipped as .cpfont on the card. Two reasons. A built-in
# face works on a blank card, where the previous card-only arrangement meant the
# Editor Font setting silently did nothing on every device; and a built-in face
# cannot appear in the READING picker, which an install into /fonts would cause,
# since SdCardFontRegistry lists whatever family directories it finds there.
#
# ONE size only. resolveEditorFont() asks for 12 pt and nothing else, so the
# 14/16/18 cuts the reading faces carry would be dead flash. All four styles are
# real: MarkdownSpans renders bold, italic and bold-italic. 2-bit + compressed,
# matching the reader cuts.
#
# Flash cost: ~83 KB for SpaceMono + IBMPlexMono, ~216 KB for the three iA
# Writer families (owner ruling 2026-08-11). The iA faces are the "S" (narrow)
# cuts; their URL-fetch recipes live in sd-fonts.yaml "Editor group", and the
# source TTFs for the builtin cuts are in builtinFonts/source/iAWriterXxx/.
#
# iA Writer font file name differs from the family name: the on-disk file is
# iAWriterQuattroS (with the S suffix for narrow), but we strip it for the
# source directory and internal symbol names to match sd-fonts.yaml's family
# name (iAWriterQuattro).
EDITOR_FAMILIES=(
  "spacemono:SpaceMono/SpaceMono"
  "ibmplexmono:IBMPlexMono/IBMPlexMono"
  "iawriterquattro:iAWriterQuattro/iAWriterQuattro"
  "iawriterduo:iAWriterDuo/iAWriterDuo"
  "iawritermono:iAWriterMono/iAWriterMono"
)

for entry in ${EDITOR_FAMILIES[@]}; do
  prefix="${entry%%:*}"
  stem="${entry#*:}"
  for style in ${READER_FONT_STYLES[@]}; do
    lc=$(echo $style | tr '[:upper:]' '[:lower:]')
    font_name="${prefix}_12_${lc}"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name 12 "../builtinFonts/source/${stem}-${style}.ttf" --2bit --compress --pnum > $output_path
    echo "Generated $output_path"
  done
done

# --- Hi-res companions for the editor fonts ----------------------------------
#
# One 2x cut (24 pt = 12 * 2) per style, per family. Same 2-bit compressed
# flags as the 1x cut: the renderer blits whichever bitmap it has (1x or 2x)
# through the same path, so the bit-depth must match. Named for the 1x face
# each stands in for (same convention as the system UI 2x matrix above).
#
# On device RENDER_SCALE=1 and --gc-sections strips these arrays; they are
# present only in simulator / iOS builds that run at RENDER_SCALE=2. The
# companion is what stops NoteEditorActivity and ClaudeChatActivity from
# rendering editor text at 1x resolution on a 2x build — drawText() checks
# getHiResFamily() for any font id it is handed.
for entry in ${EDITOR_FAMILIES[@]}; do
  prefix="${entry%%:*}"
  stem="${entry#*:}"
  for style in ${READER_FONT_STYLES[@]}; do
    lc=$(echo $style | tr '[:upper:]' '[:lower:]')
    font_name="${prefix}_12_${lc}_2x"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name 24 "../builtinFonts/source/${stem}-${style}.ttf" --2bit --compress --pnum > $output_path
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
# The chrome draws at three sizes -- 8, 10 and 12 pt -- in regular and bold.
# Libre Franklin is the only chrome family (owner ruling 2026-08-07).
#
# --2bit, and DELIBERATELY NOT --compress. These were plain 1-bit until
# 2026-08-14; the comment here said "plain 1-bit, no --compress: these are the
# UI faces, and the decompressor cost is not worth paying on every list row".
# Measured, that sentence is half right, and the two halves point opposite ways
# -- see docs/two-bit-chrome.md for the full table.
#
# FLASH, measured on -e default (same tree, only these headers differ):
#
#   1-bit, uncompressed (what shipped)   4,178,069 B   63.8%
#   2-bit, uncompressed  (chosen)        4,300,597 B   65.6%   +122,528 B
#   2-bit, compressed                    4,152,733 B   63.4%    -25,336 B
#
# So compression really is free on flash -- better than free. It is TIME that
# rules it out. A chrome screen has no PrewarmScope (readers do), so every
# glyph goes through FontDecompressor's hot-group path, and one group must be
# inflated again on every switch of font id or style. Measured in the
# simulator, three variant binaries interleaved round-robin so machine load hit
# all three equally, median of 33-45 repaints each:
#
#   colophon, one page turn      1-bit  3,331 us | 2-bit  3,327 us | 2-bit+z 20,395 us
#   settings list, one Down      1-bit  3,249 us | 2-bit  3,300 us | 2-bit+z  5,436 us
#
# 2-bit uncompressed is TIME-NEUTRAL against the 1-bit cuts it replaces -- the
# extra bit costs nothing measurable in the plot loop. Compression costs 6x on
# the colophon, because its bold/regular alternation forces 45 group inflates
# per repaint, 194,633 uncompressed bytes. It also holds up to 19,612 B of DRAM
# in the decompressor's grow-only hot-group buffer, which a chrome-only session
# never used to allocate at all. Uncompressed chrome reads its glyphs straight
# out of flash: zero DRAM, zero inflate.
#
# 1-bit is what made anti-aliased chrome impossible in the first place: a
# grayscale plane is flagged only from the 2-bit branch of
# GfxRenderer::renderCharImpl, so a 1-bit chrome glyph contributed nothing to a
# plane pass.
#
# NOTE: --compress REQUIRES --2bit (fontconvert.py:808). There is no compressed
# 1-bit format, so those two columns above are the only alternatives that exist.
#
# Upstream ships Libre Franklin as a variable font; source/LibreFranklin holds
# the wght 400 and 700 instances cut by build-sd-fonts.py, committed as
# statics so this script needs no network.
UI_FAMILIES=(
  "librefranklin:LibreFranklin/LibreFranklin"
)

for entry in ${UI_FAMILIES[@]}; do
  prefix="${entry%%:*}"
  stem="${entry#*:}"
  for size in 8 10 12; do
    for style in ${UI_FONT_STYLES[@]}; do
      font_name="${prefix}_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
      output_path="../builtinFonts/${font_name}.h"
      python fontconvert.py $font_name $size "../builtinFonts/source/${stem}-${style}.ttf" --2bit > $output_path
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
#
# Same --2bit flag as the 1x cut above, and that is a HARD requirement, not
# symmetry: drawText() picks the companion by font id and blits it through the
# same renderCharImpl, which reads the bit depth off the EpdFontData it was
# handed. A 1-bit companion under a 2-bit 1x face would render as noise.
# None of these reach the device binary -- no device env sets
# CROSSPOINT_RENDER_SCALE, and nm on firmware.elf shows zero `_2x` symbols.
for entry in ${UI_FAMILIES[@]}; do
  prefix="${entry%%:*}"
  stem="${entry#*:}"
  for size in 8 10 12; do
    for style in ${UI_FONT_STYLES[@]}; do
      font_name="${prefix}_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')_2x"
      output_path="../builtinFonts/${font_name}.h"
      python fontconvert.py $font_name $((size * 2)) "../builtinFonts/source/${stem}-${style}.ttf" --2bit > $output_path
      echo "Generated $output_path"
    done
  done
done

echo ""
echo "Running compression verification..."
python verify_compression.py ../builtinFonts/
