# Xteink X4 / X3 physical dimensions

Measured 2026-08-02 for the iOS harness's chassis-matched button pad (the
panel-to-buttons gap in `crosspoint-simulator/ios/CrossPointIOSShim.cpp`
derives from the X3 numbers below). Published body dimensions from Xteink's
product pages; everything else measured from Xteink's straight-on product
renders with the scale set by the known body width (X4 12.10 px/mm, X3
12.43 px/mm).

## Published body dimensions

| | X4 | X3 |
|---|---|---|
| Body H × W × D | 114 × 69 × 5.9 mm | 97.6 × 63.7 × 5.1 mm |
| Weight | 74 g | 58 g |
| Panel (marketing) | 4.3" (SDK: 4.26"), ~220 PPI | 3.7", ~257 PPI |
| Panel resolution | 800 × 480 | 792 × 528 (`FreeInkDisplay.h:51-52`) |

## Derived panel active area (pixels ÷ PPI)

| | X4 | X3 |
|---|---|---|
| Active area, portrait H × W | 92.4 × 55.4 mm | 78.2 × 52.1 mm |
| Vertical bezel budget (body − panel) | 21.6 mm | 19.4 mm |
| Horizontal bezel | 13.6 mm (~6.8/side) | 11.6 mm (~5.8/side) |

## Measured front geometry (±1 mm)

Both devices carry the four front buttons as **two pill-shaped rockers** in
the chin below the panel (which is what the iOS pad's fused-pair capsules
mirror). Portrait orientation, top of device = panel end:

| | X4 | X3 |
|---|---|---|
| Top bezel (panel to body top) | ~3.0 mm | ~2.7 mm |
| **Panel bottom → button-slot top** | **13.4 mm** | **11.6 mm** |
| Panel bottom → button-slot center | 15.0 mm | 13.0 mm |
| Button slot height | 3.1 mm | 2.8 mm |
| Chin (panel bottom → body bottom) | 18.6 mm | 16.7 mm |

**As a fraction of panel height** (the resolution-independent form the iOS
pad consumes):

| | X4 | X3 |
|---|---|---|
| Gap ÷ panel height | 13.4 / 92.4 = **14.5%** | 11.6 / 78.2 = **14.8%** |

The two devices agree within measurement error — the chassis proportion is
effectively one number, ~14.7% of panel height from panel edge to button top.

## Methodology and caveats

- Source images: Xteink's own product-page renders
  (`cdn/shop/files/9741757520180_.pic_hd.jpg` for X4,
  `1_eeff413c-adcf-4dc3-99d4-59dd9fcfaf0b.jpg` for X3), 1946²/2000² px.
- Scale anchored on the published body width; feature rows read off a
  20-px-grid overlay by eye (automated edge profiles were defeated by the
  renders' soft drop shadows).
- These are **renders, not photographs** — assumed true-scale. "Panel bottom"
  is the visible window seam; active pixels may inset a further ~0.5-1 mm.
- Internal consistency: measured chin + implied top bezel exactly consumes
  each device's body-minus-panel vertical budget on both devices.
- Calipers on the physical units supersede all of this; update the table if
  measured.

## Consumers

- `crosspoint-simulator/ios/CrossPointIOSShim.cpp` — `kPanelGapRatio =
  11.6 / 78.2` scales the pad's panel-to-top-row gap with the presented
  panel height (the app is X3-scoped).
