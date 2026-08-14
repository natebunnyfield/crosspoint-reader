#!/usr/bin/env python3
"""Generate .cpfont binary files for SD card font loading.

Outputs binary .cpfont files containing glyph metadata and uncompressed
2-bit bitmaps, matching the EpdFontData/EpdGlyph/EpdUnicodeInterval struct
layout on the ESP32-C3 (little-endian, RISC-V).

Usage:
    # Single file with specific presets
    python fontconvert_sdcard.py \\
      --intervals latin-ext,greek,cyrillic \\
      --size 14 --style regular \\
      NotoSans-Regular.ttf \\
      -o NotoSansExt_14.cpfont

    # All 4 sizes at once
    python fontconvert_sdcard.py \\
      --intervals cjk \\
      --sizes 12,14,16,18 --style regular \\
      NotoSansCJKsc-Regular.otf \\
      --output-dir NotoSansCJK/

"""

from __future__ import annotations

import ctypes
import struct
import sys
import os
import re
import math
import argparse
from collections import namedtuple

from cpfont_version import CPFONT_VERSION

# --- Unicode interval presets ---

INTERVAL_PRESETS = {
    "ascii":       [(0x0020, 0x007E)],
    "latin1":      [(0x0080, 0x00FF)],
    "latin-ext":   [(0x0020, 0x007E), (0x0080, 0x00FF), (0x0100, 0x024F),
                    (0x02B0, 0x02FF), (0x1E00, 0x1EFF), (0x2000, 0x206F),
                    (0xFB00, 0xFB06)],
    "greek":       [(0x0370, 0x03FF), (0x1F00, 0x1FFF)],
    "cyrillic":    [(0x0400, 0x04FF), (0x0500, 0x052F)],
    "hebrew":      [(0x0590, 0x05FF), (0xFB1D, 0xFB4F)],
    "georgian":    [(0x10A0, 0x10FF), (0x2D00, 0x2D2F)],
    "armenian":    [(0x0530, 0x058F)],
    "ethiopic":    [(0x1200, 0x137F), (0x1380, 0x139F), (0x2D80, 0x2DDF)],
    "vietnamese":  [(0x01A0, 0x01B0), (0x1EA0, 0x1EF9)],
    "punctuation": [(0x2000, 0x206F)],
    "cjk":         [(0x3000, 0x303F), (0x3040, 0x309F), (0x30A0, 0x30FF),
                    (0x4E00, 0x9FFF), (0xF900, 0xFAFF), (0xFF00, 0xFFEF)],
    "hangul":      [(0xAC00, 0xD7AF), (0x1100, 0x11FF), (0x3130, 0x318F)],
    "cherokee":    [(0x13A0, 0x13FF), (0xAB70, 0xABBF)],
    "tifinagh":    [(0x2D30, 0x2D7F)],
    # Symbol blocks commonly seen in scifi/popsci/literary fiction.

    "symbols":     [(0x2070, 0x209F), (0x20A0, 0x20CF), (0x2150, 0x218F),
                    (0x2190, 0x21FF), (0x2200, 0x22FF), (0x2500, 0x257F),
                    (0x25A0, 0x25FF), (0x2600, 0x26FF), (0x2700, 0x27BF)],
    # Composite preset for English-language literary fiction including scifi/popsci.
    # Greek for physics terms, math operators, geometric shapes, uncommon
    # dialogue punctuation, CJK quote marks, miscellaneous symbols (♪♫♬), dingbats.
    "reading":     [(0x0020, 0x024F), (0x02B0, 0x02FF), (0x0300, 0x036F), (0x0370, 0x03FF),
                    (0x0400, 0x04FF), (0x1E00, 0x1EFF), (0x2000, 0x206F),
                    (0x2070, 0x209F), (0x20A0, 0x20CF), (0x2150, 0x218F),
                    (0x2190, 0x21FF), (0x2200, 0x22FF), (0x2500, 0x257F),
                    (0x25A0, 0x25FF), (0x2600, 0x26FF), (0x2700, 0x27BF),
                    (0x2900, 0x29FF), (0x2E00, 0x2E7F), (0x3000, 0x303F),
                    (0xFB00, 0xFB06)],
    # Matches the built-in font intervals from fontconvert.py exactly
    "builtin":     [(0x0000, 0x007F), (0x0080, 0x00FF), (0x0100, 0x017F),
                    (0x01A0, 0x01A1), (0x01AF, 0x01B0), (0x01C4, 0x021F),
                    (0x0300, 0x036F), (0x0400, 0x04FF),
                    (0x1EA0, 0x1EF9), (0x2000, 0x206F), (0x20A0, 0x20CF),
                    (0x2070, 0x209F), (0x2190, 0x21FF), (0x2200, 0x22FF),
                    (0xFB00, 0xFB06)],
}

# Regex for parsing unnamed hex range intervals: (0xSTART-0xEND)
_HEX_RANGE_PATTERN = re.compile(r'^\(0x([0-9a-fA-F]+)-0x([0-9a-fA-F]+)\)$')

def parse_hex_range(s: str) -> tuple[int, int] | None:
    match = _HEX_RANGE_PATTERN.fullmatch(s)
    if not match:
        return None

    start_hex, end_hex = match.groups()
    start, end = int(start_hex, 16), int(end_hex, 16)

    # Validating Unicode range bounds.
    if start > end or end > 0x10FFFF:
        return None
    return start, end


def resolve_intervals(preset_str):
    """Resolve comma-separated preset names into a merged, sorted, deduplicated interval list."""
    all_intervals = []
    for name in preset_str.split(","):
        name = name.strip().lower()
        unnamed_interval = parse_hex_range(name)
        if name not in INTERVAL_PRESETS and unnamed_interval is None:
            print(f"Error: unknown interval preset '{name}'", file=sys.stderr)
            print(f"Available presets: {', '.join(sorted(INTERVAL_PRESETS.keys()))}", file=sys.stderr)
            print("You can also specify unnamed hex ranges like (0x2100-0x214F)", file=sys.stderr)
            sys.exit(1)

        if unnamed_interval is not None:
            all_intervals.append(unnamed_interval)
        else:
            all_intervals.extend(INTERVAL_PRESETS[name])

    # Always add replacement character
    all_intervals.append((0xFFFD, 0xFFFD))

    # Sort and merge overlapping/adjacent intervals
    all_intervals.sort()
    merged = []
    for start, end in all_intervals:
        if merged and start <= merged[-1][1] + 1:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
        else:
            merged.append((start, end))
    return merged


GlyphProps = namedtuple("GlyphProps", [
    "width", "height", "advance_x", "left", "top", "data_length", "data_offset", "code_point"
])

# Intermediate data from rasterizing one font style
StyleRasterData = namedtuple("StyleRasterData", [
    "style_id",                # 0=regular, 1=bold, 2=italic, 3=bolditalic
    "intervals",               # validated intervals [(start, end), ...]
    "all_glyphs",              # [(GlyphProps, packed_bytes), ...]
    "total_bitmap_size",       # int
    "advanceY", "ascender", "descender",
    "kern_left_classes", "kern_right_classes", "kern_matrix",
    "kern_left_class_count", "kern_right_class_count",
    "ligature_pairs",
])


def norm_floor(val):
    return int(math.floor(val / (1 << 6)))


