# Calendar sleep-screen render harness

Renders `CalendarSleepScreen` off-device using the **real** firmware
`GfxRenderer`, `EpdFont` and the compressed builtin fonts, then writes the
result through the real `serialiseFramebufferAsPortraitBmp()` path to
`./fs_/sleep.bmp`.

This exercises the actual on-device code — font metrics, glyph rasterisation,
the Portrait→panel rotation, and the 1-bit BMP writer — without needing SDL2 or
the PlatformIO simulator. It exists because layout bugs (text baseline vs. box
top, ink-vs-ascender centring, header overflow) are invisible in a
reimplementation and only show up against the real renderer.

The `Hal*.h` / `Arduino.h` files here are minimal host stubs: a RAM
framebuffer, `millis()`, and a `HalStorage` that maps SD paths onto `./fs_/`.

## Build & run

    cd tools/calendar_preview
    ./build.sh
    ./render_test 2026 7 27      # year month day  (defaults to 2026-07-27)
    # -> ./fs_/sleep.bmp  (528x792, 1-bit)

## Regression sweep

`sweep.py` renders several hundred dates and asserts nothing touches the panel
edges — this is what caught the cross-year header overflow
("Diciembre 2026 – Febrero 2027" is wider than 528px at 18pt bold).

    python3 sweep.py

## Centring check

`check_centering.py` renders several dates and, for every highlight box,
measures the offset between the box centre and the centre of the digit ink
inside it. This is the check that would have caught the original
baseline-vs-box-top bug, where digits sat ~8px low because `drawText`'s `y` is
the top of the ascender box (41px for Noto Sans 18) while digits are only 28px
of ink resting on the baseline.

    python3 check_centering.py
