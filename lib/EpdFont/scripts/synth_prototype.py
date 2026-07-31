#!/usr/bin/env python3
"""Validation harness for docs/synthetic-font-styles.md: build-time synthetic
bold / oblique for a regular-only face, using the exact load path
fontconvert_sdcard.py uses (14pt at 150 DPI).

Renders a four-line specimen strip:
  regular / synthetic bold / synthetic oblique / synthetic bold-oblique
plus a parameter sweep of embolden strengths, upsampled 4x for inspection.

    python3 synth_prototype.py [path/to/Font.ttf]

Needs pillow in addition to requirements.txt (inspection-only, so it is not
listed there).
"""
import ctypes
import math
import sys

import freetype
from PIL import Image

FONT = sys.argv[1] if len(sys.argv) > 1 else "GoudyBookletter1911.ttf"
TEXT = "Handgloves & Bmefjord 1911; quick?"
PT = 14
DPI = 150


def render_line(text, embolden_x=0, embolden_y=0, shear_deg=0.0):
    """Returns (list of (bitmapbytes,rows,width,pitch,left,top,adv), ascent_px)."""
    face = freetype.Face(FONT)
    face.set_char_size(PT << 6, PT << 6, DPI, DPI)
    if shear_deg:
        m = freetype.FT_Matrix()
        m.xx = 0x10000
        m.yy = 0x10000
        m.yx = 0
        m.xy = int(math.tan(math.radians(shear_deg)) * 0x10000)
        face.set_transform(m, freetype.FT_Vector(0, 0))
    out = []
    for ch in text:
        face.load_char(ch, freetype.FT_LOAD_DEFAULT | freetype.FT_LOAD_NO_BITMAP)
        slot = face.glyph
        if embolden_x or embolden_y:
            err = freetype.FT_Outline_EmboldenXY(
                ctypes.byref(slot.outline._FT_Outline), embolden_x, embolden_y)
            if err:
                raise RuntimeError(f"EmboldenXY failed on {ch!r}: {err}")
        freetype.FT_Render_Glyph(slot._FT_GlyphSlot, freetype.FT_RENDER_MODE_NORMAL)
        bmp = slot.bitmap
        adv = (slot.advance.x + embolden_x) / 64.0
        out.append((bytes(bmp.buffer), bmp.rows, bmp.width, abs(bmp.pitch),
                    slot.bitmap_left, slot.bitmap_top, adv))
    asc = face.size.ascender / 64.0
    return out, asc


def compose(lines, gap=8):
    rendered = []
    for label, kwargs in lines:
        glyphs, asc = render_line(TEXT, **kwargs)
        width = int(sum(g[6] for g in glyphs)) + 8
        height = int(asc) + 14
        img = Image.new("L", (width, height), 255)
        px = img.load()
        pen = 4.0
        for buf, rows, w, pitch, left, top, adv in glyphs:
            ox, oy = int(pen) + left, int(asc) - top
            for y in range(rows):
                for x in range(w):
                    v = buf[y * pitch + x]
                    if v:
                        xx, yy = ox + x, oy + y
                        if 0 <= xx < width and 0 <= yy < height:
                            px[xx, yy] = min(px[xx, yy], 255 - v)
            pen += adv
        rendered.append((label, img))
    total_w = max(i.width for _, i in rendered)
    total_h = sum(i.height + gap for _, i in rendered)
    sheet = Image.new("L", (total_w, total_h), 255)
    y = 0
    for _, img in rendered:
        sheet.paste(img, (0, y))
        y += img.height + gap
    return sheet


# ppem at 14pt/150dpi ~= 29.2; measured stem ~70/1000 em ~= 2.05px = 131 in 26.6.
# Bold targets ~1.55x stem: add ~0.55 * 131 ~= 72 (26.6 units). Vertical gets
# half of that so x-height and counters survive.
strip = compose([
    ("regular", {}),
    ("synth bold x72 y36", {"embolden_x": 72, "embolden_y": 36}),
    ("synth oblique 10deg", {"shear_deg": 10}),
    ("synth bold-oblique", {"embolden_x": 72, "embolden_y": 36, "shear_deg": 10}),
])
strip = strip.resize((strip.width * 4, strip.height * 4), Image.NEAREST)
strip.save("synth-specimen.png")

sweep = compose([
    ("x48 y24", {"embolden_x": 48, "embolden_y": 24}),
    ("x64 y32", {"embolden_x": 64, "embolden_y": 32}),
    ("x72 y36", {"embolden_x": 72, "embolden_y": 36}),
    ("x96 y48", {"embolden_x": 96, "embolden_y": 48}),
    ("x128 y64", {"embolden_x": 128, "embolden_y": 64}),
])
sweep = sweep.resize((sweep.width * 4, sweep.height * 4), Image.NEAREST)
sweep.save("synth-sweep.png")
print("wrote synth-specimen.png and synth-sweep.png")
