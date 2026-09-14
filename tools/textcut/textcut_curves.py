#!/usr/bin/env python3
"""Heros Text Cut, on the CURVES -- no flattening, no shapely.

Why this replaces textcut_vector.py: that pipeline flattened every cubic to
24 line segments before shapely could offset it, and FreeType's autohinter
decides a top is ROUND (overshoot suppressed) only when the extremum sits on a
curve. With every point on-curve, every round top classified flat, so
C G O Q S rose a pixel at 11 pt and 0 3 6 8 U at 14 pt, relative to stock --
misalignments stock does not have. Here the cubic contours are edited in
place and converted to quadratics with cu2qu, so the hinter sees the same
curve structure it sees in stock.

Operations, in font units at 1000/em, all deterministic:

  WEIGHT   every on-curve point moves horizontally by d * n_x, where n is the
           outward normal of the outline there; its off-curve handles move
           with it. Vertical stems gain d a side, horizontals nothing, round
           tops and bottoms nothing (n_x = 0 there), diagonals cos-weighted.
           At a corner between two straight edges the new point is the
           intersection of the two shifted edges (a mitre), so corners stay
           corners. Alignments -- baseline, x-height, cap, overshoots -- are
           untouched by construction.
  TRAPS    at a corner between two straight edges whose paper wedge is
           narrower than 75 degrees, the corner point is replaced by a notch:
           mouth 8 u along each edge, 20 u deep along the bisector into the
           ink, 6 u flat floor. Line-line crotches only (v w y k x A M N V W X
           Z, the 4 and 7); the bowl and arch joins that meet a curve are left
           alone -- at 20 u they are sub-pixel at every slot here anyway.
  DIGITS   5 and 7 top out at 694 u where every other digit reaches 709, and
           at 9 pt the hinter puts 709 at 14 px and 694 at 13 -- a one-pixel
           dip in every number, in stock too; at 14 pt stock has nine digits
           a row below the tenth. Every digit is scaled about the baseline to
           a 715 u top, the smallest value (swept 709..729) that lands all ten
           on the cap-height row at all four slots. Owner asked (round 9).
  ADVANCE  stock, exactly. See textcut_vector.py's history: +2d rounded half
           the alphabet up a pixel at 9 pt.
  KERNING  the source's GPOS pairs in design units, plus the fitted per-pair
           fixes from kern-<style>.json (uprights only), written as one kern
           feature through feaLib.
"""
import sys, os, json, math
from fontTools.ttLib import TTFont
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.recordingPen import RecordingPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.cu2quPen import Cu2QuPen

STYLE = sys.argv[1] if len(sys.argv) > 1 else "regular"
SRC = ("/Users/natebunnyfield/src/crosspoint-reader/lib/EpdFont/scripts/"
       f"downloaded_fonts/QHERO/texgyreheros-{STYLE}.otf")
OUTDIR = "BUILD_DIR"
os.makedirs(OUTDIR, exist_ok=True)
OUT = f"{OUTDIR}/HerosTextCut-{STYLE}.ttf"
BOLD = STYLE.startswith("bold")

WEIGHT = 3.0 if BOLD else 5.0
TRAP_DEPTH, TRAP_MOUTH, TRAP_FLOOR, TRAP_MAX_DEG = 20.0, 8.0, 3.0, 75.0
# Every digit to a 715 u top. Swept 709..729 (round 9): 715 is the smallest
# value that puts all ten digits on the cap-height row at 9, 11, 12 AND 14 pt;
# 709 (stock) leaves 5 and 7 a row low at 9 pt and nine of ten low at 14 pt.
DIGIT_TOP = 715.0
DIGIT_LIFT = {c: DIGIT_TOP / 709.0 for c in "01234689"}; DIGIT_LIFT.update({"5": DIGIT_TOP / 694.0, "7": DIGIT_TOP / 694.0})
CU2QU_ERR = 1.0

def wanted(cp): return 0x20 <= cp < 0x0250 or 0x2000 <= cp <= 0x206F or cp == 0x20AC

