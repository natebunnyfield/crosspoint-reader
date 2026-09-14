#!/usr/bin/env python3
"""TeX Gyre Heros, text cut -- on the OUTLINES, producing a font.

    CFF cubics  ->  flattened polygons (shapely)
                ->  MITRE offset for weight     (corners stay corners)
                ->  ink traps: a notch polygon SUBTRACTED at every acute
                    ink-side crotch             (a cut, not a fillet)
                ->  per-sort side-bearings      (fit)
                ->  TrueType glyf via fontTools.fontBuilder

Nothing touches a bitmap until FreeType rasterizes the finished TTF for
the figure.  Every glyph is deterministic and identical wherever it
recurs.  Units below are font units at 1000/em.
"""
import math
import numpy as np
from fontTools.ttLib import TTFont
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.recordingPen import RecordingPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.misc.bezierTools import splitCubicAtT
from shapely.geometry import Polygon, MultiPolygon, Point
from shapely.geometry.polygon import orient
from shapely.ops import unary_union
from shapely import affinity

import sys, os
STYLE = sys.argv[1] if len(sys.argv) > 1 else "regular"        # regular | bold | italic | bolditalic
SRC = ("/Users/natebunnyfield/src/crosspoint-reader/lib/EpdFont/scripts/"
       f"downloaded_fonts/QHERO/texgyreheros-{STYLE}.otf")
OUTDIR = "BUILD_DIR"
os.makedirs(OUTDIR, exist_ok=True)
OUT = f"{OUTDIR}/HerosTextCut-{STYLE}.ttf"
BOLD = STYLE.startswith("bold")

# ------------------------------------------------------------- the cut, in units
WEIGHT       = 3.0 if BOLD else 5.0   # per side, every sort: regular stem 88 -> 98 (+11 %), a 2 px stem at the 9 pt slot; bold 140 -> 146
WEIGHT_NARROW= WEIGHT                  # no separate narrow class: the extra was my theory, not the ask, and it closed the f's hook
TRAP_DEPTH   = 20.0   # e-ink: 0.4-0.55 px at 9-13 pt -- a lighter crotch pixel, never a visible notch   # notch depth along the bisector, into the ink
TRAP_MOUTH   = 8.0   # notch mouth, measured along each edge from the crotch
TRAP_FLOOR   = 3.0    # half-width of the notch floor
TRAP_MAX_DEG = 75.0   # a crotch this blunt or blunter gets no trap: right-angle joins (E H L T) keep their corners
BEAR_NARROW  = 0.0    # fit is MEASURED, not classed: see fit_pairs.py
BEAR_ROUND   = 0.0
TRACK        = 0.0    # no tracking: the only fit rule is 'letters do not touch at 9 pt'

NARROW = set("iljtfr1I!|.,:;'")
ROUND  = set("oceOCGQbdpqg069")
# Every codepoint the source cmap has in Basic Latin, Latin-1, Latin Ext-A/B
# and General Punctuation, plus the euro. ASCII only was a proof-of-concept
# glyph set; a font file that WORKS needs the accents it will meet.
def _wanted(cp):
    return 0x20 <= cp < 0x0250 or 0x2000 <= cp <= 0x206F or cp == 0x20AC
CHARS = None   # filled from the cmap in build()

# ------------------------------------------------------------- outlines -> polygons
def flatten_contours(glyphset, name, per_curve=24):
    pen = RecordingPen(); glyphset[name].draw(pen)
    contours, cur, last = [], [], None
    for op, pts in pen.value:
        if op == "moveTo":
            cur = [pts[0]]; last = pts[0]
        elif op == "lineTo":
            cur.append(pts[0]); last = pts[0]
        elif op == "curveTo":
            c1, c2, p = pts
            for i in range(1, per_curve + 1):
                t = i / per_curve
                # de Casteljau
                a = lerp(last, c1, t); b = lerp(c1, c2, t); c = lerp(c2, p, t)
                d = lerp(a, b, t); e = lerp(b, c, t)
                cur.append(lerp(d, e, t))
            last = p
        elif op == "qCurveTo":
            raise RuntimeError("quadratic in a CFF font?")
        elif op in ("closePath", "endPath"):
            if len(cur) >= 3: contours.append(cur)
            cur = []
    if len(cur) >= 3: contours.append(cur)
    return contours

def lerp(a, b, t): return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)

