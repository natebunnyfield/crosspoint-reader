#!/usr/bin/env python3
"""Optical autokerning: fill a font's kerning gaps without overriding its designer.

Writes synthesised pairs into a legacy `kern` table. fontconvert_sdcard.py reads
both legacy and GPOS and SUMS them (fontconvert_sdcard.py:283 and :212), so the
emitted values compose with whatever the designer already specified.

    python3 optical_kern.py IN.ttf OUT.ttf              # synthesise
    python3 optical_kern.py IN.ttf --validate           # check the metric first

Only run this on a face whose kerning is genuinely sparse. A well-kerned font
needs nothing from it.

A cautionary note on the metric
-------------------------------
The obvious implementation -- equalise the MEAN LINEAR GAP over rows where both
glyphs have ink -- is wrong, and wrong in a way that looks reasonable until you
render it. For n+n it is honest: two rectangles, gap == sidebearings. For T+r
the shared band is mostly the open void UNDER the crossbar, which a reader
perceives as part of the T, not as space between letters. That version measured
394 units where the eye sees ~245, demanded -149, saturated its cap with 672
pairs, and collided k+d in "backdrop". It also overwrote designer pairs, taking
T+r from the designer's -30 to -130.

So: clamp per-row gaps at a reach depth, calibrate against the font's own
rhythm, and never touch a pair the designer already ruled on.

How it works
------------
1. AREA, not linear gap. Each row's gap is clamped at a reach depth, so a void
   deeper than the eye reads as "letter shape" stops counting as letterspace.
2. Calibrated to INTRA-letter white -- the counters of n and o -- which is the
   actual principle: the space between letters should match the space within
   them. Not calibrated to a hand-picked list of reference pairs.
3. Designer values are never overwritten. Synthesis fills gaps only.
4. A tight cap, and cap saturation is reported as a FAILURE rather than
   silently clamped -- pairs piling up at the cap means the metric is wrong.

Acceptance test (--validate): for the pairs the designer DID kern, the
predicted correction must correlate with the designer's own value, and for
well-fitted reference pairs it must predict ~0. If that fails, the metric is
not measuring what a type designer's eye measures and must not be shipped.
"""
import argparse
import os
import statistics
import sys

import freetype
from fontTools.ttLib import TTFont, newTable
from fontTools.ttLib.tables._k_e_r_n import KernTable_format_0

PPEM = 96  # rasterise well above target sizes so the profile is smooth

SET = ("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
       "abcdefghijklmnopqrstuvwxyz"
       "0123456789"
       ".,;:!?'\"()[]{}-/&@*")


class Glyph:
    __slots__ = ("left", "right", "adv", "rows", "name", "top", "bot")

    def __init__(self, left, right, adv, rows, name, top, bot):
        self.left = left    # {y: first ink x}
        self.right = right  # {y: last ink x}
        self.adv = adv      # advance in px
        self.rows = rows    # {y: [(runstart, runend), ...]}
        self.name = name
        self.top = top
        self.bot = bot


def profile(face, font, cmap, hmtx, upem, chars):
    """Rasterise each char and record per-row ink runs in px, y measured up from baseline."""
    u = upem / PPEM
    out = {}
    for ch in chars:
        cp = ord(ch)
        if cp not in cmap:
            continue
        face.load_char(ch, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_NO_HINTING)
        g = face.glyph
        bm = g.bitmap
        if not bm.rows or not bm.width:
            continue
        buf = bytes(bm.buffer)
        left, right, rows = {}, {}, {}
        for yy in range(bm.rows):
            row = buf[yy * bm.pitch: yy * bm.pitch + bm.width]
            runs = []
            start = -1
            for x, v in enumerate(row):
                ink = v > 60
                if ink and start < 0:
                    start = x
                elif not ink and start >= 0:
                    runs.append((g.bitmap_left + start, g.bitmap_left + x - 1))
                    start = -1
            if start >= 0:
                runs.append((g.bitmap_left + start, g.bitmap_left + bm.width - 1))
            if runs:
                y = g.bitmap_top - yy
                rows[y] = runs
                left[y] = runs[0][0]
                right[y] = runs[-1][1]
        if rows:
            ys = rows.keys()
            out[ch] = Glyph(left, right, hmtx[cmap[cp]][0] / u, rows,
                            cmap[cp], max(ys), min(ys))
    return out


