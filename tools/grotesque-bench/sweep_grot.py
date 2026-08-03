#!/usr/bin/env python3
"""Text-grotesque bench: slot sizes, metrics span, coverage, counter survival.

Same uniform-slot procedure as the sans bench, with the leading target
corrected to what the tier actually SHIPS (34/39/45/51, measured off Edgar and
TeX Gyre Schola) rather than the aspirational 34/40/46/51 in sd-fonts.yaml.

Adds the measurement this genre needs. A grotesque is defined by closed
apertures and tight spacing, and the reader's BW mode paints every pixel
carrying any ink at all solid black (GfxRenderer.cpp:506). So the question is
not "how open is the aperture in font units" but "does the counter still exist
as white pixels once the panel is done with it". That is measured here on the
same FreeType render the converter stores, thresholded the way the renderer
thresholds it, with a flood fill from outside to separate enclosed counter from
open aperture.
"""
import io
import json
import math
import os
import sys
import urllib.request
import zipfile
from collections import deque

import freetype
from fontTools import ttLib
from fontTools.varLib import instancer

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "sources")
os.makedirs(SRC, exist_ok=True)

XH_TARGETS = [12, 14, 16, 18]
ADVY_TARGETS = [34, 39, 45, 51]          # what the tier ships, not what it documents
INK_PAD = 65                             # +-0.065 em, the +0.13 em leading floor
ASC_RATIO = 0.77                         # Edgar 0.765, TeX Gyre Schola 0.794

PLAIN = ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
         "0123456789.,;:!?'\"()-–—‘’“”")
ACCENTED_CAPS = "ÁÉÍÓÚÄÖÜÀÂÊÑÅ"
COUNTER_CHARS = "aeosc"                  # the letters a grotesque closes first

GF = "https://raw.githubusercontent.com/google/fonts/main/ofl"

FAMILIES = {
    "Archivo": {
        "regular": (f"{GF}/archivo/Archivo%5Bwdth%2Cwght%5D.ttf", {"wght": 400, "wdth": 100}),
        "italic": (f"{GF}/archivo/Archivo-Italic%5Bwdth%2Cwght%5D.ttf", {"wght": 400, "wdth": 100}),
    },
    "LibreFranklin": {
        "regular": (f"{GF}/librefranklin/LibreFranklin%5Bwght%5D.ttf", {"wght": 400}),
        "italic": (f"{GF}/librefranklin/LibreFranklin-Italic%5Bwght%5D.ttf", {"wght": 400}),
    },
    "InstrumentSans": {
        "regular": (f"{GF}/instrumentsans/InstrumentSans%5Bwdth%2Cwght%5D.ttf", {"wght": 400, "wdth": 100}),
        "italic": (f"{GF}/instrumentsans/InstrumentSans-Italic%5Bwdth%2Cwght%5D.ttf", {"wght": 400, "wdth": 100}),
    },
    "SchibstedGrotesk": {
        "regular": (f"{GF}/schibstedgrotesk/SchibstedGrotesk%5Bwght%5D.ttf", {"wght": 400}),
        "italic": (f"{GF}/schibstedgrotesk/SchibstedGrotesk-Italic%5Bwght%5D.ttf", {"wght": 400}),
    },
    "HankenGrotesk": {
        "regular": (f"{GF}/hankengrotesk/HankenGrotesk%5Bwght%5D.ttf", {"wght": 400}),
        "italic": (f"{GF}/hankengrotesk/HankenGrotesk-Italic%5Bwght%5D.ttf", {"wght": 400}),
    },
    "WixMadeforText": {
        "regular": (f"{GF}/wixmadefortext/WixMadeforText%5Bwght%5D.ttf", {"wght": 400}),
        "italic": (f"{GF}/wixmadefortext/WixMadeforText-Italic%5Bwght%5D.ttf", {"wght": 400}),
    },
    "FamiljenGrotesk": {
        "regular": (f"{GF}/familjengrotesk/FamiljenGrotesk%5Bwght%5D.ttf", {"wght": 400}),
        "italic": (f"{GF}/familjengrotesk/FamiljenGrotesk-Italic%5Bwght%5D.ttf", {"wght": 400}),
    },
    "HostGrotesk": {
        "regular": (f"{GF}/hostgrotesk/HostGrotesk%5Bwght%5D.ttf", {"wght": 400}),
        "italic": (f"{GF}/hostgrotesk/HostGrotesk-Italic%5Bwght%5D.ttf", {"wght": 400}),
    },
    "Roboto": {
        "regular": (f"{GF}/roboto/Roboto%5Bwdth%2Cwght%5D.ttf", {"wght": 400, "wdth": 100}),
        "italic": (f"{GF}/roboto/Roboto-Italic%5Bwdth%2Cwght%5D.ttf", {"wght": 400, "wdth": 100}),
    },
}

