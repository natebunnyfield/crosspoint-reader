#!/usr/bin/env python3
"""Pick uniform-slot sizes + vertical metrics for sans-serif candidates.

Mirrors the procedure documented in lib/EpdFont/scripts/sd-fonts.yaml:

  x-height  12 / 14 / 16 / 18 px   (slots 0-3, regular style, hinted)
  advanceY  34 / 40 / 46 / 51 px

The converter renders at 150 DPI, so ppem = pt * 150/72, and a .cpfont's
advanceY = ceil((ascent - descent)/1000 * pt * 150/72) from the patched hhea.

For each family this measures the hinted x-height at every candidate point
size, picks the point size closest to each slot target, then solves for the
single per-mille ascent/descent span that lands advanceY nearest the four
leading targets — floored by the worst-style plain-text ink span + 0.13 em so
lines never overlap, and with the ascender clearing accented capitals where
the span allows.
"""
import io
import json
import os
import sys
import urllib.request
import zipfile

import freetype

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "sources")
os.makedirs(SRC, exist_ok=True)

XH_TARGETS = [12, 14, 16, 18]
ADVY_TARGETS = [34, 40, 46, 51]
INK_PAD_PER_MILLE = 65  # +-0.065 em each side == the +0.13 em rule

# Plain running text: what the leading floor actually has to clear.
PLAIN = ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
         "0123456789.,;:!?'\"()-–—‘’“”")
ACCENTED_CAPS = "ÁÉÍÓÚÄÖÜÀÂÊÑÅ"

GF = "https://raw.githubusercontent.com/google/fonts/main/ofl"

FAMILIES = {
    "AtkinsonNext": {
        "regular": "https://raw.githubusercontent.com/googlefonts/atkinson-hyperlegible-next/main/fonts/ttf/AtkinsonHyperlegibleNext-Regular.ttf",
        "italic": "https://raw.githubusercontent.com/googlefonts/atkinson-hyperlegible-next/main/fonts/ttf/AtkinsonHyperlegibleNext-Italic.ttf",
    },
    "Luciole": {
        "zip": "https://www.luciole-vision.com/fonts/Luciole.zip",
        "regular_member": "Luciole/Luciole-Regular.ttf",
        "italic_member": "Luciole/Luciole-Regular-Italic.ttf",
    },
    "LexicaUltralegible": {
        "regular": "https://raw.githubusercontent.com/jacobxperez/lexica-ultralegible/main/fonts/ttf/LexicaUltralegible-Regular.ttf",
        "italic": "https://raw.githubusercontent.com/jacobxperez/lexica-ultralegible/main/fonts/ttf/LexicaUltralegible-Italic.ttf",
    },
    "Andika": {
        "regular": f"{GF}/andika/Andika-Regular.ttf",
        "italic": f"{GF}/andika/Andika-Italic.ttf",
    },
    "SourceSans3": {
        "regular": "https://raw.githubusercontent.com/adobe-fonts/source-sans/release/TTF/SourceSans3-Regular.ttf",
        "italic": "https://raw.githubusercontent.com/adobe-fonts/source-sans/release/TTF/SourceSans3-It.ttf",
    },
    "FiraSans": {
        "regular": f"{GF}/firasans/FiraSans-Regular.ttf",
        "italic": f"{GF}/firasans/FiraSans-Italic.ttf",
    },
    "IBMPlexSans": {
        "regular": "https://raw.githubusercontent.com/IBM/plex/master/packages/plex-sans/fonts/complete/ttf/IBMPlexSans-Regular.ttf",
        "italic": "https://raw.githubusercontent.com/IBM/plex/master/packages/plex-sans/fonts/complete/ttf/IBMPlexSans-Italic.ttf",
    },
    "AlegreyaSans": {
        "regular": f"{GF}/alegreyasans/AlegreyaSans-Regular.ttf",
        "italic": f"{GF}/alegreyasans/AlegreyaSans-Italic.ttf",
    },
    "OpenSans": {
        "regular": f"{GF}/opensans/OpenSans%5Bwdth%2Cwght%5D.ttf",
        "regular_axes": {"wght": 400, "wdth": 100},
        "italic": f"{GF}/opensans/OpenSans-Italic%5Bwdth%2Cwght%5D.ttf",
        "italic_axes": {"wght": 400, "wdth": 100},
    },
    "PublicSans": {
        "regular": f"{GF}/publicsans/PublicSans%5Bwght%5D.ttf",
        "regular_axes": {"wght": 400},
        "italic": f"{GF}/publicsans/PublicSans-Italic%5Bwght%5D.ttf",
        "italic_axes": {"wght": 400},
    },
    "PTSans": {
        "regular": f"{GF}/ptsans/PT_Sans-Web-Regular.ttf",
        "italic": f"{GF}/ptsans/PT_Sans-Web-Italic.ttf",
    },
    "Inter": {
        "regular": f"{GF}/inter/Inter%5Bopsz%2Cwght%5D.ttf",
        "regular_axes": {"wght": 400, "opsz": 14},
        "italic": f"{GF}/inter/Inter-Italic%5Bopsz%2Cwght%5D.ttf",
        "italic_axes": {"wght": 400, "opsz": 14},
    },
    "NotoSans": {
        "regular": "https://raw.githubusercontent.com/notofonts/notofonts.github.io/main/fonts/NotoSans/unhinted/ttf/NotoSans-Regular.ttf",
        "italic": "https://raw.githubusercontent.com/notofonts/notofonts.github.io/main/fonts/NotoSans/unhinted/ttf/NotoSans-Italic.ttf",
    },
}


