#!/bin/bash

set -e

cd "$(dirname "$0")"

# Every generation below is `python fontconvert.py ... > some_font.h`, so ANY
# byte the interpreter writes to stdout before the converter's own output lands
# inside the generated C header. On a free-threaded Python build, importing
# fontTools emits a GIL RuntimeWarning that does exactly that, and the result is
# a header whose line 1 is prose. The compiler then fails at `<frozen
# importlib._bootstrap>:491:` with "expected unqualified-id" and a cascade of
# undeclared-identifier errors naming the font's own arrays — which reads like a
# broken font recipe rather than a stray warning, and cost a debugging cycle on
# 2026-08-15 when it corrupted all eight Nitti cuts at once.
#
# PYTHON_GIL=1 silences it at the source; PYTHONWARNINGS=ignore covers whatever
# the next interpreter decides to narrate. Neither changes the rasterisation.
export PYTHON_GIL=1
export PYTHONWARNINGS=ignore

# Belt and braces: a generated header must begin with the `/**` banner
# fontconvert.py writes. Anything else means something leaked into the file, and
# failing here beats shipping a header that only breaks at compile time — or
# worse, one that compiles because the junk happened to look like a comment.
assert_header_clean() {
  local header="$1"
  if [ ! -s "$header" ]; then
    echo "ERROR: $header is empty — fontconvert.py wrote nothing" >&2
    exit 1
  fi
  if ! head -n 1 "$header" | grep -q '^/\*\*'; then
    echo "ERROR: $header does not start with the /** banner. First line was:" >&2
    head -n 1 "$header" >&2
    echo "Something wrote to stdout ahead of the converter (a Python warning?)." >&2
    exit 1
  fi
}

READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")

# Hi-res companion tiers to emit, as multipliers of each face's 1x point size.
#
# The repo carries EVERY tier, not just the one the current build uses, because
# host builds pick their scale at compile time and there is no single right
# answer: the desktop simulator runs 2 (scripts/sim_render_scale.py) and the iOS
# target runs 3 (crosspoint-simulator/ios/CMakeLists.txt). A build references one
# tier and --gc-sections drops the rest; the DEVICE references none, since no
# device env sets CROSSPOINT_RENDER_SCALE at all.
#
# Adding a tier here is all that is needed on the generation side -- main.cpp
# pastes the suffix from CROSSPOINT_RENDER_SCALE rather than naming _2x, so it
# follows automatically.
# Overridable so a single tier can be regenerated without rewriting the others:
#   CROSSPOINT_HIRES_SCALES=3 ./convert-builtin-fonts.sh
# The 1x cuts are emitted by their own loops and always run.
HIRES_SCALES=(${CROSSPOINT_HIRES_SCALES:-2 3})

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
# Six sizes since 2026-08-26 (owner: "cut XS and XXS versions of every s tiers
# shipping font"). This ramp keeps its own 2 pt step and is NOT the SD
# families' ramp — the same slot reads one step larger here. See
# src/ReaderFontSizes.h for the measured offset.
# Adding one here also needs: a header include in builtinFonts/all.h (1x and
# every hi-res tier), an id in src/fontIds.h, the EpdFont/EpdFontFamily globals
# plus CP_READER_HIRES and the insertFont call in src/main.cpp, and a case in
# CrossPointSettings::getReaderFontId(). Miss the last one and the size is
# selectable but renders as 14 pt, silently.
LIBREFRANKLIN_READER_SIZES=(8 10 12 14 16 18)

