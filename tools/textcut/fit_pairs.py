#!/usr/bin/env python3
"""Which letter pairs TOUCH at the 9 pt slot, measured the way the converter
rasterizes -- 150 dpi, FT_LOAD_RENDER | FT_LOAD_FORCE_AUTOHINT, advance =
round(linearHoriAdvance), origin = bitmap_left -- and the minimum per-side
bearing that stops them.

    fit_pairs.py FONT                 report pairs with gap <= 0 px
    fit_pairs.py FONT --fix OUT.json  also write/extend kern-<style>.json {"fa": units}
    fit_pairs.py FONT --fix OUT.json --match STOCK.otf
                                      italics: lift only pairs that touch AND that
                                      stock keeps apart, and only up to stock's gap

Gap for the pair (a, b), in px:  adv_a + left_b - (left_a + width_a).
A pair with gap <= 0 has bitmaps that abut or overlap.  The fix adds units to
a's RIGHT and b's LEFT, split evenly, enough to lift the gap to 1 px, in whole
units of 1000/em at 9 pt (1 px = 1000 / 18.75 = 53.3 u).  Advance rounding
means one pass may not be enough; the caller re-cuts and re-measures.
"""
import sys, json, itertools, os
import freetype

PT = 9; DPI = 150; SCALE = 1.014                    # the Heros recipe's per-style `scale` -- the pipeline renders 1.4 % above nominal
PPEM = PT * DPI / 72.0 * SCALE                       # 19.01 px/em at the 9 pt slot
U_PER_PX = 1000.0 / PPEM

CORPUS = ("The lamp on the far wall flickered once and held, and for a while nobody in the "
          "reading room moved. Outside, rain filled the gutters. Marjorie turned page 148 of "
          "the ledger, following a column of figures she had copied out of the Illinois office "
          "three months earlier, then a jump nobody had ever explained. Official policy said the "
          "difference was rounding. She did not believe it, and neither, she suspected, did the "
          "auditor who had signed off on it. Every officer aboard knew the difference between "
          "quiet and silence. thick with thought, whether, rhythm, fifth, tight, lift, oft, "
          "wavy, key, rye, fly, try, ply, avow, rafter, after, effort, off, if, it, fit, "
          "ratty, kitty, pretty, little, better, letter, matter, butter, bottle, battle")
PAIRS = sorted({(a, b) for a, b in zip(CORPUS, CORPUS[1:]) if a.strip() and b.strip()})

import numpy as np
def metrics(path):
    """Per glyph: advance px, bitmap_left, bitmap_top, and the INK rows -- for
    each bitmap row, the leftmost and rightmost pixel with coverage >= 128 (level 2,
    dark gray, or black). Level 1 is a fringe: counting it declared an italic ry
    touching on the strength of one light pixel at the r's arm tip (round 12)."""
    face = freetype.Face(path)
    face.set_char_size(int(round(PPEM * 64)), 0, 72, 72)   # exact scaled ppem
    out = {}
    for ch in sorted({c for p in PAIRS for c in p}):
        face.load_char(ch, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_FORCE_AUTOHINT)
        g = face.glyph; bm = g.bitmap
        a = np.array(bm.buffer, dtype=np.uint8).reshape(bm.rows, bm.pitch)[:, :bm.width] if bm.rows else np.zeros((0, 0), np.uint8)
        rows = {}
        for r in range(a.shape[0]):
            cols = np.nonzero(a[r] >= 128)[0]     # ink = the panel's DARK gray or black; a level-1 fringe pixel is not a collision
            if cols.size: rows[g.bitmap_top - r] = (int(cols[0]), int(cols[-1]))   # keyed by baseline-relative y
        # advance as the .cpfont stores it: 12.4 fixed point of the LINEAR advance
        out[ch] = (int(round(g.linearHoriAdvance / 65536.0 * 16)), g.bitmap_left, rows)
    return out

def load_kern(path):
    """Effective kerning the cutter baked into this font: {"ab": design units}."""
    kp = os.path.splitext(path)[0] + ".kern.json"
    return json.load(open(kp)) if os.path.exists(kp) else {}