def signed_area(ring):
    return 0.5 * sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(ring, ring[1:] + ring[:1]))

def contours_to_polygon(contours):
    """Nonzero-winding fill, as PostScript does it: same-sense as the largest
    ring is ink, opposite sense is a hole."""
    if not contours: return None
    rings = [(signed_area(c), Polygon(c).buffer(0)) for c in contours]
    ref = max(rings, key=lambda r: abs(r[0]))[0]
    ink   = [p for a, p in rings if (a > 0) == (ref > 0)]
    holes = [p for a, p in rings if (a > 0) != (ref > 0)]
    poly = unary_union(ink)
    if holes: poly = poly.difference(unary_union(holes))
    return poly

# ------------------------------------------------------------- the cut
def offset_mitre(poly, d, aniso=50.0):
    """HORIZONTAL weight only. An isotropic buffer grows the glyph d units up
    and down as well, which moves baseline, x-height and cap height -- and the
    autohinter then snaps them to different pixels than stock (9 pt cap came
    out 13 px against 14, 11 pt 18 against 16; the f lost a hook row). Buffering
    under a tall y-scale makes the vertical growth d/aniso ~ 0.1 u: stems gain
    2d, alignments stay exactly stock, corners stay corners (mitre)."""
    tall = affinity.scale(poly, xfact=1.0, yfact=aniso, origin=(0, 0))
    grown = tall.buffer(d, join_style="mitre", mitre_limit=8.0)
    return affinity.scale(grown, xfact=1.0, yfact=1.0 / aniso, origin=(0, 0))

def crotches(poly):
    """Vertices where a paper wedge narrower than TRAP_MAX_DEG opens into the ink."""
    found = []
    polys = list(poly.geoms) if isinstance(poly, MultiPolygon) else [poly]
    for pg in polys:
        for ring in [pg.exterior, *pg.interiors]:
            pts = list(ring.coords)[:-1]; n = len(pts)
            for i in range(n):
                A, P, B = pts[i - 1], pts[i], pts[(i + 1) % n]
                u = unit((A[0] - P[0], A[1] - P[1])); w = unit((B[0] - P[0], B[1] - P[1]))
                if u is None or w is None: continue
                cosang = max(-1.0, min(1.0, u[0] * w[0] + u[1] * w[1]))
                theta = math.degrees(math.acos(cosang))
                if theta > TRAP_MAX_DEG: continue
                bis = unit((u[0] + w[0], u[1] + w[1]))
                if bis is None: continue
                probe = Point(P[0] + bis[0] * 1.5, P[1] + bis[1] * 1.5)
                if not poly.contains(probe):          # the small wedge is PAPER -> a crotch
                    found.append((P, u, w, bis, theta))
    return found

def unit(v):
    l = math.hypot(*v)
    return None if l < 1e-9 else (v[0] / l, v[1] / l)

def trap_polygon(P, u, w, bis):
    """Mouth on the two edges, apex pushed into the ink along -bisector, flat floor."""
    m1 = (P[0] + u[0] * TRAP_MOUTH, P[1] + u[1] * TRAP_MOUTH)
    m2 = (P[0] + w[0] * TRAP_MOUTH, P[1] + w[1] * TRAP_MOUTH)
    apex = (P[0] - bis[0] * TRAP_DEPTH, P[1] - bis[1] * TRAP_DEPTH)
    nrm = (-bis[1], bis[0])
    f1 = (apex[0] + nrm[0] * TRAP_FLOOR, apex[1] + nrm[1] * TRAP_FLOOR)
    f2 = (apex[0] - nrm[0] * TRAP_FLOOR, apex[1] - nrm[1] * TRAP_FLOOR)
    # pair each floor corner with the nearer mouth point so the quad does not twist
    if math.dist(f1, m1) + math.dist(f2, m2) > math.dist(f1, m2) + math.dist(f2, m1):
        f1, f2 = f2, f1
    # the mouth apex sits OUT in the paper, so the crotch vertex is strictly inside
    # the cut and cannot survive the boolean as a hairline spike
    out = (P[0] + bis[0] * 6.0, P[1] + bis[1] * 6.0)
    return Polygon([m1, out, m2, f2, f1]).buffer(0)