def norm_ceil(val):
    return int(math.ceil(val / (1 << 6)))


# Fixed-point (fp4) output conventions (must match EpdFontData.h / fp4 namespace):
#
#   advanceX    12.4 unsigned fixed-point (uint16_t).
#               12 integer bits, 4 fractional bits = 1/16-pixel resolution.
#               Encoded from FreeType's 16.16 linearHoriAdvance.
#
#   kernMatrix  4.4 signed fixed-point (int8_t).
#               4 integer bits, 4 fractional bits = 1/16-pixel resolution.
#               Range: -8.0 to +7.9375 pixels.
#               Encoded from font design-unit kerning values.
#
# Both share 4 fractional bits so the renderer can add them directly into a
# single int32_t accumulator and defer rounding until pixel placement.

def fp4_from_ft16_16(val):
    """Convert FreeType 16.16 fixed-point to 12.4 fixed-point with rounding."""
    return (val + (1 << 11)) >> 12

def fp4_from_design_units(du, scale):
    """Convert a font design-unit value to 4.4 fixed-point, clamped to int8_t.

    Multiplies by scale (ppem / units_per_em) and shifts into 4 fractional
    bits.  The result is rounded to nearest and clamped to [-128, 127].
    """
    raw = round(du * scale * 16)
    return max(-128, min(127, raw))


# Standard Unicode ligature codepoints for known input sequences.
# Used as a fallback when the GSUB substitute glyph has no cmap entry.
STANDARD_LIGATURE_MAP = {
    (0x66, 0x66):       0xFB00,  # ff
    (0x66, 0x69):       0xFB01,  # fi
    (0x66, 0x6C):       0xFB02,  # fl
    (0x66, 0x66, 0x69): 0xFB03,  # ffi
    (0x66, 0x66, 0x6C): 0xFB04,  # ffl
    (0x17F, 0x74):      0xFB05,  # long-s + t
    (0x73, 0x74):       0xFB06,  # st
}


def extract_ligature_glyph_indices_fonttools(font_path):
    """Map standard ligature codepoints without a cmap entry to their glyph IDs.

    Most fonts use unencoded glyphs such as ``f_f`` for standard ligatures.
    The reader represents those substitutions with the Unicode compatibility
    codepoints, so rasterize the GSUB target directly instead of falling back
    to another font for a codepoint that the primary cmap does not contain.
    """
    from fontTools.ttLib import TTFont

    font = TTFont(font_path)
    cmap = font.getBestCmap() or {}
    glyph_to_cp = {gname: cp for cp, gname in cmap.items()}
    glyph_indices = {gname: index for index, gname in enumerate(font.getGlyphOrder())}
    overrides = {}

    if 'GSUB' in font:
        gsub = font['GSUB'].table
        liga_lookup_indices = set()
        if gsub.FeatureList:
            for fr in gsub.FeatureList.FeatureRecord:
                if fr.FeatureTag in ('liga', 'rlig'):
                    liga_lookup_indices.update(fr.Feature.LookupListIndex)

        for li in liga_lookup_indices:
            lookup = gsub.LookupList.Lookup[li]
            for st in lookup.SubTable:
                actual = st.ExtSubTable if lookup.LookupType == 7 and hasattr(st, 'ExtSubTable') else st
                if not hasattr(actual, 'ligatures'):
                    continue
                for first_glyph, ligature_list in actual.ligatures.items():
                    if first_glyph not in glyph_to_cp:
                        continue
                    for lig in ligature_list:
                        if any(component not in glyph_to_cp for component in lig.Component):
                            continue
                        seq = tuple([glyph_to_cp[first_glyph]] + [glyph_to_cp[component] for component in lig.Component])
                        lig_cp = STANDARD_LIGATURE_MAP.get(seq)
                        if lig_cp is not None and lig_cp not in cmap:
                            overrides[lig_cp] = glyph_indices[lig.LigGlyph]

    font.close()
    return overrides


def _extract_pairpos_subtable(subtable, glyph_to_cp, raw_kern):
    """Extract kerning from a PairPos subtable (Format 1 or 2)."""
    if subtable.Format == 1:
        # Individual pairs
        for i, coverage_glyph in enumerate(subtable.Coverage.glyphs):
            if coverage_glyph not in glyph_to_cp:
                continue
            pair_set = subtable.PairSet[i]
            for pvr in pair_set.PairValueRecord:
                if pvr.SecondGlyph not in glyph_to_cp:
                    continue
                xa = 0
                if hasattr(pvr, 'Value1') and pvr.Value1:
                    xa = getattr(pvr.Value1, 'XAdvance', 0) or 0
                if xa != 0:
                    key = (coverage_glyph, pvr.SecondGlyph)
                    raw_kern[key] = raw_kern.get(key, 0) + xa
    elif subtable.Format == 2:
        # Class-based pairs — iterate by class, not by glyph, to avoid
        # O(glyphs²) explosion for CJK fonts with many requested glyphs.
        class_def1 = subtable.ClassDef1.classDefs if subtable.ClassDef1 else {}
        class_def2 = subtable.ClassDef2.classDefs if subtable.ClassDef2 else {}
        coverage_set = set(subtable.Coverage.glyphs)

        # Build reverse mappings: class_id -> list of glyph names
        left_by_class = {}   # only glyphs in coverage AND glyph_to_cp
        for glyph in glyph_to_cp:
            if glyph not in coverage_set:
                continue
            c1 = class_def1.get(glyph, 0)
            left_by_class.setdefault(c1, []).append(glyph)

        right_by_class = {}  # all glyphs in glyph_to_cp
        for glyph in glyph_to_cp:
            c2 = class_def2.get(glyph, 0)
            right_by_class.setdefault(c2, []).append(glyph)

        # Iterate class pairs (typically << glyph pairs)
        for c1, class1_rec in enumerate(subtable.Class1Record):
            if c1 not in left_by_class:
                continue
            for c2, c2_rec in enumerate(class1_rec.Class2Record):
                xa = 0
                if hasattr(c2_rec, 'Value1') and c2_rec.Value1:
                    xa = getattr(c2_rec.Value1, 'XAdvance', 0) or 0
                if xa == 0:
                    continue
                if c2 not in right_by_class:
                    continue
                for lg in left_by_class[c1]:
                    for rg in right_by_class[c2]:
                        key = (lg, rg)
                        raw_kern[key] = raw_kern.get(key, 0) + xa


