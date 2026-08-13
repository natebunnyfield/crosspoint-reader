#!/usr/bin/env python3
"""Perceptual (not geometric) match metrics for the Inknut/Junicode italic pairing.

Round 5 chose an ENLA value by rendered x-height parity, and the owner's answer
was that geometric parity still reads small. This measures the things that
actually make one face look the same size as another beside it:

  counter area   the enclosed white inside o/e/a/b/d/p/q/g. Two faces read as
                 the same size when their apertures do, more than when their
                 x-heights do -- a small-countered face at equal x-height looks
                 smaller and darker.
  ink covered    pixels the glyph actually paints, per glyph and per unit of
                 advance. This is "amount of pixels covered": the page colour
                 the italic contributes against the roman it sits inside.
  x-height       measured off the flat-topped 'n' as well as 'x'. 'n' is the
                 honest comparator: it is the same shape in both faces and has
                 no curve overshoot, so a 1 px difference there is a real one.

Everything is measured on the SAME rasterisation the .cpfont carries: FreeType
at 150 DPI, the font's own hinter, then the converter's 8 -> 4 -> 2 bit
quantisation (fontconvert_sdcard.py:713-765). Measuring the outlines instead
would miss the hinting, which is where round 4's "exact by construction" build
lost 2 px.

Then it applies the DEVICE rule, which is the part that makes these numbers
mean something. A reading page renders `BW`, and GfxRenderer.cpp:512 paints a
glyph pixel black at ANY non-zero level -- there is no antialiasing on the page
at all. So the ink the eye gets is level >= 1, and a counter is white only
where the level is exactly 0. Weighting by coverage would model a screen this
firmware never draws.

    python3 tools/italic-bench/measure_enla.py                 # 20 23 25 27
    python3 tools/italic-bench/measure_enla.py 9.8 15 20 27 35
"""

import importlib.util
import sys
from collections import deque
from pathlib import Path

import freetype

SCRIPT_DIR = Path(__file__).resolve().parent
REPO = SCRIPT_DIR.parent.parent
FONT_SCRIPTS = REPO / "lib" / "EpdFont" / "scripts"

# The pairing under test, from inknut-italic-bench.yaml (round 4/5 geometry).
INKNUT_LIGHT = "https://raw.githubusercontent.com/clauseggers/Inknut-Antiqua/master/TTF-OTF/InknutAntiqua-Light.ttf"
JUNICODE_ZIP = "https://github.com/psb1558/Junicode-font/releases/download/v2.226/Junicode_2.226.zip"
JUNICODE_VF = "Junicode/VAR/JunicodeVF-Italic.ttf"
CAP_MATCH_SCALE = 1.171  # M ink 0.7880 em roman / 0.6730 em italic
WGHT, WDTH = 600, 112.5
SIZES = [10, 12, 14, 16]  # the family's ordinal slots

# Closed counters only -- 'n'/'u' apertures are open and have no enclosed area.
COUNTER_LETTERS = "oeabdpqg"
# ...and of those, only the round bowls are the SAME SHAPE in both faces, so
# only they can carry a size comparison. Junicode's italic a and g are
# single-storey against Inknut's double-storey, which makes their counters
# differ by design rather than by size; e is an aperture, not a bowl.
BOWL_LETTERS = "obdpq"
# Lowercase letters weighted by English text frequency, so ink-per-advance is
# the colour of real prose rather than of the alphabet.
FREQ = {
    "e": 12.7, "t": 9.1, "a": 8.2, "o": 7.5, "i": 7.0, "n": 6.7, "s": 6.3,
    "h": 6.1, "r": 6.0, "d": 4.3, "l": 4.0, "c": 2.8, "u": 2.8, "m": 2.4,
    "w": 2.4, "f": 2.2, "g": 2.0, "y": 2.0, "p": 1.9, "b": 1.5, "v": 1.0,
    "k": 0.8, "j": 0.15, "x": 0.15, "q": 0.10, "z": 0.07,
}


def _load_builder():
    spec = importlib.util.spec_from_file_location(
        "build_sd_fonts", FONT_SCRIPTS / "build-sd-fonts.py")
    mod = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(FONT_SCRIPTS))
    spec.loader.exec_module(mod)
    return mod


BUILD = _load_builder()


def italic_face_path(enla: float) -> Path:
    """Instance the VF at (wght, wdth, ENLA), then apply the cap-match upem scale."""
    tag = f"MeasureE{enla:g}".replace(".", "p")
    src = BUILD.resolve_font_path(
        {"zip": JUNICODE_ZIP, "member": JUNICODE_VF,
         "variable": {"wght": WGHT, "wdth": WDTH, "ENLA": enla}},
        tag, "italic")
    return BUILD.apply_upem_scale(src, CAP_MATCH_SCALE, tag, "italic")


def roman_face_path() -> Path:
    return BUILD.resolve_font_path({"url": INKNUT_LIGHT}, "MeasureRoman", "regular")


def quantize2(v8: int) -> int:
    """8-bit coverage -> the converter's 2-bit level (fontconvert_sdcard.py:748-760)."""
    bm = v8 >> 4
    if bm >= 12:
        return 3
    if bm >= 8:
        return 2
    if bm >= 4:
        return 1
    return 0


