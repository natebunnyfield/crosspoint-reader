#!/usr/bin/env python3
"""Build a 1-bit /sleep.bmp for the CUSTOM sleep screen.

Fits any source image onto the X4/X3 portrait panel (480x800), Floyd-Steinberg
dithered to 1bpp BI_RGB (the exact format lib/GfxRenderer/Bitmap.cpp accepts),
with an optional owner-name caption in the letterbox band below the art.

Usage:
  python3 scripts/make_sleep_bmp.py INPUT.png [--name "Owen Bunnyfield"] \
      [--out sleep.bmp] [--width 480] [--height 800]
"""
import argparse
import glob
import sys

from PIL import Image, ImageDraw, ImageFont

SERIF_CANDIDATES = [
    "/System/Library/Fonts/Supplemental/Georgia.ttf",
    "/System/Library/Fonts/Supplemental/Times New Roman.ttf",
    "/System/Library/Fonts/Times.ttc",
    "/Library/Fonts/Georgia.ttf",
]


def serif(size: int) -> ImageFont.FreeTypeFont:
    for path in SERIF_CANDIDATES:
        for candidate in glob.glob(path):
            try:
                return ImageFont.truetype(candidate, size)
            except OSError:
                continue
    return ImageFont.load_default()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("--name", default="Owen Bunnyfield")
    ap.add_argument("--out", default="sleep.bmp")
    ap.add_argument("--width", type=int, default=480)
    ap.add_argument("--height", type=int, default=800)
    args = ap.parse_args()

    W, H = args.width, args.height
    src = Image.open(args.input).convert("L")

    # With a caption: fit-width and reserve a band below (fit-height fallback
    # when the source is too tall). With --name "" (art carries its own text):
    # plain best-fit of the whole panel, centered, no band.
    scale = W / src.width
    fit_h = round(src.height * scale)
    canvas = Image.new("L", (W, H), 255)
    band_min = 64 if args.name else 0  # caption space; none when no name
    if fit_h <= H - band_min:
        art = src.resize((W, fit_h), Image.LANCZOS)
        canvas.paste(art, (0, (H - band_min - fit_h) // 2 if not args.name else 0))
        band_top = fit_h
    else:
        scale = (H - band_min) / src.height
        art = src.resize((round(src.width * scale), H - band_min), Image.LANCZOS)
        canvas.paste(art, ((W - art.width) // 2, 0))
        band_top = H - band_min

    if args.name:
        d = ImageDraw.Draw(canvas)
        band_h = H - band_top
        f = serif(min(34, max(20, band_h // 3)))
        tw = d.textlength(args.name, font=f)
        ty = band_top + (band_h - f.size) // 2
        # thin rule above the name, matching the art's inked frame
        d.line([(W // 2 - tw // 2, ty - 10), (W // 2 + tw // 2, ty - 10)], fill=0, width=2)
        d.text(((W - tw) // 2, ty), args.name, font=f, fill=0)

    # Floyd-Steinberg to 1-bit; PIL "1" mode saves as 1bpp BI_RGB BMP.
    canvas.convert("1").save(args.out, "BMP")
    print(f"{args.out}: {W}x{H} 1bpp, band at y={band_top}, name={args.name!r}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