def extract_kerning_fonttools(font_path, codepoints, ppem):
    """Extract kerning pairs from a font file using fonttools.

    Returns dict of {(leftCp, rightCp): pixel_adjust} for the given
    codepoints.  Values are scaled from font design units to integer
    pixels at ppem.
    """
    from fontTools.ttLib import TTFont

    font = TTFont(font_path)
    units_per_em = font['head'].unitsPerEm
    cmap = font.getBestCmap() or {}

    # Build glyph_name -> [codepoints] map (preserves aliases where multiple
    # codepoints share a glyph, e.g. space/nbsp)
    glyph_to_cps = {}
    for cp in codepoints:
        gname = cmap.get(cp)
        if gname:
            glyph_to_cps.setdefault(gname, []).append(cp)
    # Flat dict for membership checks and subtable extraction (uses keys only)
    glyph_to_cp = glyph_to_cps

    # Collect raw kerning values in font design units.
    #
    # The two sources are collected SEPARATELY and then overlaid, GPOS winning,
    # rather than summed into one dict. A font that predates OpenType layout
    # commonly ships the SAME pairs twice — once in the legacy `kern` table for
    # non-OT clients, once in GPOS — and summing them applies the designer's
    # correction twice. LibrisADF is such a font: `kern` and the GPOS kern
    # feature carry byte-identical pair sets (1053 pairs, regular), so T+o came
    # out at -214/1000 em instead of -107 and every T/Y pair saturated the
    # int8_t 4.4 floor (-8.0 px) at 18 pt. Overlaying is also what the OpenType
    # spec asks for: where both exist, GPOS supersedes `kern`.
    #
    # Legacy-ONLY pairs still survive the overlay, which is what
    # optical_kern.py depends on — it writes its synthesised pairs into a fresh
    # `kern` table and never emits a pair the designer already ruled on in GPOS
    # (optical_kern.py:390-393), so nothing it produces is shadowed here.
    raw_kern = {}   # (left_glyph_name, right_glyph_name) -> design_units
    gpos_kern = {}  # same, GPOS only — overlaid onto raw_kern below

    # 1. Legacy kern table
    if 'kern' in font:
        for subtable in font['kern'].kernTables:
            if hasattr(subtable, 'kernTable'):
                for (lg, rg), val in subtable.kernTable.items():
                    if lg in glyph_to_cp and rg in glyph_to_cp:
                        raw_kern[(lg, rg)] = raw_kern.get((lg, rg), 0) + val

    # 2. GPOS 'kern' feature
    if 'GPOS' in font:
        gpos = font['GPOS'].table
        kern_lookup_indices = set()
        if gpos.FeatureList:
            for fr in gpos.FeatureList.FeatureRecord:
                if fr.FeatureTag == 'kern':
                    kern_lookup_indices.update(fr.Feature.LookupListIndex)
        for li in kern_lookup_indices:
            lookup = gpos.LookupList.Lookup[li]
            for st in lookup.SubTable:
                actual = st
                # Unwrap Extension (lookup type 9) wrappers. After unwrapping,
                # `lookup.LookupType` is still 9, so we must look at the
                # *effective* type carried on the extension subtable to know
                # whether `actual` is a PairPos table.
                if lookup.LookupType == 9 and hasattr(st, 'ExtSubTable'):
                    actual = st.ExtSubTable
                effective_type = getattr(st, 'ExtensionLookupType', lookup.LookupType)
                if hasattr(actual, 'Format'):
                    # _extract_pairpos_subtable assumes a Type-2 (PairPos)
                    # subtable. Other lookup types reachable through the kern
                    # feature (cursive attachment, mark-to-mark, contextual,
                    # etc.) have a different shape and crash inside the
                    # extractor. Skip them with a debug note rather than
                    # aborting the whole build. Modern fonts often ship kern
                    # via Extension-wrapped PairPos, so checking the effective
                    # type instead of the outer type is what makes those
                    # lookups actually reach the extractor.
                    if effective_type == 2:
                        _extract_pairpos_subtable(actual, glyph_to_cp, gpos_kern)
                    else:
                        print(f"  Debug: skipping unsupported GPOS kern lookupType="
                              f"{effective_type} (outer={lookup.LookupType}, Format={actual.Format})",
                              file=sys.stderr)

    font.close()

    # GPOS supersedes the legacy table pair-for-pair; legacy-only pairs remain.
    shadowed = sum(1 for k in gpos_kern if k in raw_kern)
    if shadowed:
        print(f"  Debug: {shadowed} pair(s) present in BOTH the legacy kern table and "
              f"GPOS; GPOS wins (summing them would double the designer's correction)",
              file=sys.stderr)
    raw_kern.update(gpos_kern)

    # Scale design-unit kerning values to 4.4 fixed-point pixels.
    # Expand glyph aliases: if multiple codepoints share a glyph, emit kern
    # pairs for all codepoint combinations.
    scale = ppem / units_per_em
    result = {}  # (leftCp, rightCp) -> 4.4 fixed-point adjust
    for (lg, rg), du in raw_kern.items():
        adjust = fp4_from_design_units(du, scale)
        if adjust != 0:
            for lcp in glyph_to_cps[lg]:
                for rcp in glyph_to_cps[rg]:
                    result[(lcp, rcp)] = adjust
    return result


def derive_kern_classes(kern_map):
    """Derive class-based kerning from a pair map.

    Returns (kern_left_classes, kern_right_classes, kern_matrix,
             kern_left_class_count, kern_right_class_count) where:
    - kern_left_classes: sorted list of (codepoint, classId) tuples
    - kern_right_classes: sorted list of (codepoint, classId) tuples
    - kern_matrix: flat list of int8 values (left_class_count * right_class_count)
    - kern_left_class_count: number of distinct left classes
    - kern_right_class_count: number of distinct right classes
    """
    if not kern_map:
        return [], [], [], 0, 0

    all_left_cps = {lcp for lcp, _ in kern_map}
    all_right_cps = {rcp for _, rcp in kern_map}

    sorted_right_cps = sorted(all_right_cps)
    sorted_left_cps = sorted(all_left_cps)

    # Group left codepoints by identical adjustment row
    left_profile_to_class = {}
    left_class_map = {}
    left_class_id = 1
    for lcp in sorted(all_left_cps):
        row = tuple(kern_map.get((lcp, rcp), 0) for rcp in sorted_right_cps)
        if row not in left_profile_to_class:
            left_profile_to_class[row] = left_class_id
            left_class_id += 1
        left_class_map[lcp] = left_profile_to_class[row]

    # Group right codepoints by identical adjustment column
    right_profile_to_class = {}
    right_class_map = {}
    right_class_id = 1
    for rcp in sorted(all_right_cps):
        col = tuple(kern_map.get((lcp, rcp), 0) for lcp in sorted_left_cps)
        if col not in right_profile_to_class:
            right_profile_to_class[col] = right_class_id
            right_class_id += 1
        right_class_map[rcp] = right_profile_to_class[col]

    kern_left_class_count = left_class_id - 1
    kern_right_class_count = right_class_id - 1

    if kern_left_class_count > 255 or kern_right_class_count > 255:
        print(f"WARNING: kerning class count exceeds uint8_t range "
              f"(left={kern_left_class_count}, right={kern_right_class_count}), "
              f"dropping kerning for this style",
              file=sys.stderr)
        return ([], [], [], 0, 0)

    # Build the class x class matrix
    kern_matrix = [0] * (kern_left_class_count * kern_right_class_count)
    for (lcp, rcp), adjust in kern_map.items():
        lc = left_class_map[lcp] - 1
        rc = right_class_map[rcp] - 1
        kern_matrix[lc * kern_right_class_count + rc] = adjust

    # Build sorted class entry lists
    kern_left_classes = sorted(left_class_map.items())
    kern_right_classes = sorted(right_class_map.items())

    return (kern_left_classes, kern_right_classes, kern_matrix,
            kern_left_class_count, kern_right_class_count)