# ------------------------------------------------------------- contours
def contours_of(glyphset, name):
    """[(pt, kind)] per closed contour; kind 'on' or 'off'. Cubic only."""
    pen = RecordingPen(); glyphset[name].draw(pen)
    cs, cur = [], []
    for op, pts in pen.value:
        if op == "moveTo": cur = [(pts[0], "on")]
        elif op == "lineTo": cur.append((pts[0], "on"))
        elif op == "curveTo": cur += [(pts[0], "off"), (pts[1], "off"), (pts[2], "on")]
        elif op in ("closePath", "endPath"):
            if cur and cur[0][0] == cur[-1][0] and cur[-1][1] == "on" and len(cur) > 1: cur.pop()
            if len(cur) >= 3: cs.append(cur)
            cur = []
    if len(cur) >= 3: cs.append(cur)
    return cs

def signed_area(c):
    on = [p for p, k in c if k == "on"]
    return 0.5 * sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(on, on[1:] + on[:1]))

def unit(v):
    l = math.hypot(*v); return (v[0] / l, v[1] / l) if l > 1e-9 else (0.0, 0.0)

def tangents(c, i):
    """Incoming and outgoing unit tangents at on-curve point i (index into c)."""
    n = len(c); P = c[i][0]
    prev = c[(i - 1) % n]; nxt = c[(i + 1) % n]
    tin = unit((P[0] - prev[0][0], P[1] - prev[0][1]))
    tout = unit((nxt[0][0] - P[0], nxt[0][1] - P[1]))
    return tin, tout

# ------------------------------------------------------------- weight
def weight_contour(c, d, sgn):
    """Move every point horizontally by d * n_x. sgn flips the normal so it
    points OUT of the ink for this font's winding."""
    n = len(c); out = [None] * n
    def normal_x(t): return sgn * t[1]                 # right-hand normal of (tx,ty) is (ty,-tx); x part = ty
    def line(P, t, dx): return (P[0] + dx, P[1]), t     # edge shifted horizontally
    for i, (P, kind) in enumerate(c):
        if kind != "on": continue
        tin, tout = tangents(c, i)
        pin, pout = c[(i - 1) % n][1], c[(i + 1) % n][1]
        smooth = abs(tin[0] * tout[0] + tin[1] * tout[1]) > 0.985 or pin == "off" or pout == "off"
        if smooth and pin == "off" and pout == "off":
            # a smooth point on a curve: use the average tangent's normal
            t = unit((tin[0] + tout[0], tin[1] + tout[1])); dx = d * normal_x(t)
            out[i] = (P[0] + dx, P[1])
        elif smooth:
            t = unit((tin[0] + tout[0], tin[1] + tout[1])); dx = d * normal_x(t)
            out[i] = (P[0] + dx, P[1])
        else:
            # a corner between two edges. NO POINT EVER MOVES VERTICALLY -- that
            # is the whole alignment guarantee. So the corner keeps its y and
            # takes its x from the more vertical of its two edges, shifted by
            # d * n_x of that edge; the flatter edge re-slopes by a few units.
            # (A true mitre of two x-shifted edges moves the vertex in y on any
            # sloped corner; the bold t's top lost a pixel at 12 pt from that.)
            dx1 = d * normal_x(tin); dx2 = d * normal_x(tout)
            if abs(tin[1]) >= abs(tout[1]): dx = dx1
            else: dx = dx2
            if abs(tin[1]) < 0.2 and abs(tout[1]) < 0.2: dx = (dx1 + dx2) / 2.0
            out[i] = (P[0] + dx, P[1])
    # off-curve handles follow their nearest on-curve neighbour's displacement
    for i, (P, kind) in enumerate(c):
        if kind == "on": continue
        j = i
        while c[j % n][1] != "on": j -= 1
        k = i
        while c[k % n][1] != "on": k += 1
        # first handle after an on-point follows that point; second follows the next on-point
        anchor = j % n if c[(i - 1) % n][1] == "on" else k % n
        Pa = c[anchor][0]; Da = out[anchor]
        out[i] = (P[0] + (Da[0] - Pa[0]), P[1] + (Da[1] - Pa[1]))
    return [(out[i], c[i][1]) for i in range(n)]

