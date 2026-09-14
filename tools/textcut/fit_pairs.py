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
import sys, json, itertools, os, math
import freetype

DPI = 150; SCALE = 1.014          # the Heros recipe's per-style `scale`
SLOTS = [7, 9, 11, 12, 14, 16]    # every size the recipe ships -- a kern is in
                                  # DESIGN UNITS and therefore applies at all of
                                  # them; fitting at 9 pt alone opened `ty` by
                                  # 3.56 px at 16 pt (adversarial review, r14).
PT = 9                            # the slot a bare run reports on
def ppem(pt=None): return (pt or PT) * DPI / 72.0 * SCALE
PPEM = ppem()
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
def metrics(path, pt=None):
    """Per glyph: advance px, bitmap_left, bitmap_top, and the INK rows -- for
    each bitmap row, the leftmost and rightmost pixel with coverage >= 128 (level 2,
    dark gray, or black). Level 1 is a fringe: counting it declared an italic ry
    touching on the strength of one light pixel at the r's arm tip (round 12)."""
    face = freetype.Face(path)
    face.set_char_size(int(round(ppem(pt) * 64)), 0, 72, 72)   # exact scaled ppem
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

def gaps(m, kern=None, pt=None):
    """Ink gap of the pair in px: over every row where BOTH letters have ink,
    the smallest (leftmost ink of b) - (rightmost ink of a) - 1. Zero = abut,
    negative = overlap. Rows where only one has ink cannot touch."""
    res = {}
    for a, b in PAIRS:
        adv_a, l_a, ra = m[a]; _, l_b, rb = m[b]
        # kern as the .cpfont stores it: 4.4 fixed point, clamped to int8; the
        # renderer advances the pen by (advanceFP + kernFP + 8) >> 4, ONE rounding
        k_fp = max(-128, min(127, int(round((kern or {}).get(a + b, 0) * ppem(pt) / 1000.0 * 16))))
        pen = (adv_a + k_fp + 8) >> 4
        best = None
        for y, (a0, a1) in ra.items():
            if y in rb:
                b0, _ = rb[y]
                gap = (pen + l_b + b0) - (l_a + a1) - 1
                best = gap if best is None else min(best, gap)
        res[(a, b)] = 99 if best is None else best
    return res

def stock_gaps(stock_path, pt=None):
    """Gaps of the STOCK font at one slot, with its own GPOS kerning."""
    import importlib.util
    from fontTools.ttLib import TTFont
    spec = importlib.util.spec_from_file_location("tc", os.path.join(os.path.dirname(os.path.abspath(__file__)), "textcut_curves.py"))
    tc = importlib.util.module_from_spec(spec); spec.loader.exec_module(tc)
    src = TTFont(stock_path); cm = src.getBestCmap()
    k = {chr(l) + chr(r): v for (l, r), v in tc.read_gpos_kern(src, {cp: cm[cp] for cp in cm if 0x20 <= cp < 0x250}).items()}
    return gaps(metrics(stock_path, pt), k, pt)

def audit(path, stock_path, kern):
    """Per slot: pairs that touch, and pairs opened WIDER than stock by >= 1 px."""
    out = {}
    for pt in SLOTS:
        m = metrics(path, pt); g = gaps(m, kern, pt); sg = stock_gaps(stock_path, pt)
        touch = [p for p in PAIRS if g[p] <= 0 and sg[p] > 0]
        loose = {p: (sg[p], g[p]) for p in PAIRS if g[p] - sg[p] >= 1}
        out[pt] = (touch, loose)
    return out

def main():
    path = sys.argv[1]
    fix = sys.argv[sys.argv.index("--fix") + 1] if "--fix" in sys.argv else None
    stock = sys.argv[sys.argv.index("--stock") + 1] if "--stock" in sys.argv else (
            sys.argv[sys.argv.index("--match") + 1] if "--match" in sys.argv else None)
    kern = load_kern(path)
    if not stock:
        g = gaps(metrics(path), kern); t = {p: v for p, v in g.items() if v <= 0}
        print(f"{os.path.basename(path)} @ {PT} pt ({ppem():.2f} px/em): {len(PAIRS)} pairs, {len(t)} touch: "
              + " ".join(f"{a}{b}({v})" for (a, b), v in sorted(t.items())))
        return 1 if t else 0
    # MINIMAL, MULTI-SLOT. A kern is in design units, so one value serves every
    # slot. Take the smallest value that stops the pair touching at the slot
    # where it touches, and REFUSE any value that would open the pair more than
    # 1 px past stock at the largest slot -- a pair is better left abutting at
    # 7 pt than blown open at 16.
    base = {p: v for p, v in kern.items()}
    a0 = audit(path, stock, base)
    need = {}
    for pt, (touch, _) in a0.items():
        m = metrics(path, pt); g = gaps(m, base, pt)
        for p in touch:
            need[p] = max(need.get(p, 0.0), (1 - g[p]) * 1000.0 / ppem(pt))   # design units
    applied, refused = {}, {}
    for p, u in sorted(need.items()):
        u = int(math.ceil(u))
        excess = u * ppem(SLOTS[-1]) / 1000.0
        if excess > 1.5:
            refused[p] = (u, excess); continue
        applied[p] = u
    print(f"{os.path.basename(path)}: slots {SLOTS}")
    for pt, (touch, loose) in a0.items():
        print(f"   {pt:2d}pt  touching {len(touch)}: {' '.join(a + b for a, b in touch)}")
    if applied: print("   applying (design units): " + " ".join(f"{p}+{u}" for p, u in applied.items()))
    if refused: print("   REFUSED (would exceed 1.5 px at 16 pt): " + " ".join(f"{p}+{u}({e:.2f}px)" for p, (u, e) in refused.items()))
    if fix:
        out = dict(base)
        for p, u in applied.items(): out[p] = out.get(p, 0) + u
        json.dump(out, open(fix, "w"), indent=1, sort_keys=True)
        print(f"   {os.path.basename(fix)}: {len(out)} pairs, total {sum(out.values())} u")
    return 1 if applied else 0

if __name__ == "__main__":
    sys.exit(main())
