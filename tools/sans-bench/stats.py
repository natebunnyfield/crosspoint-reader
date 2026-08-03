#!/usr/bin/env python3
"""Source-level metrics for the candidates + an italic-shape contact sheet."""
import glob
import json
import os

import freetype
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "sources")

ORDER = ["AtkinsonNext", "Luciole", "LexicaUltralegible", "Andika", "SourceSans3",
         "FiraSans", "IBMPlexSans", "AlegreyaSans", "OpenSans", "PublicSans",
         "PTSans", "Inter", "NotoSans"]


def path_for(name, style):
    for cand in (f"{name}-{style}.ttf", f"{name}-{style.capitalize()}.ttf",
                 f"{name}-{style}-raw.ttf"):
        p = os.path.join(SRC, cand)
        if os.path.exists(p):
            return p
    return None


def metrics(path):
    f = freetype.Face(path)
    upem = f.units_per_EM

    def h(ch):
        gi = f.get_char_index(ch)
        f.load_glyph(gi, freetype.FT_LOAD_NO_SCALE)
        bb = f.glyph.outline.get_bbox()
        return bb.yMax, bb.yMin

    xh = h("x")[0] / upem
    cap = h("H")[0] / upem
    asc = h("b")[0] / upem
    desc = h("p")[1] / upem
    # counter openness: the 'e' eye height as a share of x-height is a decent
    # proxy for how much of the counter survives a hard 1-bit threshold.
    f.load_glyph(f.get_char_index("n"), freetype.FT_LOAD_NO_SCALE)
    nbb = f.glyph.outline.get_bbox()
    n_width = (nbb.xMax - nbb.xMin) / upem
    return {
        "upem": upem,
        "x_height_em": round(xh, 3),
        "cap_em": round(cap, 3),
        "xh_over_cap": round(xh / cap, 3),
        "asc_em": round(asc, 3),
        "desc_em": round(desc, 3),
        "n_width_em": round(n_width, 3),
        "glyphs": f.num_glyphs,
    }


def contact_sheet(out_png):
    """One row per family: the italic 'a e f g y' plus a word, at 56px."""
    rows = []
    for name in ORDER:
        p = path_for(name, "italic")
        if p is None:
            continue
        rows.append((name, p))

    W, RH = 1180, 82
    img = Image.new("L", (W, RH * len(rows) + 10), 255)
    d = ImageDraw.Draw(img)
    for i, (name, p) in enumerate(rows):
        face = freetype.Face(p)
        face.set_char_size(0, 56 * 64, 150, 150)
        y0 = i * RH + 60
        x = 300
        for ch in "aefgy — Every officer knew":
            face.load_char(ch, freetype.FT_LOAD_RENDER)
            bm = face.glyph.bitmap
            for r in range(bm.rows):
                for c in range(bm.width):
                    v = bm.buffer[r * bm.pitch + c]
                    if v:
                        px = x + face.glyph.bitmap_left + c
                        py = y0 - face.glyph.bitmap_top + r
                        if 0 <= px < W and 0 <= py < img.height:
                            img.putpixel((px, py), 255 - v)
            x += face.glyph.advance.x >> 6
        d.text((12, y0 - 14), name, fill=0)
    img.save(out_png)
    return out_png


def main():
    out = {}
    for name in ORDER:
        reg = path_for(name, "regular")
        if reg is None:
            print("missing regular:", name)
            continue
        out[name] = metrics(reg)
        print(f"{name:20s} x/em {out[name]['x_height_em']:.3f}  x/cap {out[name]['xh_over_cap']:.3f}  "
              f"n-width {out[name]['n_width_em']:.3f}  glyphs {out[name]['glyphs']}")
    with open(os.path.join(HERE, "sans_stats.json"), "w") as f:
        json.dump(out, f, indent=2)
    print("\n" + contact_sheet(os.path.join(HERE, "italics.png")))


if __name__ == "__main__":
    main()