def counter_white(g, band, depth):
    """Mean clamped white INSIDE one glyph, over the band -- the intra-letter target.

    For n this is the gap between the two stems; for o the bowl. Rows with a
    single ink run contribute nothing (no interior), so they are skipped rather
    than counted as zero -- counting them would drag the target down toward the
    solid parts of the letter.
    """
    vals = []
    for y in band:
        runs = g.rows.get(y)
        if not runs or len(runs) < 2:
            continue
        gaps = [runs[i + 1][0] - runs[i][1] - 1 for i in range(len(runs) - 1)]
        vals.append(min(max(gaps), depth))
    return statistics.mean(vals) if vals else None


def pair_white(a, b, band, depth, k=0.0):
    """Mean clamped white BETWEEN two glyphs over the band, with kern k applied.

    A row where either glyph has no ink is fully open, so it contributes the
    full reach depth -- that is the fix for v1. Under a T's crossbar the void is
    deeper than the eye tracks, so it saturates at `depth` and stops driving the
    correction, instead of reporting 394 units of "space" that must be removed.
    """
    vals = []
    for y in band:
        ra = a.right.get(y)
        rb = b.left.get(y)
        if ra is None or rb is None:
            vals.append(depth)
            continue
        gap = (a.adv - ra - 1) + rb + k
        vals.append(min(max(gap, 0.0), depth))
    return statistics.mean(vals) if vals else None