for size in ${LIBREFRANKLIN_READER_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="librefranklin_reader_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/LibreFranklin/LibreFranklin-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum > $output_path
    echo "Generated $output_path"
  done
done

# --- Hi-res companions for the built-in READING face ------------------------
#
# librefranklin_reader had NONE until 2026-08-18, which meant the one reading
# face that ships in the firmware -- what you read a book in before choosing an
# SD font -- was blitted at 1x on a supersampled host. Every other family here
# already had its tiers; this one was simply missed, and nothing said so because
# drawText falls back silently.
#
# Device builds are unaffected either way: no device env sets
# CROSSPOINT_RENDER_SCALE, so these headers are never compiled into firmware.
for size in ${LIBREFRANKLIN_READER_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    for scale in ${HIRES_SCALES[@]}; do
      font_name="librefranklin_reader_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')_${scale}x"
      font_path="../builtinFonts/source/LibreFranklin/LibreFranklin-${style}.ttf"
      output_path="../builtinFonts/${font_name}.h"
      python fontconvert.py $font_name $((size * scale)) $font_path --2bit --compress --pnum > $output_path
      assert_header_clean "$output_path"
      echo "Generated $output_path"
    done
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
# TWO sizes, 12 and 14 (owner ruling 2026-08-15, "allow editor font to be
# resized"). 13 was asked for and declined: it exists in no ramp anywhere and
# SdCardFontSystem::loadForDisplay snaps to the nearest available size, so a 13
# row would have rendered 12 while claiming 13. The 16/18 cuts the reading faces
# carry are still dead flash and are still not built.
#
# Only the THREE WIRED families get 14. The retired ones below stay at 12: they
# are generatable-but-unwired, so a 14 cut would be disk and nothing else. All four styles are
# real: MarkdownSpans renders bold, italic and bold-italic. 2-bit + compressed,
# matching the reader cuts.
#
# Only iAWriterQuattro is still wired into the firmware (2026-08-15 ruling);
# the rest are kept generatable so a removed family stays one commit away.
# Historic flash cost: ~83 KB for SpaceMono + IBMPlexMono, ~216 KB for the iA
# Writer families (owner ruling 2026-08-11). The iA faces are the "S" (narrow)
# cuts; their URL-fetch recipes live in sd-fonts.yaml "Editor group", and the
# source TTFs for the builtin cuts are in builtinFonts/source/iAWriterXxx/.
#
# iA Writer font file name differs from the family name: the on-disk file is
# iAWriterQuattroS (with the S suffix for narrow), but we strip it for the
# source directory and internal symbol names to match sd-fonts.yaml's family
# name (iAWriterQuattro).
# The sizes the Editor Font Size setting offers. The setting persists a real
# POINT SIZE rather than a picker index, so this list is not order-frozen and a
# size may be added or dropped without a migration.
EDITOR_SIZES=(12 14)

EDITOR_FAMILIES=(
  "iawriterquattro:iAWriterQuattro/iAWriterQuattro"
)

# Quattro is DUOSPACE ('i' 7.50 px, 'm' 22.50 px at 12 pt) yet the source TTF
# gives '.' and ',' the full wide 15.00 px advance -- 5 px of ink centered in
# 5+5 px of side bearing, the most over-advanced punctuation in the build.
# Owner ruling 2026-08-22 (docs/punctuation-kerning-audit-2026-08-22.md,
# Standing rulings): '.' and ',' get the narrow i-width advance, ink
# re-centered, via regeneration. ONLY Quattro: PragmataPro and Nitti are true
# monospace, where the full advance is correct, and they stay untouched.
narrow_punct_flags_for() {
  case "$1" in
    iawriterquattro) echo '--narrow-punct .,' ;;
    *) echo '' ;;
  esac
}
# Removed from the app on 2026-08-15 but kept generatable, so a family stays one
# commit away from returning. 12 pt only -- nothing references these.
EDITOR_FAMILIES_RETIRED=(
  "spacemono:SpaceMono/SpaceMono"
  "ibmplexmono:IBMPlexMono/IBMPlexMono"
  "iawriterduo:iAWriterDuo/iAWriterDuo"
  "iawritermono:iAWriterMono/iAWriterMono"
)

for entry in ${EDITOR_FAMILIES[@]}; do
  prefix="${entry%%:*}"
  stem="${entry#*:}"
  for size in ${EDITOR_SIZES[@]}; do
    for style in ${READER_FONT_STYLES[@]}; do
      lc=$(echo $style | tr '[:upper:]' '[:lower:]')
      font_name="${prefix}_${size}_${lc}"
      output_path="../builtinFonts/${font_name}.h"
      python fontconvert.py $font_name $size "../builtinFonts/source/${stem}-${style}.ttf" --2bit --compress --pnum $(narrow_punct_flags_for $prefix) > $output_path
      echo "Generated $output_path"
    done
  done