# Already built and measured by the sans bench; carried as controls so the
# genre is judged against something, not rebuilt.
CONTROLS = ["Inter", "IBMPlexSans", "PublicSans", "LexicaUltralegible", "Edgar"]


def fetch(url, dest):
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        return dest
    req = urllib.request.Request(url, headers={"User-Agent": "crosspoint-grotesque-bench"})
    with urllib.request.urlopen(req, timeout=90) as r:
        data = r.read()
    open(dest, "wb").write(data)
    return dest


def source_for(name, style, spec):
    url, axes = spec
    raw = fetch(url, os.path.join(SRC, f"{name}-{style}-raw.ttf"))
    if not axes:
        return raw
    dest = os.path.join(SRC, f"{name}-{style}.ttf")
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        return dest
    font = ttLib.TTFont(raw)
    if "fvar" not in font:
        return raw
    have = {a.axisTag for a in font["fvar"].axes}
    instancer.instantiateVariableFont(font, {k: v for k, v in axes.items() if k in have},
                                      inplace=True, updateFontNames=False)
    font.save(dest)
    return dest


def hinted_xheight_px(path, pt):
    face = freetype.Face(path)
    face.set_char_size(pt << 6, pt << 6, 150, 150)
    face.load_char("x", freetype.FT_LOAD_RENDER)
    return face.glyph.bitmap.rows


def ink_span(path, chars):
    face = freetype.Face(path)
    upem = face.units_per_EM
    ymax, ymin = -10**9, 10**9
    for ch in chars:
        gi = face.get_char_index(ch)
        if gi == 0:
            continue
        face.load_glyph(gi, freetype.FT_LOAD_NO_SCALE)
        bb = face.glyph.outline.get_bbox()
        if bb.yMax == 0 and bb.yMin == 0:
            continue
        ymax = max(ymax, bb.yMax)
        ymin = min(ymin, bb.yMin)
    return ymax * 1000.0 / upem, ymin * 1000.0 / upem


def source_metrics(path):
    f = freetype.Face(path)
    u = f.units_per_EM

    def top(ch):
        f.load_glyph(f.get_char_index(ch), freetype.FT_LOAD_NO_SCALE)
        return f.glyph.outline.get_bbox().yMax / u

    f.load_glyph(f.get_char_index("n"), freetype.FT_LOAD_NO_SCALE)
    nbb = f.glyph.outline.get_bbox()
    return {
        "x_height_em": round(top("x"), 3),
        "cap_em": round(top("H"), 3),
        "n_width_em": round((nbb.xMax - nbb.xMin) / u, 3),
        "glyphs": f.num_glyphs,
    }


def counter_pixels(path, pt, ch):
    """White pixels enclosed inside `ch` AFTER the panel's threshold.

    The converter renders with FT_LOAD_RENDER (8-bit AA) and stores 2 bits per
    pixel; GfxRenderer's BW mode then paints every non-white level solid black.
    So any coverage at all is ink, which is what this reproduces. A flood fill
    from the border separates the enclosed counter from an open aperture: an
    aperture that survives leaks to the outside and is not counted, a counter
    that closes drops to zero.
    """
    face = freetype.Face(path)
    face.set_char_size(pt << 6, pt << 6, 150, 150)
    face.load_char(ch, freetype.FT_LOAD_RENDER)
    bm = face.glyph.bitmap
    w, h = bm.width, bm.rows
    if w == 0 or h == 0:
        return 0, 0
    pad = 1
    W, H = w + 2 * pad, h + 2 * pad
    ink = [[False] * W for _ in range(H)]
    buf = bm.buffer
    pitch = abs(bm.pitch)
    for y in range(h):
        row = y * pitch
        for x in range(w):
            if buf[row + x]:                      # ANY coverage -> black
                ink[y + pad][x + pad] = True
    seen = [[False] * W for _ in range(H)]
    q = deque([(0, 0)])
    seen[0][0] = True
    while q:
        cy, cx = q.popleft()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ny, nx = cy + dy, cx + dx
            if 0 <= ny < H and 0 <= nx < W and not seen[ny][nx] and not ink[ny][nx]:
                seen[ny][nx] = True
                q.append((ny, nx))
    enclosed = sum(1 for y in range(H) for x in range(W) if not ink[y][x] and not seen[y][x])
    inked = sum(1 for y in range(H) for x in range(W) if ink[y][x])
    return enclosed, inked