# ------------------------------------------------------------- traps
def trap_contour(c, sgn):
    """Replace acute line-line crotch points with a notch."""
    n = len(c); res = []; cut = 0
    for i, (P, kind) in enumerate(c):
        if kind != "on" or c[(i - 1) % n][1] != "on" or c[(i + 1) % n][1] != "on":
            res.append((P, kind)); continue
        A = c[(i - 1) % n][0]; B = c[(i + 1) % n][0]
        u = unit((A[0] - P[0], A[1] - P[1])); w = unit((B[0] - P[0], B[1] - P[1]))
        cosang = max(-1.0, min(1.0, u[0] * w[0] + u[1] * w[1])); theta = math.degrees(math.acos(cosang))
        bis = unit((u[0] + w[0], u[1] + w[1]))
        # is the wedge between the edges paper? outward normal of the incoming edge points out of ink;
        # the wedge bisector is on the paper side when it agrees with that normal
        tin = unit((P[0] - A[0], P[1] - A[1])); nout = (sgn * tin[1], -sgn * tin[0])
        paper_side = bis[0] * nout[0] + bis[1] * nout[1] > 0
        if theta > TRAP_MAX_DEG or not paper_side or bis == (0.0, 0.0):
            res.append((P, kind)); continue
        m1 = (P[0] + u[0] * TRAP_MOUTH, P[1] + u[1] * TRAP_MOUTH)
        m2 = (P[0] + w[0] * TRAP_MOUTH, P[1] + w[1] * TRAP_MOUTH)
        apex = (P[0] - bis[0] * TRAP_DEPTH, P[1] - bis[1] * TRAP_DEPTH)
        nrm = (-bis[1], bis[0])
        f1 = (apex[0] + nrm[0] * TRAP_FLOOR, apex[1] + nrm[1] * TRAP_FLOOR)
        f2 = (apex[0] - nrm[0] * TRAP_FLOOR, apex[1] - nrm[1] * TRAP_FLOOR)
        if math.dist(f1, m1) + math.dist(f2, m2) > math.dist(f1, m2) + math.dist(f2, m1): f1, f2 = f2, f1
        res += [(m1, "on"), (f1, "on"), (f2, "on"), (m2, "on")]; cut += 1
    return res, cut

# ------------------------------------------------------------- glyph
def draw(pen, contours):
    for c in contours:
        n = len(c)
        # rotate so we start on an on-curve point
        start = next(i for i, (_, k) in enumerate(c) if k == "on")
        c = c[start:] + c[:start]
        pen.moveTo(c[0][0]); i = 1
        while i < n:
            P, k = c[i]
            if k == "on": pen.lineTo(P); i += 1
            else:
                c1 = c[i][0]; c2 = c[i + 1][0]; p = c[(i + 2) % n][0]
                pen.curveTo(c1, c2, p); i += 3
        pen.closePath()

def cut_glyph(glyphset, name, ch):
    cs = contours_of(glyphset, name)
    if not cs: return None, 0
    ref = max(cs, key=lambda c: abs(signed_area(c)))
    sgn = 1.0 if signed_area(ref) > 0 else -1.0     # CCW outer (PostScript, y-up): interior is on the LEFT of travel, so outward is the right-hand normal (ty, -tx)
    out = []; ntraps = 0
    for c in cs:
        c2 = weight_contour(c, WEIGHT, sgn)
        if ch.isalnum():
            c2, k = trap_contour(c2, sgn); ntraps += k
        if ch in DIGIT_LIFT:
            f = DIGIT_LIFT[ch]; c2 = [((x, y * f), kind) for (x, y), kind in c2]
        out.append(c2)
    return out, ntraps

def read_gpos_kern(font, charmap):
    if "GPOS" not in font: return {}
    name_to_cps = {}
    for cp, g in charmap.items(): name_to_cps.setdefault(g, []).append(cp)
    out = {}
    def add(gl, gr, v):
        if not v: return
        for l in name_to_cps.get(gl, []):
            for r in name_to_cps.get(gr, []): out[(l, r)] = out.get((l, r), 0) + int(v)
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
                for gl in set(st.Coverage.glyphs):
                    c1 = cd1.get(gl, 0)
                    for gr in name_to_cps:
                        v = st.Class1Record[c1].Class2Record[cd2.get(gr, 0)].Value1
                        add(gl, gr, getattr(v, "XAdvance", 0) if v else 0)
    return out