done

for entry in ${EDITOR_FAMILIES_RETIRED[@]}; do
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
  for size in ${EDITOR_SIZES[@]}; do
    for scale in ${HIRES_SCALES[@]}; do
      for style in ${READER_FONT_STYLES[@]}; do
        lc=$(echo $style | tr '[:upper:]' '[:lower:]')
        font_name="${prefix}_${size}_${lc}_${scale}x"
        output_path="../builtinFonts/${font_name}.h"
        python fontconvert.py $font_name $((size * scale)) "../builtinFonts/source/${stem}-${style}.ttf" --2bit --compress --pnum $(narrow_punct_flags_for $prefix) > $output_path
        echo "Generated $output_path"
      done
    done
  done
done

# --- Editor-group monospace from LOCAL (commercial) sources ------------------
#
# PragmataPro is COMMERCIAL (Fabrizio Schiavi). Same rule as Edgar, GTAlpinaCond,
# Venetian301 and CaledoniaCC on the SD side: the RECIPE is committed, the font
# files never are. They go in lib/EpdFont/local_fonts/ (gitignored) under the
# names below.
#
# The generated pragmatapro_*.h are ALSO gitignored, which is what makes this
# different from every other family in this script. A 12/24 pt bitmap table of a
# commercial face is a redistributable derivative of it, and this repo is
# public — committing those headers would ship the typeface. So the headers are
# built locally and stay local, and `#if __has_include` in main.cpp lets a tree
# without them compile and run with the row degrading through
# editorfonts::resolve() to a built-in mono. See docs/sd-card-fonts.md.
#
# Skips with a clear message when the sources are absent — that means "cannot
# regenerate here", NOT "missing from the build".
LOCAL_EDITOR_FAMILIES=(
  "pragmatapro:PragmataPro"
)

for entry in ${LOCAL_EDITOR_FAMILIES[@]}; do
  prefix="${entry%%:*}"
  stem="${entry#*:}"
  missing=0
  for style in ${READER_FONT_STYLES[@]}; do
    [ -f "../local_fonts/${stem}-${style}.ttf" ] || missing=1
  done
  if [ $missing -eq 1 ]; then
    echo "Skipping ${stem} (no local source in lib/EpdFont/local_fonts/): commercial font, recipe only"
    continue
  fi
  for size in ${EDITOR_SIZES[@]}; do
    for style in ${READER_FONT_STYLES[@]}; do
      lc=$(echo $style | tr '[:upper:]' '[:lower:]')
      # 1x cut, then one hi-res companion per HIRES_SCALES tier, same flags as
      # the OFL editor families above.
      python fontconvert.py "${prefix}_${size}_${lc}" $size "../local_fonts/${stem}-${style}.ttf" --2bit --compress --pnum > "../builtinFonts/${prefix}_${size}_${lc}.h"
      echo "Generated ../builtinFonts/${prefix}_${size}_${lc}.h"
      for scale in ${HIRES_SCALES[@]}; do
        python fontconvert.py "${prefix}_${size}_${lc}_${scale}x" $((size * scale)) "../local_fonts/${stem}-${style}.ttf" --2bit --compress --pnum > "../builtinFonts/${prefix}_${size}_${lc}_${scale}x.h"
        echo "Generated ../builtinFonts/${prefix}_${size}_${lc}_${scale}x.h"
      done
    done
  done
done