def cut_glyph(poly, ch):
    d = WEIGHT_NARROW if ch in NARROW else WEIGHT
    poly = offset_mitre(poly, d)
    if not (ch.isalnum() or ch in '&@'):          # punctuation keeps its curls
        return poly, 0
    traps = [trap_polygon(P, u, w, bis) for (P, u, w, bis, th) in crotches(poly)]
    if traps:
        poly = poly.difference(unary_union(traps))
    return poly, len(traps)

# ------------------------------------------------------------- polygons -> glyf
def draw_polygon(pen, poly):
    polys = list(poly.geoms) if isinstance(poly, MultiPolygon) else [poly]
    for pg in polys:
        pg = orient(pg, sign=-1.0)                    # TrueType: outer clockwise
        for ring in [pg.exterior, *pg.interiors]:
            pts = [(round(x), round(y)) for x, y in list(ring.coords)[:-1]]
            pts = dedupe(pts)
            if len(pts) < 3: continue
            pen.moveTo(pts[0])
            for p in pts[1:]: pen.lineTo(p)
            pen.closePath()

def dedupe(pts):
    out = []
    for p in pts:
        if not out or p != out[-1]: out.append(p)
    if len(out) > 1 and out[0] == out[-1]: out.pop()
    return out

import json
_BEAR = {}
# Side bearings are NOT adjusted any more: widening a's left side to clear
# `fa` also loosened `la`, which never touched. Fit is per-pair kerning
# (kern-<style>.json from fit_pairs.py --fix), layered over the source's own
# GPOS pairs, and written back out as GPOS so the converter carries it.
def bearing(ch):
    """Symmetric legacy hook (unused when bearings.json exists)."""
    return 0.0
def bearing_lr(ch):
    l, r = _BEAR.get(ch, [0.0, 0.0]); return float(l), float(r)

def read_gpos_kern(font, charmap):
    """Every PairPos XAdvance in the source GPOS, in DESIGN UNITS, for glyph
    pairs whose glyphs we carry. Format 1 (per pair) and Format 2 (class)
    both, Extension lookups unwrapped. Written ourselves because the
    converter's extractor returns its 4.4 fixed-point pixel values clamped to
    int8 (raw f/a is -10 u; it reported -128), which is the right thing for a
    .cpfont and the wrong thing for a font file."""
    if "GPOS" not in font: return {}
    name_to_cps = {}
    for cp, g in charmap.items(): name_to_cps.setdefault(g, []).append(cp)
    out = {}
    def add(gl, gr, v):
        if not v: return
        for l in name_to_cps.get(gl, []):
            for r in name_to_cps.get(gr, []):
                out[(l, r)] = out.get((l, r), 0) + int(v)
    for lk in font["GPOS"].table.LookupList.Lookup:
        for st in lk.SubTable:
            st = getattr(st, "ExtSubTable", st)
            if getattr(st, "LookupType", None) != 2: continue
            if st.Format == 1:
                for ps, first in zip(st.PairSet, st.Coverage.glyphs):
                    for pvr in ps.PairValueRecord:
                        add(first, pvr.SecondGlyph, getattr(pvr.Value1, "XAdvance", 0) if pvr.Value1 else 0)
            elif st.Format == 2:
                cd1 = st.ClassDef1.classDefs; cd2 = st.ClassDef2.classDefs
                cov = set(st.Coverage.glyphs)
                for gl in cov:
                    c1 = cd1.get(gl, 0)
                    for gr in name_to_cps:
                        c2 = cd2.get(gr, 0)
                        v = st.Class1Record[c1].Class2Record[c2].Value1
                        add(gl, gr, getattr(v, "XAdvance", 0) if v else 0)
    return out