def fetch(url, dest):
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        return dest
    req = urllib.request.Request(url, headers={"User-Agent": "crosspoint-font-sweep"})
    with urllib.request.urlopen(req, timeout=60) as r:
        data = r.read()
    with open(dest, "wb") as f:
        f.write(data)
    return dest


def fetch_zip_member(url, member, dest):
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        return dest
    req = urllib.request.Request(url, headers={"User-Agent": "crosspoint-font-sweep"})
    with urllib.request.urlopen(req, timeout=60) as r:
        blob = r.read()
    with zipfile.ZipFile(io.BytesIO(blob)) as z:
        with open(dest, "wb") as f:
            f.write(z.read(member))
    return dest


def instance(path, axes, dest):
    """Pin a variable font's axes — freetype-py cannot set them itself."""
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        return dest
    from fontTools import ttLib
    from fontTools.varLib import instancer
    font = ttLib.TTFont(path)
    instancer.instantiateVariableFont(font, axes, inplace=True, updateFontNames=False)
    font.save(dest)
    return dest


def source_for(name, spec, style):
    if "zip" in spec and style == "regular":
        return fetch_zip_member(spec["zip"], spec["regular_member"], os.path.join(SRC, f"{name}-Regular.ttf"))
    if "zip" in spec and style == "italic":
        return fetch_zip_member(spec["zip"], spec["italic_member"], os.path.join(SRC, f"{name}-Italic.ttf"))
    url = spec.get(style)
    if not url:
        return None
    raw = fetch(url, os.path.join(SRC, f"{name}-{style}-raw.ttf"))
    axes = spec.get(f"{style}_axes")
    if axes:
        return instance(raw, axes, os.path.join(SRC, f"{name}-{style}.ttf"))
    return raw


def hinted_xheight_px(path, pt):
    """Rows of ink in a hinted, rendered 'x' — exactly what the converter stores."""
    face = freetype.Face(path)
    face.set_char_size(pt << 6, pt << 6, 150, 150)
    face.load_char("x", freetype.FT_LOAD_RENDER)
    return face.glyph.bitmap.rows