def build():
    src = TTFont(SRC); upm = src["head"].unitsPerEm; cmap = src.getBestCmap(); gs = src.getGlyphSet(); hmtx = src["hmtx"]
    order = [".notdef"]; charmap = {}; glyphs = {}; metrics = {}; traps_total = 0
    pen = TTGlyphPen(None); pen.moveTo((50, 0)); pen.lineTo((50, 700)); pen.lineTo((450, 700)); pen.lineTo((450, 0)); pen.closePath()
    glyphs[".notdef"] = pen.glyph(); metrics[".notdef"] = (500, 50)
    for cp in sorted(cp for cp in cmap if wanted(cp)):
        ch = chr(cp); name = cmap[cp]; adv, lsb = hmtx[name]
        contours, nt = cut_glyph(gs, name, ch); traps_total += nt
        tt = TTGlyphPen(None)
        if contours:
            draw(Cu2QuPen(tt, max_err=CU2QU_ERR, reverse_direction=True), contours)
        gname = name if name not in glyphs else name + ".cut"
        if not all(c.isalnum() or c in "._" for c in gname) or gname[0].isdigit(): gname = f"uni{cp:04X}"
        order.append(gname); charmap[cp] = gname; glyphs[gname] = tt.glyph()
        metrics[gname] = (adv, lsb - int(round(WEIGHT)) if contours else 0)
    fb = FontBuilder(upm, isTTF=True); fb.setupGlyphOrder(order); fb.setupCharacterMap(charmap); fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(metrics)
    hhea = src["hhea"]; os2 = src["OS/2"]
    fb.setupHorizontalHeader(ascent=hhea.ascent, descent=hhea.descent)
    fb.setupNameTable({"familyName": "Heros Text Cut", "styleName": STYLE.capitalize(), "fullName": f"Heros Text Cut {STYLE.capitalize()}",
                       "psName": f"HerosTextCut-{STYLE.capitalize()}", "uniqueFontIdentifier": f"HerosTextCut-{STYLE} curves 2026-09-14", "version": "Version 0.002"})
    fb.setupOS2(sTypoAscender=os2.sTypoAscender, sTypoDescender=os2.sTypoDescender, sTypoLineGap=os2.sTypoLineGap,
                usWinAscent=os2.usWinAscent, usWinDescent=os2.usWinDescent, sxHeight=os2.sxHeight, sCapHeight=os2.sCapHeight)
    fb.setupPost()
    src_kern = read_gpos_kern(src, {cp: cmap[cp] for cp in charmap})
    fixes = {}; kp = f"{OUTDIR}/kern-{STYLE}.json"
    if os.path.exists(kp): fixes = json.load(open(kp))
    eff = {}
    for (l, r), v in src_kern.items():
        if l in charmap and r in charmap and v: eff[(l, r)] = int(v)
    for pair, v in fixes.items():
        l, r = ord(pair[0]), ord(pair[1])
        if l in charmap and r in charmap: eff[(l, r)] = eff.get((l, r), 0) + int(v)
    if eff:
        from fontTools.feaLib.builder import addOpenTypeFeaturesFromString
        lines = ["languagesystem DFLT dflt;", "feature kern {"] + [f"  pos {charmap[l]} {charmap[r]} {v};" for (l, r), v in sorted(eff.items())] + ["} kern;"]
        addOpenTypeFeaturesFromString(fb.font, "\n".join(lines))
    json.dump({chr(l) + chr(r): v for (l, r), v in eff.items()}, open(f"{OUTDIR}/HerosTextCut-{STYLE}.kern.json", "w"), indent=1, sort_keys=True)
    fb.save(OUT)
    print(f"wrote {os.path.basename(OUT)}: {len(order) - 1} glyphs, {traps_total} traps, {len(src_kern)} source kern pairs + {len(fixes)} fitted")

if __name__ == "__main__":
    build()