def coverage(path):
    face = freetype.Face(path)
    out = {}
    for key, (a, b) in {"latin1": (0x00A0, 0x00FF), "latinA": (0x0100, 0x017F),
                        "punct": (0x2010, 0x203A)}.items():
        cps = list(range(a, b + 1))
        out[key] = round(100.0 * sum(1 for c in cps if face.get_char_index(c) != 0) / len(cps), 1)
    return out


def advy_probe(path, asc, desc, sizes):
    """asc/desc are per-1000-upem, exactly as the YAML carries them.

    build-sd-fonts.py's apply_metrics_override scales by head.unitsPerEm/1000
    before writing hhea, so a probe that writes the raw number is wrong by
    2.048x on every 2048-upem font — which silently produced 0.68 em line
    heights for Roboto and Schibsted Grotesk on the first run of this sweep.
    """
    f = ttLib.TTFont(path, recalcBBoxes=False, recalcTimestamp=False)
    scale = f["head"].unitsPerEm / 1000.0
    a, d = round(asc * scale), round(desc * scale)
    f["hhea"].ascent, f["hhea"].descent, f["hhea"].lineGap = a, d, 0
    o = f["OS/2"]
    o.sTypoAscender, o.sTypoDescender, o.sTypoLineGap = a, d, 0
    o.usWinAscent, o.usWinDescent = a, -d
    tmp = os.path.join(SRC, "_probe.ttf")
    f.save(tmp)
    out = []
    for pt in sizes:
        fa = freetype.Face(tmp)
        fa.set_char_size(pt << 6, pt << 6, 150, 150)
        fa.load_char("|", freetype.FT_LOAD_RENDER)
        out.append(int(math.ceil(fa.size.height / 64)))
    return out


def main():
    result = {}
    for name, styles in FAMILIES.items():
        reg = source_for(name, "regular", styles["regular"])
        ita = source_for(name, "italic", styles["italic"]) if "italic" in styles else None

        ramp = {pt: hinted_xheight_px(reg, pt) for pt in range(7, 30)}
        sizes = [min(ramp, key=lambda p: (abs(ramp[p] - t), p)) for t in XH_TARGETS]
        # ramp kept for the report: where no point size lands exactly on a
        # target the hinter skipped that pixel value entirely.

        top_r, bot_r = ink_span(reg, PLAIN)
        top_i, bot_i = ink_span(ita, PLAIN) if ita else (top_r, bot_r)
        ink_top, ink_bot = max(top_r, top_i), min(bot_r, bot_i)
        floor = (ink_top + INK_PAD) - (ink_bot - INK_PAD)
        acc_top, _ = ink_span(reg, ACCENTED_CAPS)

        best = None
        for span in range(max(900, int(floor)), 2200):
            asc = round(span * ASC_RATIO)
            got = advy_probe(reg, asc, -(span - asc), sizes)
            err = sum((got[i] - ADVY_TARGETS[i]) ** 2 for i in range(4))
            if best is None or err < best[0]:
                best = (err, span, asc, -(span - asc), got)
            if err == 0:
                break
        err, span, asc, desc, advy = best
        if asc < acc_top + INK_PAD:                # ascender must clear Ä
            asc = int(round(acc_top + INK_PAD))
            desc = -(span - asc)
            advy = advy_probe(reg, asc, desc, sizes)

        counters = {}
        for slot, pt in enumerate(sizes):
            counters[slot] = {ch: counter_pixels(reg, pt, ch)[0] for ch in COUNTER_CHARS}

        result[name] = {
            "sizes": sizes,
            "xheights": [ramp[p] for p in sizes],
            "metrics": {"ascent": asc, "descent": desc, "linegap": 0},
            "advY": advy,
            "advY_err": err,
            "floor_span": round(floor, 1),
            "clears_accents": asc >= acc_top,
            "src": source_metrics(reg),
            "cov": coverage(reg),
            "counters": counters,
        }
        c0 = counters[0]
        print(f"{name:18s} sizes={sizes} xh={result[name]['xheights']} advY={advy} "
              f"x/em={result[name]['src']['x_height_em']:.3f} "
              f"counters@12px a={c0['a']} e={c0['e']} o={c0['o']} s={c0['s']} c={c0['c']} "
              f"cov={result[name]['cov']['latinA']:.0f}%")

    json.dump(result, open(os.path.join(HERE, "grot_slots.json"), "w"), indent=2)
    print("\nwrote grot_slots.json")


if __name__ == "__main__":
    main()