# --- NittiTypewriter: commercial, three real cuts, no outline synthesis ------
#
# NittiTypewriter is COMMERCIAL (Pieter van Rosmalen / Bold Monday). Three cuts
# are available locally and all four styles are now drawn from a REAL one --
# nothing is emboldened or sheared any more (owner ruling 2026-08-15, replacing
# the embolden_em 0.045 / slant_deg 11 synthesis this block used to carry):
#
#   regular     NittiTypewriter-Regular.ttf, untouched
#   italic      NittiTypewriter-Underline.ttf, rule joined across the cell
#   bold        NittiTypewriter-Inverse.ttf -- the knockout cut, a solid cell
#               with the letterform punched out white. A typewriter had no bold
#               slug; on a monospace grid a knockout reads as emphasis without
#               the outline growth that would break the cell.
#   bolditalic  Underline again, joined, PLUS the double strike -- the same slug
#               hit twice with the carriage barely advanced, which is what
#               emphasis actually was on a typewriter.
#
# --synth-underline-connect: the Underline cut draws its rule inside each
# glyph's own bbox, so the rule stops short of the advance and the line breaks
# at every character (measured at 75 ppem: pen-x 3..41 on `a`, 5..39 on `n`,
# against an advance of 45). The flag replicates one of the rule's own interior
# columns out to -overlap .. advance+overlap. `left` moves; the advance does not.
#
# --synth-double-strike-em 0.0533: em-relative, so it is scale-independent by
# construction and the SAME number applies at every hi-res tier. It grows the
# BITMAP only -- outline emboldening moved the advance from 45.000 to 48.375 at
# 36 pt and broke the monospace grid, which is exactly why this is an overprint.
#
# --synth-underline-overlap-px 2 is ABSOLUTE pixels and is deliberately NOT
# scaled per tier. The overlap exists to defeat one rounding step when
# neighbouring cells are placed, and that error is ~1 device pixel at whatever
# resolution the tier rasterises at -- it does not grow with the tier. (Nitti's
# advance is an exact 15.000 px at 12 pt, so there is no fractional accumulation
# to round in the first place; 2 px is already insurance.) Scaling it would just
# widen every glyph bitmap at 2x/3x for nothing.
#
# --line-height, per size, matched to the iA Writer Quattro cut at the SAME
# size so the three editor faces stay interchangeable without reflowing.
# Measured from the generated Quattro headers (the advanceY field of
# EpdFontData): 33 at 12 pt, 38 at 14 pt. Nitti natively sets 24% denser than
# that -- 25 at 12 pt -- while matching on advance and x-height, so overriding
# the leading is what makes it a true drop-in rather than a different rhythm.
#
# Derived per size rather than scaled: 33 * 14/12 is 38.5, and rounding a
# derived leading is exactly how a family ends up one pixel off the set it is
# supposed to match. The number comes from the cut it has to sit beside.
#
# Skips with a clear message when any source is absent — that means
# "cannot regenerate here", NOT "missing from the build".
NITTI_REGULAR="../local_fonts/NittiTypewriter-Regular.ttf"
NITTI_UNDERLINE="../local_fonts/NittiTypewriter-Underline.ttf"
NITTI_INVERSE="../local_fonts/NittiTypewriter-Inverse.ttf"
NITTI_MISSING=0
for f in "$NITTI_REGULAR" "$NITTI_UNDERLINE" "$NITTI_INVERSE"; do
  [ -f "$f" ] || NITTI_MISSING=1
done
if [ $NITTI_MISSING -eq 1 ]; then
  echo "Skipping NittiTypewriter (no local source in lib/EpdFont/local_fonts/): commercial font, recipe only"