def ink_span_per_mille(path, chars):
    """Outline yMax/yMin over `chars`, in units per 1000 upem (unscaled)."""
    face = freetype.Face(path)
    upem = face.units_per_EM
    ymax, ymin = -10**9, 10**9
    for ch in chars:
        gi = face.get_char_index(ch)
        if gi == 0:
            continue
        face.load_glyph(gi, freetype.FT_LOAD_NO_SCALE)
        bbox = face.glyph.outline.get_bbox()
        if bbox.yMax == 0 and bbox.yMin == 0:
            continue
        ymax = max(ymax, bbox.yMax)
        ymin = min(ymin, bbox.yMin)
    return ymax * 1000.0 / upem, ymin * 1000.0 / upem


def advy(span_per_mille, pt):
    import math
    return math.ceil(span_per_mille / 1000.0 * pt * 150.0 / 72.0)


def main():
    out = {}
    for name, spec in FAMILIES.items():
        reg = source_for(name, spec, "regular")
        ita = source_for(name, spec, "italic")
        if not reg:
            print(f"!! {name}: no regular source", file=sys.stderr)
            continue

        # 1. size ramp: hinted x-height per point size
        ramp = {}
        for pt in range(7, 30):
            ramp[pt] = hinted_xheight_px(reg, pt)

        sizes = []
        for target in XH_TARGETS:
            best = min(ramp, key=lambda p: (abs(ramp[p] - target), p))
            sizes.append(best)

        # 2. ink floor across regular + italic, plain running text
        ymax_r, ymin_r = ink_span_per_mille(reg, PLAIN)
        ymax_i, ymin_i = (ink_span_per_mille(ita, PLAIN) if ita else (ymax_r, ymin_r))
        ink_top = max(ymax_r, ymax_i)
        ink_bot = min(ymin_r, ymin_i)
        floor_span = (ink_top + INK_PAD_PER_MILLE) - (ink_bot - INK_PAD_PER_MILLE)

        # accented capitals must fit under the declared ascender
        acc_top, _ = ink_span_per_mille(reg, ACCENTED_CAPS)

        # 3. one span per family: least-squares over the four leading targets
        cands = [ADVY_TARGETS[i] * 72.0 * 1000.0 / (150.0 * sizes[i]) for i in range(4)]
        best_span, best_err = None, None
        lo, hi = int(min(cands)) - 40, int(max(cands)) + 40
        for span in range(max(lo, int(floor_span)), hi + 1):
            err = sum((advy(span, sizes[i]) - ADVY_TARGETS[i]) ** 2 for i in range(4))
            if best_err is None or err < best_err:
                best_span, best_err = span, err
        span = max(best_span, int(round(floor_span)))

        # 4. split: ascender clears accented caps where the span allows,
        #    descender takes the rest but never less than the ink descent.
        need_desc = -(ink_bot - INK_PAD_PER_MILLE)
        ascent = min(span - need_desc, max(acc_top + INK_PAD_PER_MILLE, ink_top + INK_PAD_PER_MILLE))
        if ascent < ink_top + INK_PAD_PER_MILLE:
            ascent = ink_top + INK_PAD_PER_MILLE
        descent = -(span - ascent)

        out[name] = {
            "sizes": sizes,
            "xheights": [ramp[p] for p in sizes],
            "metrics": {"ascent": int(round(ascent)), "descent": int(round(descent)), "linegap": 0},
            "advY": [advy(span, p) for p in sizes],
            "span": span,
            "floor_span": round(floor_span, 1),
            "clears_accents": bool(ascent >= acc_top),
            "ramp": ramp,
        }
        print(f"{name:22s} sizes={sizes} xh={out[name]['xheights']} "
              f"advY={out[name]['advY']} span={span} floor={floor_span:.0f} "
              f"acc_ok={out[name]['clears_accents']}")

    with open(os.path.join(HERE, "sans_slots.json"), "w") as f:
        json.dump(out, f, indent=2)
    print("\nwrote sans_slots.json")


if __name__ == "__main__":
    main()
