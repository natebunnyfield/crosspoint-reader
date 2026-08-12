#!/usr/bin/env python3
"""Pick uniform-slot sizes + vertical metrics for the blind serif bench.

Same procedure as tools/sans-bench/sweep_sans.py — see that file's docstring
for the full derivation. This is the fourteen owner-picked keepers from the
TUG FontCatalogue bold-italic bake-off, not a from-scratch method.
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
INK_PAD_PER_MILLE = 65

PLAIN = ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
         "0123456789.,;:!?'\"()-–—‘’“”")
ACCENTED_CAPS = "ÁÉÍÓÚÄÖÜÀÂÊÑÅ"

GF = "https://raw.githubusercontent.com/google/fonts/main/ofl"
CTAN_IBIBLIO = "https://mirrors.ibiblio.org/CTAN/fonts"
CTAN_MIT = "https://ctan.csail.mit.edu/fonts"

FAMILIES = {
    "Accanthis": {
        "regular": "https://mirrors.ctan.org/fonts/accanthis/opentype/AccanthisADFStdNo3-Regular.otf",
        "italic": "https://mirrors.ctan.org/fonts/accanthis/opentype/AccanthisADFStdNo3-Italic.otf",
        "bold": "https://mirrors.ctan.org/fonts/accanthis/opentype/AccanthisADFStdNo3-Bold.otf",
        "bolditalic": "https://mirrors.ctan.org/fonts/accanthis/opentype/AccanthisADFStdNo3-BoldItalic.otf",
    },
    "Almendra": {
        "regular": f"{GF}/almendra/Almendra-Regular.ttf",
        "italic": f"{GF}/almendra/Almendra-Italic.ttf",
        "bold": f"{GF}/almendra/Almendra-Bold.ttf",
        "bolditalic": f"{GF}/almendra/Almendra-BoldItalic.ttf",
    },
    "BaskervilleF": {
        "regular": f"{CTAN_IBIBLIO}/baskervillef/opentype/BaskervilleF-Regular.otf",
        "italic": f"{CTAN_IBIBLIO}/baskervillef/opentype/BaskervilleF-Italic.otf",
        "bold": f"{CTAN_IBIBLIO}/baskervillef/opentype/BaskervilleF-Bold.otf",
        "bolditalic": f"{CTAN_IBIBLIO}/baskervillef/opentype/BaskervilleF-BoldItalic.otf",
    },
    "BerenisADF": {
        "regular": f"{CTAN_IBIBLIO}/berenisadf/opentype/BerenisADFPro-Regular.otf",
        "italic": f"{CTAN_IBIBLIO}/berenisadf/opentype/BerenisADFPro-Italic.otf",
        "bold": f"{CTAN_IBIBLIO}/berenisadf/opentype/BerenisADFPro-Bold.otf",
        "bolditalic": f"{CTAN_IBIBLIO}/berenisadf/opentype/BerenisADFPro-BoldItalic.otf",
    },
    "BodoniLibre": {
        "regular": "https://raw.githubusercontent.com/impallari/Libre-Bodoni/master/fonts/v2002%20-%20Nhung%20edtis%20v2/LibreBodoniv2002-Regular.otf",
        "italic": "https://raw.githubusercontent.com/impallari/Libre-Bodoni/master/fonts/v2002%20-%20Nhung%20edtis%20v2/LibreBodoniv2002-Italic.otf",
        "bold": "https://raw.githubusercontent.com/impallari/Libre-Bodoni/master/fonts/v2002%20-%20Nhung%20edtis%20v2/LibreBodoniv2002-Bold.otf",
        "bolditalic": "https://raw.githubusercontent.com/impallari/Libre-Bodoni/master/fonts/v2002%20-%20Nhung%20edtis%20v2/LibreBodoniv2002-BoldItalic.otf",
    },
    "GaromandQT": {
        "regular": f"{CTAN_MIT}/qualitype/opentype/QTGaromand.otf",
        "italic": f"{CTAN_MIT}/qualitype/opentype/QTGaromand-Italic.otf",
        "bold": f"{CTAN_MIT}/qualitype/opentype/QTGaromand-Bold.otf",
        "bolditalic": f"{CTAN_MIT}/qualitype/opentype/QTGaromand-BoldItalic.otf",
    },
    "IbarraRealNova": {
        "regular": "https://raw.githubusercontent.com/googlefonts/ibarrareal/main/fonts/ttf/IbarraRealNova-Regular.ttf",
        "italic": "https://raw.githubusercontent.com/googlefonts/ibarrareal/main/fonts/ttf/IbarraRealNova-Italic.ttf",
        "bold": "https://raw.githubusercontent.com/googlefonts/ibarrareal/main/fonts/ttf/IbarraRealNova-Bold.ttf",
        "bolditalic": "https://raw.githubusercontent.com/googlefonts/ibarrareal/main/fonts/ttf/IbarraRealNova-BoldItalic.ttf",
    },
    "LibrisADF": {
        "regular": "https://salsa.debian.org/fonts-team/fonts-adf/-/raw/master/Libris-Std-20110117/OTF/LibrisADFStd-Regular.otf",
        "italic": "https://salsa.debian.org/fonts-team/fonts-adf/-/raw/master/Libris-Std-20110117/OTF/LibrisADFStd-Italic.otf",
        "bold": "https://salsa.debian.org/fonts-team/fonts-adf/-/raw/master/Libris-Std-20110117/OTF/LibrisADFStd-Bold.otf",
        "bolditalic": "https://salsa.debian.org/fonts-team/fonts-adf/-/raw/master/Libris-Std-20110117/OTF/LibrisADFStd-BoldItalic.otf",
    },
    "Merriweather": {
        "regular": "https://raw.githubusercontent.com/SorkinType/Merriweather/master/fonts/ttf/Merriweather-Regular.ttf",
        "italic": "https://raw.githubusercontent.com/SorkinType/Merriweather/master/fonts/ttf/Merriweather-Italic.ttf",
        "bold": "https://raw.githubusercontent.com/SorkinType/Merriweather/master/fonts/ttf/Merriweather-Bold.ttf",
        "bolditalic": "https://raw.githubusercontent.com/SorkinType/Merriweather/master/fonts/ttf/Merriweather-BoldItalic.ttf",
    },
    # Light is measured on its OWN weight file (that's the point of testing it
    # separately) but has no light-weight bold cut, so bold/bolditalic borrow
    # the base family's — the reading specimen barely exercises bolditalic
    # anyway (renderReadingSpecimen draws regular/bold/italic, not bolditalic).
    "MerriweatherLight": {
        "regular": "https://raw.githubusercontent.com/SorkinType/Merriweather/master/fonts/ttf/Merriweather-Light.ttf",
        "italic": "https://raw.githubusercontent.com/SorkinType/Merriweather/master/fonts/ttf/Merriweather-LightItalic.ttf",
        "bold": "https://raw.githubusercontent.com/SorkinType/Merriweather/master/fonts/ttf/Merriweather-Bold.ttf",
        "bolditalic": "https://raw.githubusercontent.com/SorkinType/Merriweather/master/fonts/ttf/Merriweather-BoldItalic.ttf",
    },
    "PunkNova": {
        "regular": "https://raw.githubusercontent.com/aliftype/punk-otf/main/punknova-regular.otf",
        "italic": "https://raw.githubusercontent.com/aliftype/punk-otf/main/punknova-slanted.otf",
        "bold": "https://raw.githubusercontent.com/aliftype/punk-otf/main/punknova-bold.otf",
        "bolditalic": "https://raw.githubusercontent.com/aliftype/punk-otf/main/punknova-boldslanted.otf",
    },
    "SchoolCenturyQT": {
        "regular": f"{CTAN_MIT}/qualitype/opentype/QTSchoolCentury.otf",
        "italic": f"{CTAN_MIT}/qualitype/opentype/QTSchoolCentury-Italic.otf",
        "bold": f"{CTAN_MIT}/qualitype/opentype/QTSchoolCentury-Bold.otf",
        "bolditalic": f"{CTAN_MIT}/qualitype/opentype/QTSchoolCentury-BoldItalic.otf",
    },
    "Spectral": {
        "regular": f"{GF}/spectral/Spectral-Regular.ttf",
        "italic": f"{GF}/spectral/Spectral-Italic.ttf",
        "bold": f"{GF}/spectral/Spectral-Bold.ttf",
        "bolditalic": f"{GF}/spectral/Spectral-BoldItalic.ttf",
    },
    "SpectralLight": {
        "regular": f"{GF}/spectral/Spectral-Light.ttf",
        "italic": f"{GF}/spectral/Spectral-LightItalic.ttf",
        "bold": f"{GF}/spectral/Spectral-Bold.ttf",
        "bolditalic": f"{GF}/spectral/Spectral-BoldItalic.ttf",
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


def hinted_xheight_px(path, pt):
    face = freetype.Face(path)
    face.set_char_size(pt << 6, pt << 6, 150, 150)
    face.load_char("x", freetype.FT_LOAD_RENDER)
    return face.glyph.bitmap.rows


def ink_span_per_mille(path, chars):
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
        reg = fetch(spec["regular"], os.path.join(SRC, f"{name}-Regular.ttf"))
        ita = fetch(spec["italic"], os.path.join(SRC, f"{name}-Italic.ttf"))
        fetch(spec["bold"], os.path.join(SRC, f"{name}-Bold.ttf"))
        fetch(spec["bolditalic"], os.path.join(SRC, f"{name}-BoldItalic.ttf"))

        ramp = {}
        for pt in range(7, 30):
            ramp[pt] = hinted_xheight_px(reg, pt)

        sizes = []
        for target in XH_TARGETS:
            best = min(ramp, key=lambda p: (abs(ramp[p] - target), p))
            sizes.append(best)

        ymax_r, ymin_r = ink_span_per_mille(reg, PLAIN)
        ymax_i, ymin_i = ink_span_per_mille(ita, PLAIN)
        ink_top = max(ymax_r, ymax_i)
        ink_bot = min(ymin_r, ymin_i)
        floor_span = (ink_top + INK_PAD_PER_MILLE) - (ink_bot - INK_PAD_PER_MILLE)

        acc_top, _ = ink_span_per_mille(reg, ACCENTED_CAPS)

        cands = [ADVY_TARGETS[i] * 72.0 * 1000.0 / (150.0 * sizes[i]) for i in range(4)]
        best_span, best_err = None, None
        lo, hi = int(min(cands)) - 40, int(max(cands)) + 40
        for span in range(max(lo, int(floor_span)), hi + 1):
            err = sum((advy(span, sizes[i]) - ADVY_TARGETS[i]) ** 2 for i in range(4))
            if best_err is None or err < best_err:
                best_span, best_err = span, err
        span = max(best_span, int(round(floor_span)))

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
        }
        print(f"{name:20s} sizes={sizes} xh={out[name]['xheights']} "
              f"advY={out[name]['advY']} span={span} floor={floor_span:.0f} "
              f"acc_ok={out[name]['clears_accents']}")

    with open(os.path.join(HERE, "blind_slots.json"), "w") as f:
        json.dump(out, f, indent=2)
    print("\nwrote blind_slots.json")


if __name__ == "__main__":
    main()