else
  NITTI_JOIN="--synth-underline-connect --synth-underline-overlap-px 2"
  # 1x cuts, then one hi-res companion per HIRES_SCALES tier. Same flags at
  # every tier: the strike is em-relative, the join is measured off the tier's
  # own advance, and the line height is the 1x metric the hi-res tables inherit.
  for size in ${EDITOR_SIZES[@]}; do
    case $size in
      12) NITTI_LH="--line-height 33" ;;
      14) NITTI_LH="--line-height 38" ;;
      *)  echo "No measured Nitti line height for ${size}pt -- generate the iA Writer Quattro cut at that size and read its advanceY" >&2; exit 1 ;;
    esac
    for scale in 1 ${HIRES_SCALES[@]}; do
      if [ "$scale" = "1" ]; then suffix=""; else suffix="_${scale}x"; fi
      px=$((size * scale))
      python fontconvert.py "nittitypewriter_${size}_regular${suffix}"    $px "$NITTI_REGULAR"   --2bit --compress --pnum $NITTI_LH > "../builtinFonts/nittitypewriter_${size}_regular${suffix}.h"
      echo "Generated ../builtinFonts/nittitypewriter_${size}_regular${suffix}.h"
      python fontconvert.py "nittitypewriter_${size}_bold${suffix}"       $px "$NITTI_INVERSE"   --2bit --compress --pnum $NITTI_LH > "../builtinFonts/nittitypewriter_${size}_bold${suffix}.h"
      echo "Generated ../builtinFonts/nittitypewriter_${size}_bold${suffix}.h"
      python fontconvert.py "nittitypewriter_${size}_italic${suffix}"     $px "$NITTI_UNDERLINE" --2bit --compress --pnum $NITTI_JOIN $NITTI_LH > "../builtinFonts/nittitypewriter_${size}_italic${suffix}.h"
      echo "Generated ../builtinFonts/nittitypewriter_${size}_italic${suffix}.h"
      python fontconvert.py "nittitypewriter_${size}_bolditalic${suffix}" $px "$NITTI_UNDERLINE" --2bit --compress --pnum $NITTI_JOIN --synth-double-strike-em 0.0533 $NITTI_LH > "../builtinFonts/nittitypewriter_${size}_bolditalic${suffix}.h"
      echo "Generated ../builtinFonts/nittitypewriter_${size}_bolditalic${suffix}.h"
    done
  done
fi

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
#
# NOTO SANS joins the matrix 2026-08-17 by owner instruction ("put noto sans in
# as builtin"). It is NOT a second chrome family -- Libre Franklin remains the
# only face the UI draws with (owner ruling 2026-08-07) -- it is the COVERAGE
# face behind it: 2,965 codepoints against Libre Franklin's 918, which is where
# Greek, Cyrillic and the rest of Latin Extended come from when a title or a
# filename has them.
#
# MEASURED AND STATED: Noto Sans carries 0 of the 128 codepoints in the Arrows
# block, so this does NOT put arrows in the chrome. Arrows exist only in
# NotoSansMath among the faces here. Adding this for arrows would be adding it
# for something it cannot do.
UI_FAMILIES=(
  "librefranklin:LibreFranklin/LibreFranklin"
  "notosans:NotoSans/NotoSans"
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
#
# LIBRE FRANKLIN ONLY, since 2026-08-22 (docs/ios-app-size.md A4 in the
# simulator repo). Noto Sans is the COVERAGE face, not a chrome face: it is
# only ever reached per glyph, for codepoints Libre Franklin's own cut lacks --
# and that cut already carries U+0400-04FF Cyrillic and most of Latin Extended,
# so what is left to Noto is Greek, sub/superscripts, currency and fractions.
# Its 2x + 3x companions were 4,325,323 bytes -- 21 % of the iOS binary -- for
# characters that are rare in a title and, without a companion, coarsen rather
# than vanish. Its 1x tier stays: that is what actually draws them, and hi-res
# companions carry bitmaps only.
HIRES_UI_FAMILIES=(
  "librefranklin:LibreFranklin/LibreFranklin"
)

for entry in ${HIRES_UI_FAMILIES[@]}; do
  prefix="${entry%%:*}"
  stem="${entry#*:}"
  for size in 8 10 12; do
    for style in ${UI_FONT_STYLES[@]}; do
      for scale in ${HIRES_SCALES[@]}; do
        font_name="${prefix}_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')_${scale}x"
        output_path="../builtinFonts/${font_name}.h"
        python fontconvert.py $font_name $((size * scale)) "../builtinFonts/source/${stem}-${style}.ttf" --2bit > $output_path
        echo "Generated $output_path"
      done
    done
  done
done

echo ""
echo "Checking every generated header starts with its banner..."
for header in ../builtinFonts/*.h; do
  case "$(basename "$header")" in
    all.h) continue ;;  # hand-written index, not generated
  esac
  assert_header_clean "$header"
done
echo "All headers clean."

echo ""
echo "Running compression verification..."
python verify_compression.py ../builtinFonts/