def build(src, args):
    face = freetype.Face(src)
    face.set_char_size(PPEM * 64)
    font = TTFont(src)
    upem = font["head"].unitsPerEm
    cmap = font.getBestCmap()
    hmtx = font["hmtx"]
    u = upem / PPEM  # px -> font units

    glyphs = profile(face, font, cmap, hmtx, upem, SET)
    if "x" not in glyphs or "n" not in glyphs or "o" not in glyphs:
        sys.exit("font lacks x/n/o -- cannot calibrate")

    # Band: baseline to x-height. This is where a reader's eye judges rhythm;
    # ascender/descender rows are sparse and would bias every measurement open.
    xh = glyphs["x"].top
    band = list(range(1, xh + 1))

    # Reach depth: how far past a glyph's edge white still reads as letterspace.
    # Derived from the font's own counter, not a magic constant -- a wide-counter
    # face tolerates more white before it reads as a hole.
    n_runs = [max(r[i + 1][0] - r[i][1] - 1 for i in range(len(r) - 1))
              for y in band if (r := glyphs["n"].rows.get(y)) and len(r) > 1]
    counter_px = statistics.median(n_runs) if n_runs else 0.25 * PPEM
    depth = args.reach * counter_px

    tn = counter_white(glyphs["n"], band, depth)
    to = counter_white(glyphs["o"], band, depth)
    counter_target = statistics.mean([v for v in (tn, to) if v is not None])

    # Designer's existing pairs, expanded to characters, so we can both avoid
    # overwriting them and validate the metric against them.
    designer = existing_pairs(font, glyphs)

    # Calibrate the target to the rhythm the DESIGNER actually converged on,
    # rather than asserting it equals the n-counter. The counter is the right
    # intuition but the wrong number: round letters legitimately sit tighter
    # than straight ones, so a raw counter target tells the metric to add space
    # to o+o. Instead, take the optical white the designer's own text achieves
    # -- measured white plus whatever kern they applied -- over the lowercase
    # pairs that set a face's rhythm. Unkerned pairs count too: the designer
    # leaving a pair alone is a statement that it was already right.
    #
    # And do it PER SIDE-SHAPE. A single scalar cannot serve both o+o and H+H:
    # fitting one told the metric to loosen the other (+49 / -37 units in the
    # first cut). Sides are bucketed by how much their edge profile varies over
    # the band -- flat stem, curved bowl, or open diagonal/void -- which is
    # measured from the outlines, not from a hand-maintained character list, so
    # it transfers to any face.
    cat = {ch: (side_category(g, band, "right"), side_category(g, band, "left"))
           for ch, g in glyphs.items()}

    # Calibrate over every rhythm-bearing glyph, not just lowercase: the
    # flat|flat bucket that H+H lands in has almost no lowercase members, so a
    # lowercase-only fit starved it and sent H+H back to the global target.
    # Punctuation stays out -- its spacing follows different conventions and
    # would drag the letter buckets.
    rhythm = ("abcdefghijklmnopqrstuvwxyz"
              "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
              "0123456789")
    buckets = {}
    for ca in rhythm:
        for cb in rhythm:
            if ca not in glyphs or cb not in glyphs:
                continue
            w = pair_white(glyphs[ca], glyphs[cb], band, depth)
            if w is None:
                continue
            key = (cat[ca][0], cat[cb][1])
            buckets.setdefault(key, []).append(w + designer.get((ca, cb), 0) / u)

    global_target = statistics.median(
        [v for vs in buckets.values() for v in vs]) if buckets else counter_target
    # A thin bucket would fit noise; fall back to the global rhythm there.
    targets = {k: (statistics.median(v) if len(v) >= args.min_bucket else global_target)
               for k, v in buckets.items()}

    print(f"  x-height {xh}px  n-counter {counter_px:.1f}px  reach {depth:.1f}px")
    print(f"  global target {global_target * u:.0f} units "
          f"(raw n/o counter would have been {counter_target * u:.0f})")
    for k in sorted(targets):
        print(f"    target[{k[0]:<8}|{k[1]:<8}] = {targets[k] * u:>4.0f} units "
              f"from {len(buckets[k]):>3} pairs")

    # Cap derived from the designer's own practice rather than a constant: never
    # synthesise a kern bigger than the 95th percentile of what they themselves
    # used in this face. v1's 0.13em cap was above anything the designer ever
    # wrote, so it never bound; a flat 0.04em was below their routine Ta/Va
    # corrections and clipped a third of all pairs. The font already knows the
    # right answer.
    dvals = sorted(abs(v) for v in designer.values())
    if args.cap is not None:
        cap = args.cap * upem
        cap_src = f"{args.cap:.3f}em (explicit)"
    elif len(dvals) >= 20:
        cap = dvals[int(0.95 * (len(dvals) - 1))]
        cap_src = f"{cap / upem:.3f}em (designer p95 of {len(dvals)} pairs)"
    else:
        cap = 0.06 * upem
        cap_src = "0.060em (default; too few designer pairs to derive one)"
    dead = args.deadband * upem
    minclear = args.minclear * upem
    print(f"  cap {cap:.0f} units = {cap_src}")

    predicted, clearance = {}, {}
    for ca, ga in glyphs.items():
        for cb, gb in glyphs.items():
            w = pair_white(ga, gb, band, depth)
            if w is None:
                continue
            tgt = targets.get((cat[ca][0], cat[cb][1]), global_target)
            want_px = tgt - w
            # Never tighten past a hard minimum ink clearance at the closest row.
            closest = min_clearance(ga, gb, band)
            if want_px < 0 and closest is not None:
                want_px = max(want_px, -max(0.0, closest * u - minclear) / u)
            predicted[(ca, cb)] = want_px * u  # font units
            clearance[(ca, cb)] = None if closest is None else closest * u

    return dict(font=font, upem=upem, glyphs=glyphs, designer=designer,
                predicted=predicted, clearance=clearance, cap=cap, dead=dead,
                target=global_target, minclear=minclear, u=u)


def side_category(g, band, side):
    """Bucket one side of a glyph by how much its edge profile varies.

    A stem's edge is flat, a bowl's curves, a diagonal or an overhang swings
    wide. That variation is exactly what decides whether the white beside the
    letter reads as letterspace or as part of the letter, so it is the right
    axis to calibrate the target on. Measured, not enumerated.
    """
    prof = g.right if side == "right" else g.left
    vals = [prof[y] for y in band if y in prof]
    if len(vals) < 4:
        return "open"
    spread = max(vals) - min(vals)
    xh = max(band) if band else 1
    ratio = spread / max(xh, 1)
    if ratio < 0.12:
        return "flat"
    if ratio < 0.34:
        return "curved"
    return "open"