def build():
    src = TTFont(SRC)
    upm = src["head"].unitsPerEm
    cmap = src.getBestCmap()
    gs = src.getGlyphSet()
    hmtx = src["hmtx"]
    order = [".notdef"]; charmap = {}; glyphs = {}; metrics = {}; report = []
    # .notdef: a box
    pen = TTGlyphPen(None)
    for p in [(50, 0), (50, 700), (450, 700), (450, 0)]: pen.lineTo(p) if p != (50, 0) else pen.moveTo(p)
    pen.closePath(); glyphs[".notdef"] = pen.glyph(); metrics[".notdef"] = (500, 50)
    for cp in sorted(cp for cp in cmap if _wanted(cp)):
        ch = chr(cp)
        name = cmap[cp]
        adv, lsb = hmtx[name]
        contours = flatten_contours(gs, name)
        pen = TTGlyphPen(None)
        ntraps = 0
        d = WEIGHT_NARROW if ch in NARROW else WEIGHT
        bl, br = bearing_lr(ch)
        # ADVANCE STAYS STOCK. Adding the offset's 2d (+10 u) to every advance is
        # +0.19 px at 9 pt, and rounding turned that into a whole extra pixel on
        # 33 of 67 glyphs -- h, a, r, e, o, every digit -- which read as bad
        # kerning ("harf"). The stems grow into the bearings by d a side instead
        # (0.09 px, invisible); the set width at every slot is stock's.
        shift = bl                                   # outline sits where stock's did
        if contours:
            poly = contours_to_polygon(contours)
            poly, ntraps = cut_glyph(poly, ch)
            poly = affinity.translate(poly, xoff=shift)
            draw_polygon(pen, poly)
        g = pen.glyph()
        gname = name if name not in glyphs else name + ".cut"
        if not all(c.isalnum() or c in "._" for c in gname) or gname[0].isdigit():
            gname = f"uni{cp:04X}"
        order.append(gname); charmap[cp] = gname; glyphs[gname] = g
        new_adv = int(round(adv + bl + br + TRACK))  # == stock while bearings and track are 0
        metrics[gname] = (new_adv, int(round(lsb + shift)) if contours else 0)
        report.append((ch, name, adv, new_adv, ntraps))
    fb = FontBuilder(upm, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(charmap)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(metrics)
    hhea = src["hhea"]; os2 = src["OS/2"]
    fb.setupHorizontalHeader(ascent=hhea.ascent, descent=hhea.descent)
    fb.setupNameTable({"familyName": "Heros Text Cut", "styleName": STYLE.capitalize(),
                       "fullName": f"Heros Text Cut {STYLE.capitalize()}", "psName": f"HerosTextCut-{STYLE.capitalize()}",
                       "uniqueFontIdentifier": f"HerosTextCut-{STYLE} eink 2026-09-14",
                       "version": "Version 0.001"})
    fb.setupOS2(sTypoAscender=os2.sTypoAscender, sTypoDescender=os2.sTypoDescender,
                sTypoLineGap=os2.sTypoLineGap, usWinAscent=os2.usWinAscent,
                usWinDescent=os2.usWinDescent, sxHeight=os2.sxHeight, sCapHeight=os2.sCapHeight)
    fb.setupPost()
    # --- kerning: the source's own pairs (design units, via the converter's
    # extractor at ppem == upm so pixel == unit) plus the fitted touch fixes.
    src_kern = read_gpos_kern(src, {cp: cmap[cp] for cp in charmap})   # source names; {(cpL, cpR): design units}
    fixes = {}
    _kp = f"{OUTDIR}/kern-{STYLE}.json"
    if os.path.exists(_kp):
        fixes = json.load(open(_kp))                             # {"ab": +units}
    eff = {}
    for (l, r), v in src_kern.items():
        if l in charmap and r in charmap and v: eff[(l, r)] = int(v)
    for pair, v in fixes.items():
        l, r = ord(pair[0]), ord(pair[1])
        if l in charmap and r in charmap: eff[(l, r)] = eff.get((l, r), 0) + int(v)
    if eff:
        from fontTools.feaLib.builder import addOpenTypeFeaturesFromString
        lines = ["languagesystem DFLT dflt;", "feature kern {"]
        for (l, r), v in sorted(eff.items()):
            lines.append(f"  pos {charmap[l]} {charmap[r]} {v};")
        lines.append("} kern;")
        font = fb.font
        addOpenTypeFeaturesFromString(font, "\n".join(lines))
    json.dump({chr(l) + chr(r): v for (l, r), v in eff.items()},
              open(f"{OUTDIR}/HerosTextCut-{STYLE}.kern.json", "w"), indent=1, sort_keys=True)
    fb.save(OUT)
    print(f"kerning: {len(src_kern)} source pairs, {len(fixes)} fitted fixes, {len(eff)} written to GPOS")
    return report

if __name__ == "__main__":
    rep = build()
    total = sum(r[4] for r in rep)
    print(f"wrote {OUT}: {len(rep)} glyphs, {total} traps cut")
    print("traps per letter:", " ".join(f"{ch}:{n}" for ch, _, _, _, n in rep if n and ch.strip()))
    print("advance widths (sample): " + "  ".join(f"{ch} {a}->{b}" for ch, _, a, b, _ in rep if ch in "ilovwEy"))