def render(face, ch: str):
    """Return (levels[h][w], advance_px) for one character, as the .cpfont stores it."""
    face.load_char(ch, freetype.FT_LOAD_RENDER)
    bm = face.glyph.bitmap
    buf, pitch = bm.buffer, abs(bm.pitch)
    rows = [[quantize2(buf[y * pitch + x]) for x in range(bm.width)]
            for y in range(bm.rows)]
    return rows, face.glyph.advance.x / 64.0


def ink_px(levels):
    """Pixels the panel actually paints black: every level >= 1."""
    return sum(1 for row in levels for v in row if v)


def counter_area(levels):
    """Enclosed white inside a glyph, as the panel shows it.

    Ink is level >= 1, because BW mode paints every non-zero level black
    (GfxRenderer.cpp:512). A counter is therefore only the pixels left at
    level 0 -- a bowl whose inside is all faint antialiasing has no counter on
    this device, however open it looks in the outline.
    """
    h = len(levels)
    if not h:
        return 0
    w = len(levels[0])
    # Pad by one so a counter touching the bitmap edge cannot leak out.
    seen = [[False] * (w + 2) for _ in range(h + 2)]

    def is_ink(y, x):
        if y == 0 or x == 0 or y > h or x > w:
            return False
        return levels[y - 1][x - 1] >= 1

    q = deque([(0, 0)])
    seen[0][0] = True
    while q:
        y, x = q.popleft()
        for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
            if 0 <= ny <= h + 1 and 0 <= nx <= w + 1 and not seen[ny][nx] and not is_ink(ny, nx):
                seen[ny][nx] = True
                q.append((ny, nx))

    area = 0
    for y in range(1, h + 1):
        for x in range(1, w + 1):
            if not is_ink(y, x) and not seen[y][x]:
                area += 1
    return area


def measure(path: Path, size: int):
    face = freetype.Face(str(path))
    face.set_char_size(size << 6, size << 6, 150, 150)

    x_levels, _ = render(face, "x")
    n_levels, _ = render(face, "n")
    m_levels, _ = render(face, "M")

    counters = {ch: counter_area(render(face, ch)[0]) for ch in COUNTER_LETTERS}

    ink = advance = 0.0
    weight_total = 0.0
    for ch, weight in FREQ.items():
        levels, adv = render(face, ch)
        ink += weight * ink_px(levels)
        advance += weight * adv
        weight_total += weight

    bowls = [counters[ch] for ch in BOWL_LETTERS]
    return {
        "x_px": len(x_levels),
        "n_px": len(n_levels),
        "cap_px": len(m_levels),
        "bowl_px": sum(bowls) / len(bowls),
        "counters": counters,
        "ink_per_adv": ink / advance,
        "ink_per_glyph": ink / weight_total,
    }


def main():
    enlas = [float(a) for a in sys.argv[1:]] or [20.0, 23.0, 25.0, 27.0]
    roman = roman_face_path()
    italics = {e: italic_face_path(e) for e in enlas}

    for size in SIZES:
        r = measure(roman, size)
        print(f"\n=== {size} pt  (ppem {size * 150 / 72:.1f}) "
              f"— roman: Inknut Antiqua Light, BW ink (level >= 1) ===")
        print(f"{'face':>10} {'x px':>5} {'n px':>5} {'cap':>4} "
              f"{'bowl':>6} {'ink/adv':>8} {'ink/glyph':>10}")
        print(f"{'roman':>10} {r['x_px']:>5} {r['n_px']:>5} {r['cap_px']:>4} "
              f"{r['bowl_px']:>6.1f} {r['ink_per_adv']:>8.2f} {r['ink_per_glyph']:>10.1f}")
        for enla in enlas:
            m = measure(italics[enla], size)
            print(f"{'ENLA ' + f'{enla:g}':>10} {m['x_px']:>5} {m['n_px']:>5} {m['cap_px']:>4} "
                  f"{m['bowl_px']:>6.1f} {m['ink_per_adv']:>8.2f} {m['ink_per_glyph']:>10.1f}"
                  f"   bowl {m['bowl_px'] / r['bowl_px'] * 100:5.1f}%"
                  f"  ink/glyph {m['ink_per_glyph'] / r['ink_per_glyph'] * 100:5.1f}%")

    print("\nper-letter counter area (px, level-0 pixels fully enclosed by ink)")
    print("bowls o b d p q are structurally comparable; a e g are not — Junicode's")
    print("italic a and g are single-storey where Inknut's are double.")
    for size in (12, 16):
        print(f"\n  {size} pt")
        r = measure(roman, size)
        print(f"{'face':>10} " + " ".join(f"{ch:>4}" for ch in COUNTER_LETTERS))
        print(f"{'roman':>10} " + " ".join(f"{r['counters'][ch]:>4}" for ch in COUNTER_LETTERS))
        for enla in enlas:
            m = measure(italics[enla], size)
            print(f"{'ENLA ' + f'{enla:g}':>10} " +
                  " ".join(f"{m['counters'][ch]:>4}" for ch in COUNTER_LETTERS))


if __name__ == "__main__":
    main()