def min_clearance(a, b, band):
    vals = []
    for y in band:
        ra, rb = a.right.get(y), b.left.get(y)
        if ra is None or rb is None:
            continue
        vals.append((a.adv - ra - 1) + rb)
    return min(vals) if vals else None


def existing_pairs(font, glyphs):
    """{(charA, charB): design_units} for the font's own kern feature."""
    name_to_ch = {g.name: ch for ch, g in glyphs.items()}
    raw = {}
    if "GPOS" in font:
        gp = font["GPOS"].table
        idx = set()
        for fr in gp.FeatureList.FeatureRecord:
            if fr.FeatureTag == "kern":
                idx.update(fr.Feature.LookupListIndex)
        for i in idx:
            lk = gp.LookupList.Lookup[i]
            if lk.LookupType != 2:
                continue
            for st in lk.SubTable:
                if st.Format == 1:
                    for j, g1 in enumerate(st.Coverage.glyphs):
                        for pvr in st.PairSet[j].PairValueRecord:
                            v = getattr(pvr.Value1, "XAdvance", 0) or 0
                            if v:
                                raw[(g1, pvr.SecondGlyph)] = v
                elif st.Format == 2:
                    c1 = st.ClassDef1.classDefs if st.ClassDef1 else {}
                    c2 = st.ClassDef2.classDefs if st.ClassDef2 else {}
                    for g1 in st.Coverage.glyphs:
                        rec = st.Class1Record[c1.get(g1, 0)]
                        for g2 in name_to_ch:
                            v = getattr(rec.Class2Record[c2.get(g2, 0)].Value1,
                                        "XAdvance", 0) or 0
                            if v:
                                raw[(g1, g2)] = v
    out = {}
    for (ga, gb), v in raw.items():
        if ga in name_to_ch and gb in name_to_ch:
            out[(name_to_ch[ga], name_to_ch[gb])] = v
    return out


def validate(ctx):
    """The metric must agree with the designer where the designer spoke."""
    designer, predicted = ctx["designer"], ctx["predicted"]
    common = [(k, designer[k], predicted[k]) for k in designer if k in predicted]
    if len(common) < 30:
        print(f"  VALIDATE: only {len(common)} overlapping pairs -- inconclusive")
        return
    ds = [d for _, d, _ in common]
    ps = [p for _, _, p in common]
    r = pearson(ds, ps)
    signagree = sum(1 for _, d, p in common if (d < 0) == (p < 0)) / len(common)
    med_abs_err = statistics.median([abs(p - d) for _, d, p in common])

    refs = [("n", "n"), ("o", "o"), ("H", "H"), ("n", "o"), ("o", "n"), ("m", "n")]
    print(f"\n  VALIDATE against {len(common)} designer pairs:")
    print(f"    pearson r          = {r:+.3f}   (want > +0.5)")
    print(f"    sign agreement     = {signagree * 100:.0f}%      (want > 70%)")
    print(f"    median |error|     = {med_abs_err:.0f} units")
    print(f"    designer range     = {min(ds):+.0f} .. {max(ds):+.0f}")
    print(f"    predicted range    = {min(ps):+.0f} .. {max(ps):+.0f}")
    print("    well-fitted reference pairs (designer left these alone, want ~0):")
    for a, b in refs:
        p = predicted.get((a, b))
        if p is not None:
            flag = "OK" if abs(p) < 25 else "** OFF **"
            print(f"      {a}{b}: predicted {p:+6.0f} units   {flag}")


def pearson(xs, ys):
    n = len(xs)
    mx, my = statistics.mean(xs), statistics.mean(ys)
    num = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    dx = sum((x - mx) ** 2 for x in xs) ** 0.5
    dy = sum((y - my) ** 2 for y in ys) ** 0.5
    return num / (dx * dy) if dx and dy else 0.0