def extract_ligatures_fonttools(font_path, codepoints):
    """Extract ligature substitution pairs from a font file using fonttools.

    Returns list of (packed_pair, ligature_codepoint) for the given codepoints.
    Multi-character ligatures are decomposed into chained pairs.
    """
    from fontTools.ttLib import TTFont

    font = TTFont(font_path)
    cmap = font.getBestCmap() or {}

    # Build glyph_name -> codepoint and codepoint -> glyph_name maps
    glyph_to_cp = {}
    cp_to_glyph = {}
    for cp, gname in cmap.items():
        glyph_to_cp[gname] = cp
        cp_to_glyph[cp] = gname

    # Collect raw ligature rules: (sequence_of_codepoints) -> ligature_codepoint
    raw_ligatures = {}  # tuple of codepoints -> ligature codepoint

    if 'GSUB' in font:
        gsub = font['GSUB'].table

        LIGATURE_FEATURES = ('liga', 'rlig')
        liga_lookup_indices = set()
        if gsub.FeatureList:
            for fr in gsub.FeatureList.FeatureRecord:
                if fr.FeatureTag in LIGATURE_FEATURES:
                    liga_lookup_indices.update(fr.Feature.LookupListIndex)

        for li in liga_lookup_indices:
            lookup = gsub.LookupList.Lookup[li]
            for st in lookup.SubTable:
                actual = st
                # Unwrap Extension (lookup type 7) wrappers
                if lookup.LookupType == 7 and hasattr(st, 'ExtSubTable'):
                    actual = st.ExtSubTable
                # LigatureSubst is lookup type 4
                if not hasattr(actual, 'ligatures'):
                    continue
                for first_glyph, ligature_list in actual.ligatures.items():
                    if first_glyph not in glyph_to_cp:
                        continue
                    first_cp = glyph_to_cp[first_glyph]
                    for lig in ligature_list:
                        component_cps = []
                        valid = True
                        for comp_glyph in lig.Component:
                            if comp_glyph not in glyph_to_cp:
                                valid = False
                                break
                            component_cps.append(glyph_to_cp[comp_glyph])
                        if not valid:
                            continue
                        seq = tuple([first_cp] + component_cps)
                        if lig.LigGlyph in glyph_to_cp:
                            lig_cp = glyph_to_cp[lig.LigGlyph]
                        elif seq in STANDARD_LIGATURE_MAP:
                            lig_cp = STANDARD_LIGATURE_MAP[seq]
                        else:
                            seq_str = ', '.join(f'U+{cp:04X}' for cp in seq)
                            print(f"ligatures: WARNING: dropping ligature ({seq_str}) -> "
                                  f"glyph '{lig.LigGlyph}': output glyph has no cmap entry "
                                  f"and input sequence is not in STANDARD_LIGATURE_MAP",
                                  file=sys.stderr)
                            continue
                        raw_ligatures[seq] = lig_cp

    font.close()

    # Filter: only keep ligatures where all input and output codepoints are
    # in our generated glyph set, and all codepoints fit in 16 bits.
    #
    # The on-disk format packs each component as a uint16 (the 3+ chained
    # path packs `intermediate_cp << 16 | last_cp`, where `intermediate_cp`
    # is the lig_cp of the prefix). Dropping any seq with an SMP cp here —
    # plus any lig_cp > 0xFFFF — means every cp that reaches `packed = … <<
    # 16 | …` below is already 16-bit safe, including the chained path
    # (intermediate_cp = filtered[prefix] is filtered too).
    codepoints_set = set(codepoints)
    filtered = {}
    for seq, lig_cp in raw_ligatures.items():
        if lig_cp not in codepoints_set or lig_cp > 0xFFFF:
            continue
        if any(cp > 0xFFFF for cp in seq):
            continue
        if all(cp in codepoints_set for cp in seq):
            filtered[seq] = lig_cp

    # Decompose into chained pairs
    pairs = []
    # First pass: collect all 2-codepoint ligatures
    two_char = {seq: lig_cp for seq, lig_cp in filtered.items() if len(seq) == 2}
    for seq, lig_cp in two_char.items():
        packed = (seq[0] << 16) | seq[1]
        pairs.append((packed, lig_cp))

    # Second pass: decompose 3+ codepoint ligatures into chained pairs
    for seq, lig_cp in filtered.items():
        if len(seq) < 3:
            continue
        prefix = seq[:-1]
        last_cp = seq[-1]
        if prefix in filtered:
            intermediate_cp = filtered[prefix]
            packed = (intermediate_cp << 16) | last_cp
            pairs.append((packed, lig_cp))
        else:
            print(f"ligatures: skipping {len(seq)}-char ligature "
                  f"({', '.join(f'U+{cp:04X}' for cp in seq)}) -> U+{lig_cp:04X}: "
                  f"no intermediate ligature for prefix", file=sys.stderr)

    # Sort by packed pair key — on-device lookup uses binary search
    pairs.sort(key=lambda p: p[0])
    return pairs


def synth_params_for_size(synthetic, size):
    """Compute per-size synthetic-style parameters in FreeType units.

    `synthetic` is a dict with optional keys:
      embolden_em  x-strength as a fraction of the em (0 = no embolden)
      y_ratio      y-strength as a fraction of x-strength (default 0.5 —
                   vertical growth eats x-height and closes counters, so it
                   gets half strength)
      slant_deg    oblique shear angle in degrees (0 = no shear)

    Returns (x_strength, y_strength, shear_xy) where the strengths are 26.6
    pixel units at this size's ppem (the converter renders at 150 DPI, so
    ppem = size * 150 / 72) and shear_xy is the 16.16 matrix coefficient.
    """
    ppem = size * 150.0 / 72.0
    embolden_em = float(synthetic.get("embolden_em", 0.0))
    y_ratio = float(synthetic.get("y_ratio", 0.5))
    slant_deg = float(synthetic.get("slant_deg", 0.0))
    x_strength = int(round(embolden_em * ppem * 64))
    y_strength = int(round(x_strength * y_ratio))
    shear_xy = int(round(math.tan(math.radians(slant_deg)) * 0x10000)) if slant_deg else 0
    return x_strength, y_strength, shear_xy


def spacing_params_for_size(spacing, ppem):
    """Per-size advance deltas, in FreeType 16.16, from an em-relative spec.

    Returns (tracking, word_space): tracking is added to EVERY glyph's advance,
    word_space additionally to U+0020/U+00A0 only. Both are advance-only --
    outlines and bitmaps are untouched, so this letterspaces a face without
    distorting a single letterform.

    Exists for MIXED-SOURCE families. A borrowed italic keeps its own donor's
    fit and word space, which are drawn against a different roman: Junicode's
    space measures 0.32 em against Inknut's 0.28, so an italic phrase inside an
    Inknut sentence opens up at every word boundary while the letters sit right.
    Neither `scale:` nor the wdth axis can fix that -- both move the space in
    proportion with everything else.
    """
    if not spacing:
        return 0, 0
    track = int(round(spacing.get("tracking_em", 0.0) * ppem * 65536))
    word = int(round(spacing.get("word_space_em", 0.0) * ppem * 65536))
    return track, word


