#!/usr/bin/env python3
"""Audit a font's cmap for entries that point at the WRONG outline.

Recut and re-hosted font files sometimes carry a cmap whose entries name the
right character but reference a different glyph's outline. Nothing in the build
pipeline catches this: the glyph NAMES stay consistent with their codepoints, so
a name-vs-AGL check passes, and `fontconvert_sdcard.py` falls back to Noto only
when `get_char_index()` returns 0 — a codepoint mapped to a real-but-wrong glyph
renders that glyph forever. The fix is `drop_codepoints:` in sd-fonts.yaml, and
this script is how the list is justified.

Two passes, because neither alone is sufficient:

  1. Duplicate outlines. Two codepoints sharing one outline is the signature of
     a shifted glyph-id range (¼ drawn as ﬀ). Mechanical, no judgement needed —
     but blind to a codepoint aliased onto a glyph that has no cmap entry of its
     own (± drawn as ℗).
  2. A contact sheet, this font over a reference font, one column per codepoint.
     Catches everything pass 1 misses, at the cost of needing eyes on it.

Legitimate shared outlines exist (U+005E ^ / U+02C6 ˆ is the common one), so
pass 1 reports, it does not rule.

    python3 tools/font-cmap-audit.py lib/EpdFont/local_fonts/FreightSans-*.otf
    python3 tools/font-cmap-audit.py --sheet /tmp/sheet.png <font>
"""

import argparse
import hashlib
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_REFERENCE = REPO / "lib/EpdFont/builtinFonts/source/NotoSans/NotoSans-Regular.ttf"


def duplicate_outlines(path: Path):
    """[(hash, [codepoints...])] for outlines reached by more than one cmap entry."""
    import freetype
    from fontTools.ttLib import TTFont

    cmap = TTFont(str(path), lazy=True).getBestCmap()
    face = freetype.Face(str(path))
    face.set_char_size(64 << 6, 64 << 6, 72, 72)
    by_hash = {}
    for cp in sorted(cmap):
        gi = face.get_char_index(cp)
        if gi == 0 or chr(cp).isspace():
            continue
        face.load_glyph(gi, freetype.FT_LOAD_NO_SCALE)
        o = face.glyph.outline
        if not o.points:
            continue
        h = hashlib.md5(repr((list(o.points), list(o.tags), list(o.contours))).encode()).hexdigest()
        by_hash.setdefault(h, []).append(cp)
    return [(h, cps) for h, cps in by_hash.items() if len(cps) > 1]


def contact_sheet(path: Path, reference: Path, out: Path, cell=46, cols=22, px=30):
    """Subject glyph over reference glyph, one column per cmap codepoint."""
    import freetype
    from fontTools.ttLib import TTFont
    from PIL import Image

    cps = [c for c in sorted(TTFont(str(path), lazy=True).getBestCmap()) if c >= 0x21]

    def render(font_path, cp):
        f = freetype.Face(str(font_path))
        f.set_char_size(px << 6, px << 6, 72, 72)
        if f.get_char_index(cp) == 0:
            return None, 0
        f.load_char(cp, freetype.FT_LOAD_RENDER)
        bm = f.glyph.bitmap
        if not bm.width or not bm.rows:
            return None, 0
        g = Image.frombytes("L", (bm.width, bm.rows), bytes(bm.buffer))
        return Image.eval(g, lambda v: 255 - v), f.glyph.bitmap_top

    rows = (len(cps) + cols - 1) // cols
    sheet = Image.new("L", (cell * cols, rows * (cell * 2 + 6)), 255)
    for i, cp in enumerate(cps):
        ox, oy = (i % cols) * cell, (i // cols) * (cell * 2 + 6)
        for k, src in enumerate((path, reference)):
            g, top = render(src, cp)
            if g:
                sheet.paste(g, (ox + 4, oy + k * cell + (34 - top)))
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)
    return cps, cols, rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("fonts", nargs="+", type=Path)
    ap.add_argument("--sheet", type=Path,
                    help="Write a contact sheet here (first font only)")
    ap.add_argument("--reference", type=Path, default=DEFAULT_REFERENCE,
                    help="Font drawn on the sheet's bottom row (default: Noto Sans)")
    args = ap.parse_args()

    from fontTools.ttLib import TTFont

    for path in args.fonts:
        if not path.exists():
            print(f"ERROR: no such file: {path}", file=sys.stderr)
            return 1
        order = TTFont(str(path), lazy=True).getGlyphOrder()
        import freetype
        face = freetype.Face(str(path))
        dups = duplicate_outlines(path)
        print(f"=== {path.name}: {len(dups)} shared-outline group(s)")
        for _, cps in sorted(dups, key=lambda kv: kv[1][0]):
            print("    " + "  ==  ".join(
                f"U+{c:04X} {chr(c)!r} [{order[face.get_char_index(c)]}]" for c in cps))
        if not dups:
            print("    (none)")

    if args.sheet:
        cps, cols, rows = contact_sheet(args.fonts[0], args.reference, args.sheet)
        print(f"\nContact sheet: {len(cps)} codepoints, {cols}x{rows} -> {args.sheet}")
        print("Subject on top, reference below. Read every column; a mismapped")
        print("entry shows as two different characters stacked.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