def emit(ctx, dst, args):
    font, glyphs = ctx["font"], ctx["glyphs"]
    designer, predicted = ctx["designer"], ctx["predicted"]
    cap, dead = ctx["cap"], ctx["dead"]

    clearance, minclear = ctx["clearance"], ctx["minclear"]
    pairs, capped, skipped_designer, suppressed_pos = {}, 0, 0, 0
    for (ca, cb), want in predicted.items():
        if (ca, cb) in designer:
            skipped_designer += 1      # the designer already ruled here
            continue
        if abs(want) < dead:
            continue
        if want > 0:
            # Adding space is almost never what a font needs -- the sidebearings
            # already set it, and a positive kern means overriding the designer's
            # spacing decision by another route. Allow it only to rescue a pair
            # whose ink genuinely comes too close.
            c = clearance.get((ca, cb))
            if c is None or c >= minclear:
                suppressed_pos += 1
                continue
            want = min(want, minclear - c)
        if abs(want) > cap:
            capped += 1
            want = max(-cap, min(cap, want))
        pairs[(glyphs[ca].name, glyphs[cb].name)] = int(round(want))

    total = len(pairs) + capped
    print(f"\n  emit: {len(pairs)} synthesised pairs, "
          f"{skipped_designer} designer pairs left untouched, "
          f"{suppressed_pos} positive kerns suppressed, {capped} hit the cap")
    if total and capped / max(total, 1) > args.max_capped:
        print(f"  FAIL: {capped / total * 100:.0f}% of pairs hit the {args.cap:.3f}em cap "
              f"(limit {args.max_capped * 100:.0f}%). The metric is over-correcting; "
              f"not writing {dst}.", file=sys.stderr)
        return False

    kt = KernTable_format_0()
    kt.apple = False
    kt.format = 0
    kt.coverage = 1
    kt.version = 0
    kt.kernTable = pairs
    tbl = newTable("kern")
    tbl.version = 0
    tbl.kernTables = [kt]
    font["kern"] = tbl
    font.save(dst)
    print(f"  wrote {os.path.basename(dst)}")
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst", nargs="?")
    ap.add_argument("--reach", type=float, default=1.6,
                    help="reach depth as a multiple of the n-counter width")
    ap.add_argument("--cap", type=float, default=None, help="max |kern| in em; default = designer p95")
    ap.add_argument("--deadband", type=float, default=0.015, help="min |kern| in em")
    ap.add_argument("--min-bucket", type=int, default=20,
                    help="min samples before a side-shape bucket gets its own target")
    ap.add_argument("--minclear", type=float, default=0.020,
                    help="min ink clearance at the closest row, in em")
    ap.add_argument("--max-capped", type=float, default=0.05,
                    help="fail the build if more than this fraction saturate the cap")
    ap.add_argument("--validate", action="store_true")
    ap.add_argument("--dump-json", help="write {'L,R': units} of synthesised pairs")
    args = ap.parse_args()

    print(f"{os.path.basename(args.src)}:")
    ctx = build(args.src, args)
    if args.validate:
        validate(ctx)
    if args.dump_json:
        import json
        designer, clearance = ctx["designer"], ctx["clearance"]
        cap, dead, minclear = ctx["cap"], ctx["dead"], ctx["minclear"]
        out = {}
        for (ca, cb), want in ctx["predicted"].items():
            if (ca, cb) in designer or abs(want) < dead:
                continue
            if want > 0:
                c = clearance.get((ca, cb))
                if c is None or c >= minclear:
                    continue
                want = min(want, minclear - c)
            out[f"{ord(ca)},{ord(cb)}"] = int(round(max(-cap, min(cap, want))))
        json.dump(out, open(args.dump_json, "w"))
        print(f"  dumped {len(out)} synthesised pairs -> {args.dump_json}")
    if args.dst:
        if not emit(ctx, args.dst, args):
            sys.exit(1)


if __name__ == "__main__":
    main()