def gaps(m, kern=None):
    """Ink gap of the pair in px: over every row where BOTH letters have ink,
    the smallest (leftmost ink of b) - (rightmost ink of a) - 1. Zero = abut,
    negative = overlap. Rows where only one has ink cannot touch."""
    res = {}
    for a, b in PAIRS:
        adv_a, l_a, ra = m[a]; _, l_b, rb = m[b]
        # kern as the .cpfont stores it: 4.4 fixed point, clamped to int8; the
        # renderer advances the pen by (advanceFP + kernFP + 8) >> 4, ONE rounding
        k_fp = max(-128, min(127, int(round((kern or {}).get(a + b, 0) * PPEM / 1000.0 * 16))))
        pen = (adv_a + k_fp + 8) >> 4
        best = None
        for y, (a0, a1) in ra.items():
            if y in rb:
                b0, _ = rb[y]
                gap = (pen + l_b + b0) - (l_a + a1) - 1
                best = gap if best is None else min(best, gap)
        res[(a, b)] = 99 if best is None else best
    return res

def stock_gaps(stock_path):
    """Gaps of the STOCK font at 9 pt, with its own GPOS kerning."""
    import importlib.util
    from fontTools.ttLib import TTFont
    spec = importlib.util.spec_from_file_location("tc", os.path.join(os.path.dirname(os.path.abspath(__file__)), "textcut_curves.py"))
    tc = importlib.util.module_from_spec(spec); spec.loader.exec_module(tc)
    src = TTFont(stock_path); cm = src.getBestCmap()
    k = {chr(l) + chr(r): v for (l, r), v in tc.read_gpos_kern(src, {cp: cm[cp] for cp in cm if 0x20 <= cp < 0x250}).items()}
    return gaps(metrics(stock_path), k)

def main():
    path = sys.argv[1]; fix = sys.argv[sys.argv.index("--fix") + 1] if "--fix" in sys.argv else None
    match = sys.argv[sys.argv.index("--match") + 1] if "--match" in sys.argv else None
    m = metrics(path); g = gaps(m, load_kern(path))
    if match:
        # MATCH STOCK (italics): a pair counts only if it touches AND stock
        # keeps it apart; the lift is to stock's gap, not to an invented 1 px.
        # Stock's own contact pairs stay as the designer left them. The
        # never-touch rule pushed an italic ry out two pixels (round 7); no
        # rule at all let 43 pairs crowd once the advances went stock (round 10).
        sg = stock_gaps(match)
        touching = {p: v for p, v in g.items() if v <= 0 and sg[p] > v}
        target = {p: sg[p] for p in touching}
    else:
        touching = {p: v for p, v in g.items() if v <= 0}
        target = {p: 1 for p in touching}
    print(f"{os.path.basename(path)} @ {PT} pt ({PPEM:.2f} px/em): {len(PAIRS)} pairs measured, "
          f"{len(touching)} touch (gap <= 0): " + " ".join(f"{a}{b}({v})" for (a, b), v in sorted(touching.items())))
    if fix:
        kern = json.load(open(fix)) if os.path.exists(fix) else {}
        # PER PAIR. A side bearing would loosen every neighbour of the glyph
        # (widening a's left side to clear `fa` also loosened `la`, which never
        # touched -- the owner saw it). A kern lifts exactly the pair that
        # touches and nothing else. Lift is what the pair needs to reach a
        # 1 px gap, in whole quarter-pixels; advance rounding decides when the
        # pair actually clears, so the caller re-cuts and re-measures.
        step = int(round(U_PER_PX / 4.0))
        for (a, b), v in touching.items():
            need = (target[(a, b)] - v) * U_PER_PX
            kern[a + b] = kern.get(a + b, 0) + max(step, int(round(need)))
        json.dump(kern, open(fix, "w"), indent=1, sort_keys=True)
        print(f"  {os.path.basename(fix)} now holds {len(kern)} pairs; total {sum(kern.values())} u")
    return 1 if touching else 0

if __name__ == "__main__":
    sys.exit(main())