def rasterize_font_style(fontfile, size, intervals, style_id=0, force_autohint=False,
                         fallback_fontfile=None, synthetic=None, spacing=None):
    """Rasterize all glyphs for one font style. Returns StyleRasterData.

    `synthetic`, when set, is a dict (see synth_params_for_size) describing a
    build-time synthetic style: glyph outlines are emboldened and/or sheared
    before rendering, per docs/synthetic-font-styles.md.

    `spacing`, when set, is a dict (see spacing_params_for_size) of advance-only
    tracking and word-space deltas.
    """
    import freetype

    style_names = {0: "regular", 1: "bold", 2: "italic", 3: "bolditalic"}
    style_label = style_names.get(style_id, str(style_id))

    face = freetype.Face(fontfile)
    # Set font size at 150 DPI (matching fontconvert.py) BEFORE any glyph load.
    # load_glyph() with FT_LOAD_RENDER renders at the active size, so calling
    # it before set_char_size() would waste work at the default size and risk
    # Invalid_Size_Handle on some fonts.
    face.set_char_size(size << 6, size << 6, 150, 150)
    ligature_glyph_indices = extract_ligature_glyph_indices_fonttools(fontfile)
    fallback_face = None
    if fallback_fontfile:
        fallback_face = freetype.Face(fallback_fontfile)
        fallback_face.set_char_size(size << 6, size << 6, 150, 150)

    # Synthetic styles must split load from render so the outline can be
    # modified in between; real styles keep the single-call FT_LOAD_RENDER
    # fast path.
    track_adv, word_adv = spacing_params_for_size(spacing, face.size.x_ppem)
    if track_adv or word_adv:
        print(f"  [{style_label}] Spacing: tracking={track_adv / 65536:.2f} px, "
              f"word space {word_adv / 65536:+.2f} px (advance only)", file=sys.stderr)

    synth_x, synth_y, synth_shear = (0, 0, 0)
    if synthetic:
        synth_x, synth_y, synth_shear = synth_params_for_size(synthetic, size)
        print(f"  [{style_label}] Synthetic: embolden x={synth_x} y={synth_y} (26.6 px units), "
              f"shear xy={synth_shear} (16.16)", file=sys.stderr)
        load_flags = freetype.FT_LOAD_DEFAULT | freetype.FT_LOAD_NO_BITMAP
    else:
        load_flags = freetype.FT_LOAD_RENDER
    if force_autohint:
        load_flags |= freetype.FT_LOAD_FORCE_AUTOHINT

    def apply_synthetic_and_render(f):
        """Embolden (centered) then shear the loaded outline, then render.

        Order matters: embolden-then-shear — shearing first distorts the
        stroke stress that the embolden then amplifies. Fallback-font glyphs
        get the same treatment; the strengths are pixel-space (26.6 at this
        ppem), so they apply uniformly regardless of the source font's UPM.
        """
        slot = f.glyph
        if slot.format == freetype.FT_GLYPH_FORMAT_OUTLINE:
            outline_ref = ctypes.byref(slot.outline._FT_Outline)
            if synth_x or synth_y:
                err = freetype.FT_Outline_EmboldenXY(outline_ref, synth_x, synth_y)
                if err:
                    raise RuntimeError(
                        f"FT_Outline_EmboldenXY failed (error {err}) for style {style_label}")
                # Center the growth (KOReader's trick) so bearings stay honest.
                freetype.FT_Outline_Translate(outline_ref, -(synth_x // 2), -(synth_y // 2))
            if synth_shear:
                m = freetype.FT_Matrix()
                m.xx = 0x10000
                m.xy = synth_shear
                m.yx = 0
                m.yy = 0x10000
                freetype.FT_Outline_Transform(outline_ref, ctypes.byref(m))
            err = freetype.FT_Render_Glyph(slot._FT_GlyphSlot, freetype.FT_RENDER_MODE_NORMAL)
            if err:
                raise RuntimeError(
                    f"FT_Render_Glyph failed (error {err}) for style {style_label}")

    def load_glyph(code_point):
        glyph_index = face.get_char_index(code_point)
        if glyph_index == 0:
            glyph_index = ligature_glyph_indices.get(code_point, 0)
        if glyph_index > 0:
            face.load_glyph(glyph_index, load_flags)
            if synthetic:
                apply_synthetic_and_render(face)
            return face
        if fallback_face:
            fallback_glyph_index = fallback_face.get_char_index(code_point)
            if fallback_glyph_index > 0:
                fallback_face.load_glyph(fallback_glyph_index, load_flags)
                if synthetic:
                    apply_synthetic_and_render(fallback_face)
                return fallback_face
        return None

    # Validate intervals: remove codepoints not present in the font.
    # Only check glyph existence via get_char_index — do NOT call
    # load_glyph here, as that triggers FT_LOAD_RENDER at the target
    # DPI and doubles total rasterization time for no benefit.
    print(f"  [{style_label}] Validating intervals against font...", file=sys.stderr)
    validated_intervals = []
    for i_start, i_end in intervals:
        start = i_start
        for code_point in range(i_start, i_end + 1):
            has_primary = face.get_char_index(code_point) != 0 or code_point in ligature_glyph_indices
            has_fallback = fallback_face and fallback_face.get_char_index(code_point) != 0
            if not has_primary and not has_fallback:
                if start < code_point:
                    validated_intervals.append((start, code_point - 1))
                start = code_point + 1
        if start <= i_end:
            validated_intervals.append((start, i_end))

    intervals = validated_intervals
    total_glyphs = sum(end - start + 1 for start, end in intervals)
    print(f"  [{style_label}] Validated: {len(intervals)} intervals, {total_glyphs} glyphs", file=sys.stderr)

    # Rasterize all glyphs
    total_bitmap_size = 0
    all_glyphs = []

    for i_start, i_end in intervals:
        for code_point in range(i_start, i_end + 1):
            f = load_glyph(code_point)
            if f is None:
                glyph = GlyphProps(0, 0, 0, 0, 0, 0, total_bitmap_size, code_point)
                all_glyphs.append((glyph, b''))
                continue

            bitmap = f.glyph.bitmap

            # Build 4-bit grayscale bitmap (same logic as fontconvert.py).
            #
            # FreeType returns the buffer with bitmap.pitch as the row stride
            # in bytes, which can be negative when the bitmap is stored
            # bottom-up. Iterating bitmap.buffer linearly assumes
            # pitch == width and a top-down layout — that holds in the common
            # case but breaks on padded or flipped bitmaps and corrupts the
            # output. Walk by (row, col) using the real pitch instead.
            #
            # Cache bitmap.buffer in a local — ctypes struct field access
            # creates a new Python wrapper object each time, so re-evaluating
            # it per pixel is catastrophically slow.
            pixels4g = []
            px = 0
            buf = bitmap.buffer
            abs_pitch = abs(bitmap.pitch)
            for y in range(bitmap.rows):
                row_offset = y * abs_pitch if bitmap.pitch >= 0 else (bitmap.rows - 1 - y) * abs_pitch
                for x in range(bitmap.width):
                    v = buf[row_offset + x]
                    if x % 2 == 0:
                        px = (v >> 4)
                    else:
                        px = px | (v & 0xF0)
                        pixels4g.append(px)
                        px = 0
                if bitmap.width % 2 > 0:
                    pixels4g.append(px)
                    px = 0

            # Downsample to 2-bit bitmap
            pixels2b = []
            px = 0
            pitch = (bitmap.width // 2) + (bitmap.width % 2)
            for y in range(bitmap.rows):
                for x in range(bitmap.width):
                    px = px << 2
                    bm = pixels4g[y * pitch + (x // 2)]
                    bm = (bm >> ((x % 2) * 4)) & 0xF

                    if bm >= 12:
                        px += 3
                    elif bm >= 8:
                        px += 2
                    elif bm >= 4:
                        px += 1

                    if (y * bitmap.width + x) % 4 == 3:
                        pixels2b.append(px)
                        px = 0
            if (bitmap.width * bitmap.rows) % 4 != 0:
                # Outer parens are for clarity: in Python `*` binds tighter
                # than `<<`, so the original `px << (4 - … % 4) * 2` already
                # evaluates as `px << ((4 - … % 4) * 2)`. Match the explicit
                # bracketing here so the shift width is obvious at a glance,
                # mirroring the inner-loop style in fontconvert.py.
                px = px << ((4 - (bitmap.width * bitmap.rows) % 4) * 2)
                pixels2b.append(px)

            packed = bytes(pixels2b)
            # Advance compensation for synthetic embolden: add the full
            # x-strength to the advance (FreeType and KOReader both do;
            # skipping it is what makes faux bold look cramped). The
            # strength is 26.6, linearHoriAdvance is 16.16: << 10 converts.
            # Advance-only spacing. Tracking lands on every glyph; the word-space
            # delta only on the two space characters, so the two knobs stay
            # independent -- tracking a face tighter must not also close its word
            # gaps. Clamped at zero: a negative advance would walk the cursor
            # backwards and overprint the previous glyph.
            spacing_adv = track_adv
            if code_point in (0x20, 0xA0):
                spacing_adv += word_adv
            advance_16_16 = max(0, f.glyph.linearHoriAdvance + (synth_x << 10) + spacing_adv)

            glyph = GlyphProps(
                width=bitmap.width,
                height=bitmap.rows,
                advance_x=fp4_from_ft16_16(advance_16_16),
                left=f.glyph.bitmap_left,
                top=f.glyph.bitmap_top,
                data_length=len(packed),
                data_offset=total_bitmap_size,
                code_point=code_point,
            )
            total_bitmap_size += len(packed)
            all_glyphs.append((glyph, packed))

    # Get font metrics from pipe character (same heuristic as fontconvert.py)
    load_glyph(ord('|'))

    advanceY = norm_ceil(face.size.height)
    ascender = norm_ceil(face.size.ascender)
    descender = norm_floor(face.size.descender)

    print(f"  [{style_label}] Metrics: advanceY={advanceY}, ascender={ascender}, descender={descender}", file=sys.stderr)
    print(f"  [{style_label}] Bitmap: {total_bitmap_size} bytes ({total_bitmap_size / 1024:.1f} KB)", file=sys.stderr)

    # --- Extract kerning and ligatures ---
    ppem = size * 150.0 / 72.0
    all_cps = set(g.code_point for g, _ in all_glyphs)

    kern_map = extract_kerning_fonttools(fontfile, all_cps, ppem)
    # SMP codepoints (> U+FFFF) cannot be stored in the uint16 kern codepoint
    # field; drop them before class derivation to avoid a downstream
    # struct.error when packing the binary kern tables.
    kern_map = {(lcp, rcp): v for (lcp, rcp), v in kern_map.items() if lcp <= 0xFFFF and rcp <= 0xFFFF}
    print(f"  [{style_label}] Kerning: {len(kern_map)} pairs extracted", file=sys.stderr)

    (kern_left_classes, kern_right_classes, kern_matrix,
     kern_left_class_count, kern_right_class_count) = derive_kern_classes(kern_map)

    if kern_map:
        matrix_size = kern_left_class_count * kern_right_class_count
        entries_size = (len(kern_left_classes) + len(kern_right_classes)) * 3
        print(f"  [{style_label}] Kerning classes: {kern_left_class_count} left, {kern_right_class_count} right, "
              f"{matrix_size + entries_size} bytes", file=sys.stderr)

    # SMP codepoints in ligature inputs / outputs are filtered inside
    # extract_ligatures_fonttools (see the codepoints_set filter), so every
    # entry returned here is already 16-bit safe.
    ligature_pairs = extract_ligatures_fonttools(fontfile, all_cps)
    if len(ligature_pairs) > 255:
        print(f"  [{style_label}] WARNING: {len(ligature_pairs)} ligature pairs exceeds uint8_t max (255), truncating",
              file=sys.stderr)
        ligature_pairs = ligature_pairs[:255]
    print(f"  [{style_label}] Ligatures: {len(ligature_pairs)} pairs", file=sys.stderr)

    return StyleRasterData(
        style_id=style_id,
        intervals=intervals,
        all_glyphs=all_glyphs,
        total_bitmap_size=total_bitmap_size,
        advanceY=advanceY,
        ascender=ascender,
        descender=descender,
        kern_left_classes=kern_left_classes,
        kern_right_classes=kern_right_classes,
        kern_matrix=kern_matrix,
        kern_left_class_count=kern_left_class_count,
        kern_right_class_count=kern_right_class_count,
        ligature_pairs=ligature_pairs,
    )


# --- Binary packing helpers ---

# EpdGlyph struct: 16 bytes, little-endian
GLYPH_STRUCT_FORMAT = "<BBHhhH2xI"
assert struct.calcsize(GLYPH_STRUCT_FORMAT) == 16


def pack_style_sections(sd):
    """Pack one StyleRasterData into binary section bytearrays.
    Returns (intervals_data, glyphs_data, kern_left, kern_right, kern_matrix, ligatures, bitmaps)."""
    intervals_data = bytearray()
    offset = 0
    for i_start, i_end in sd.intervals:
        intervals_data += struct.pack("<III", i_start, i_end, offset)
        offset += i_end - i_start + 1

    glyphs_data = bytearray()
    for glyph, packed in sd.all_glyphs:
        glyphs_data += struct.pack(GLYPH_STRUCT_FORMAT,
                                   glyph.width, glyph.height, glyph.advance_x,
                                   glyph.left, glyph.top,
                                   glyph.data_length, glyph.data_offset)

    kern_left_data = bytearray()
    for cp, cls in sd.kern_left_classes:
        kern_left_data += struct.pack("<HB", cp, cls)

    kern_right_data = bytearray()
    for cp, cls in sd.kern_right_classes:
        kern_right_data += struct.pack("<HB", cp, cls)

    kern_matrix_data = bytearray()
    if sd.kern_matrix:
        kern_matrix_data = bytearray(struct.pack(f"<{len(sd.kern_matrix)}b", *sd.kern_matrix))

    ligature_data = bytearray()
    for packed_pair, lig_cp in sd.ligature_pairs:
        ligature_data += struct.pack("<II", packed_pair, lig_cp)

    bitmap_data = bytearray()
    for glyph, packed in sd.all_glyphs:
        bitmap_data += packed
    assert len(bitmap_data) == sd.total_bitmap_size

    return (intervals_data, glyphs_data, kern_left_data, kern_right_data,
            kern_matrix_data, ligature_data, bitmap_data)


def style_sections_total_size(sections):
    """Total byte size of all sections returned by pack_style_sections()."""
    return sum(len(s) for s in sections)


# --- File writers ---

def generate_cpfont_multistyle(style_fonts, size, intervals, output_path,
                               force_autohint=False, fallback_style_fonts=None,
                               synth_specs=None, spacing_specs=None):
    """Generate a multi-style v4 .cpfont file.

    style_fonts: dict of {style_id: fontfile_path} e.g. {0: "Regular.ttf", 2: "Italic.ttf"}
    fallback_style_fonts: optional dict of {style_id: fallback_fontfile_path}
    synth_specs: optional dict of {style_id: synthetic_spec_dict} for styles
        synthesized from another style's font file (embolden/shear at build
        time, see docs/synthetic-font-styles.md)
    """
    MAGIC = b"CPFONT\x00\x00"
    HEADER_SIZE = 32
    STYLE_TOC_ENTRY_SIZE = 32
    flags = 1  # always 2-bit grayscale
    style_count = len(style_fonts)

    # Rasterize each style
    raster_data = {}  # style_id -> StyleRasterData
    fallback_style_fonts = fallback_style_fonts or {}
    synth_specs = synth_specs or {}
    spacing_specs = spacing_specs or {}
    for style_id in sorted(style_fonts.keys()):
        fontfile = style_fonts[style_id]
        fallback_fontfile = fallback_style_fonts.get(style_id)
        print(f"  Rasterizing style {style_id}...", file=sys.stderr)
        raster_data[style_id] = rasterize_font_style(
            fontfile, size, intervals, style_id=style_id,
            force_autohint=force_autohint,
            fallback_fontfile=fallback_fontfile,
            synthetic=synth_specs.get(style_id),
            spacing=spacing_specs.get(style_id))

    # Pack binary sections for each style
    packed_sections = {}  # style_id -> tuple of section bytearrays
    for style_id, sd in raster_data.items():
        packed_sections[style_id] = pack_style_sections(sd)

    # Calculate data offsets (after header + TOC)
    data_start = HEADER_SIZE + style_count * STYLE_TOC_ENTRY_SIZE
    current_offset = data_start

    style_offsets = {}  # style_id -> absolute file offset
    for style_id in sorted(packed_sections.keys()):
        style_offsets[style_id] = current_offset
        current_offset += style_sections_total_size(packed_sections[style_id])

    # Build global header
    # V4 header: magic(8) + version(2) + flags(2) + styleCount(1) + reserved(19) = 32
    header = struct.pack("<8sHHB19s", MAGIC, CPFONT_VERSION, flags, style_count, bytes(19))
    assert len(header) == HEADER_SIZE

    # Build style TOC entries
    # Each entry: styleId(1) + pad(3) + intervalCount(4) + glyphCount(4) +
    #   advanceY(1) + ascender(2) + descender(2) + kernL(2) + kernR(2) +
    #   kernLCls(1) + kernRCls(1) + ligCount(1) + dataOffset(4) + reserved(4) = 32
    STYLE_TOC_FORMAT = "<B3xIIBhhHHBBBI4x"
    assert struct.calcsize(STYLE_TOC_FORMAT) == STYLE_TOC_ENTRY_SIZE

    toc_data = bytearray()
    for style_id in sorted(raster_data.keys()):
        sd = raster_data[style_id]
        if sd.advanceY > 255:
            print(f"ERROR: advanceY ({sd.advanceY}) exceeds uint8 range for "
                  f"style {style_id} size {size}. This likely means the font "
                  f"size is too large for this format.",
                  file=sys.stderr)
            sys.exit(1)
        toc_data += struct.pack(STYLE_TOC_FORMAT,
                                style_id,
                                len(sd.intervals), len(sd.all_glyphs),
                                sd.advanceY, sd.ascender, sd.descender,
                                len(sd.kern_left_classes), len(sd.kern_right_classes),
                                sd.kern_left_class_count, sd.kern_right_class_count,
                                len(sd.ligature_pairs),
                                style_offsets[style_id])

    # Write output
    os.makedirs(os.path.dirname(output_path) if os.path.dirname(output_path) else ".", exist_ok=True)
    total_file_size = 0
    with open(output_path, "wb") as f:
        f.write(header)
        f.write(toc_data)
        for style_id in sorted(packed_sections.keys()):
            for section in packed_sections[style_id]:
                f.write(section)
        total_file_size = f.tell()

    # Print summary
    print(f"  Output: {output_path} (v4, {style_count} styles)", file=sys.stderr)
    print(f"    Header+TOC: {HEADER_SIZE + len(toc_data)} bytes", file=sys.stderr)
    for style_id in sorted(raster_data.keys()):
        sd = raster_data[style_id]
        secs = packed_sections[style_id]
        style_names = {0: "regular", 1: "bold", 2: "italic", 3: "bolditalic"}
        sname = style_names.get(style_id, str(style_id))
        ssize = style_sections_total_size(secs)
        print(f"    {sname}: {len(sd.all_glyphs)} glyphs, {len(sd.intervals)} intervals, "
              f"{ssize} bytes", file=sys.stderr)
    print(f"    Total: {total_file_size} bytes ({total_file_size / 1024 / 1024:.2f} MB)", file=sys.stderr)
    return total_file_size


def main():
    parser = argparse.ArgumentParser(
        description="Generate .cpfont files for SD card font loading.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"Available interval presets: {', '.join(sorted(INTERVAL_PRESETS.keys()))}"
    )

    # Font file (positional, optional for multi-style mode)
    parser.add_argument("fontfile", nargs="?", default=None,
                        help="Path to the font file (single-style mode).")
    parser.add_argument("--intervals", dest="intervals",
                        help="Comma-separated interval presets (e.g., 'latin-ext,greek,cyrillic').")
    parser.add_argument("--size", type=int, dest="size",
                        help="Single font size to generate.")
    parser.add_argument("--sizes", dest="sizes",
                        help="Comma-separated sizes (e.g., '12,14,16,18').")
    parser.add_argument("--style", dest="style", default="regular",
                        choices=["regular", "bold", "italic", "bolditalic"],
                        help="Font style for single-style mode (default: regular).")
    parser.add_argument("--name", dest="name",
                        help="Font family name for output filenames (default: derived from font filename).")
    parser.add_argument("--force-autohint", dest="force_autohint", action="store_true",
                        help="Force FreeType auto-hinter instead of native font hinting.")
    parser.add_argument("-o", "--output", dest="output",
                        help="Output file path (for single-size mode).")
    parser.add_argument("--output-dir", dest="output_dir",
                        help="Output directory for multi-size mode.")
    parser.add_argument("--list-presets", action="store_true",
                        help="List available interval presets and exit.")

    # Multi-style mode: per-style font file arguments (generates v4 .cpfont)
    parser.add_argument("--regular", dest="font_regular",
                        help="Font file for regular style (enables multi-style v4 mode).")
    parser.add_argument("--bold", dest="font_bold",
                        help="Font file for bold style.")
    parser.add_argument("--italic", dest="font_italic",
                        help="Font file for italic style.")
    parser.add_argument("--bolditalic", dest="font_bolditalic",
                        help="Font file for bold-italic style.")
    parser.add_argument("--fallback-regular", dest="fallback_regular",
                        help="Fallback font file for regular style.")
    parser.add_argument("--fallback-bold", dest="fallback_bold",
                        help="Fallback font file for bold style.")
    parser.add_argument("--fallback-italic", dest="fallback_italic",
                        help="Fallback font file for italic style.")
    parser.add_argument("--fallback-bolditalic", dest="fallback_bolditalic",
                        help="Fallback font file for bold-italic style.")

    # Synthetic style specs: comma-separated key=value pairs, e.g.
    # "embolden_em=0.039,slant_deg=11". Keys: embolden_em, y_ratio, slant_deg.
    SYNTH_HELP = ("Synthesize this style from its font file at build time: "
                  "comma-separated key=value pairs "
                  "(embolden_em, y_ratio, slant_deg).")
    parser.add_argument("--synth-regular", dest="synth_regular", help=SYNTH_HELP)
    parser.add_argument("--synth-bold", dest="synth_bold", help=SYNTH_HELP)
    parser.add_argument("--synth-italic", dest="synth_italic", help=SYNTH_HELP)
    parser.add_argument("--synth-bolditalic", dest="synth_bolditalic", help=SYNTH_HELP)

    # Advance-only spacing: comma-separated key=value pairs, e.g.
    # "tracking_em=0.012,word_space_em=-0.04". Keys: tracking_em, word_space_em.
    SPACE_HELP = ("Advance-only spacing for this style: comma-separated "
                  "key=value pairs (tracking_em, word_space_em), em-relative. "
                  "Outlines are untouched.")
    parser.add_argument("--space-regular", dest="space_regular", help=SPACE_HELP)
    parser.add_argument("--space-bold", dest="space_bold", help=SPACE_HELP)
    parser.add_argument("--space-italic", dest="space_italic", help=SPACE_HELP)
    parser.add_argument("--space-bolditalic", dest="space_bolditalic", help=SPACE_HELP)

    args = parser.parse_args()

    if args.list_presets:
        print("Available interval presets:")
        for name, ranges in sorted(INTERVAL_PRESETS.items()):
            total = sum(e - s + 1 for s, e in ranges)
            print(f"  {name:15s}  {len(ranges)} range(s), ~{total} codepoints")
        sys.exit(0)

    # Detect multi-style mode
    style_fonts = {}
    if args.font_regular:
        style_fonts[0] = args.font_regular
    if args.font_bold:
        style_fonts[1] = args.font_bold
    if args.font_italic:
        style_fonts[2] = args.font_italic
    if args.font_bolditalic:
        style_fonts[3] = args.font_bolditalic

    fallback_style_fonts = {}
    if args.fallback_regular:
        fallback_style_fonts[0] = args.fallback_regular
    if args.fallback_bold:
        fallback_style_fonts[1] = args.fallback_bold
    if args.fallback_italic:
        fallback_style_fonts[2] = args.fallback_italic
    if args.fallback_bolditalic:
        fallback_style_fonts[3] = args.fallback_bolditalic

    def parse_kv_spec(spec_str, style_label, allowed, flag):
        spec = {}
        for part in spec_str.split(","):
            part = part.strip()
            if not part:
                continue
            if "=" not in part:
                print(f"Error: --{flag}-{style_label}: expected key=value, got '{part}'",
                      file=sys.stderr)
                sys.exit(1)
            key, _, value = part.partition("=")
            key = key.strip()
            if key not in allowed:
                print(f"Error: --{flag}-{style_label}: unknown key '{key}' "
                      f"(allowed: {', '.join(sorted(allowed))})", file=sys.stderr)
                sys.exit(1)
            try:
                spec[key] = float(value)
            except ValueError:
                print(f"Error: --{flag}-{style_label}: '{key}' value '{value}' is not a number",
                      file=sys.stderr)
                sys.exit(1)
        return spec

    SYNTH_KEYS = {"embolden_em", "y_ratio", "slant_deg"}
    SPACE_KEYS = {"tracking_em", "word_space_em"}
    synth_specs = {}
    spacing_specs = {}
    for style_id, style_label, synth_str, space_str in (
            (0, "regular", args.synth_regular, args.space_regular),
            (1, "bold", args.synth_bold, args.space_bold),
            (2, "italic", args.synth_italic, args.space_italic),
            (3, "bolditalic", args.synth_bolditalic, args.space_bolditalic)):
        if synth_str:
            synth_specs[style_id] = parse_kv_spec(synth_str, style_label, SYNTH_KEYS, "synth")
        if space_str:
            spacing_specs[style_id] = parse_kv_spec(space_str, style_label, SPACE_KEYS, "space")

    is_multistyle = len(style_fonts) > 0
    fontfile = args.fontfile

    # Require --intervals
    if not args.intervals:
        print("Error: --intervals is required (e.g., --intervals latin-ext,greek,cyrillic)", file=sys.stderr)
        print(f"Available presets: {', '.join(sorted(INTERVAL_PRESETS.keys()))}", file=sys.stderr)
        sys.exit(1)

    intervals = resolve_intervals(args.intervals)

    # Determine sizes
    if args.sizes:
        sizes = [int(s.strip()) for s in args.sizes.split(",")]
    elif args.size:
        sizes = [args.size]
    else:
        print("Error: --size or --sizes is required", file=sys.stderr)
        sys.exit(1)

    # Validate early: single-style mode requires a font file
    if not is_multistyle and not fontfile:
        print("Error: fontfile is required in single-style mode", file=sys.stderr)
        sys.exit(1)

    # Determine font name
    if args.name:
        font_name = args.name
    elif is_multistyle:
        # Derive from the regular font file
        ref_file = style_fonts[min(style_fonts.keys())]
        base = os.path.splitext(os.path.basename(ref_file))[0]
        for suffix in ["-Regular", "-Bold", "-Italic", "-BoldItalic",
                       "-regular", "-bold", "-italic", "-bolditalic"]:
            if base.endswith(suffix):
                base = base[:-len(suffix)]
                break
        font_name = base
    else:
        base = os.path.splitext(os.path.basename(fontfile))[0]
        for suffix in ["-Regular", "-Bold", "-Italic", "-BoldItalic",
                       "-regular", "-bold", "-italic", "-bolditalic"]:
            if base.endswith(suffix):
                base = base[:-len(suffix)]
                break
        font_name = base

    if not is_multistyle:
        # Single font file provided: wrap as a single-style v4 font
        style_map = {"regular": 0, "bold": 1, "italic": 2, "bolditalic": 3}
        style_fonts[style_map[args.style]] = fontfile

    # Always generate v4 format
    if args.output and len(sizes) != 1:
        print("Error: --output can only be used with a single size", file=sys.stderr)
        sys.exit(1)
    output_dir = args.output_dir if args.output_dir else f"{font_name}/"
    total_size = 0
    for sz in sizes:
        if args.output and len(sizes) == 1:
            output_path = args.output
        else:
            filename = f"{font_name}_{sz}.cpfont"
            output_path = os.path.join(output_dir, filename)
        print(f"Generating {output_path} (size {sz}, {len(style_fonts)} style(s), v4)...", file=sys.stderr)
        total_size += generate_cpfont_multistyle(
            style_fonts, sz, intervals, output_path,
            force_autohint=args.force_autohint,
            fallback_style_fonts=fallback_style_fonts,
            synth_specs=synth_specs,
            spacing_specs=spacing_specs)
    print(f"\nTotal: {len(sizes)} files, {total_size / 1024 / 1024:.2f} MB", file=sys.stderr)


if __name__ == "__main__":
    main()
