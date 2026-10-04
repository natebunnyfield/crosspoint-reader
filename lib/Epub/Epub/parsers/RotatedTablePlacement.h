#pragma once

// WHERE A WIDE TABLE'S LINES LAND ON ITS TURNED PAGE ([T-021]), for a reader who
// turns the device CLOCKWISE.
//
// Owner ruling 2026-08-19 (docs/ui-conventions.md): rotation is clockwise,
// always -- he is right-handed. T-021 nevertheless shipped this page drawn with
// GfxRenderer::drawTextRotated90CCW, whose own note says what that makes: a page
// the reader turns COUNTER-clockwise, the header running down the page's right
// edge. Found 2026-10-04 by the adversarial review of the iOS turned-page
// landscape (crosspoint-simulator/docs/turned-page-landscape-plan-2026-10-04.md),
// and ruled the same day: "Clockwise: fix the page".
//
// The fix is the old layout turned 180 degrees inside the viewport, drawn with
// drawTextRotated90CW. That is exact, not approximate: the two calls are each
// other's 180-degree mirror cursor for cursor (GfxRenderer.cpp renderCharImpl --
// CCW plots screenX = cx - (asc - top) - gy, screenY = cy + left + gx; CW plots
// screenX = cx + (asc - top) + gy, screenY = cy - left - gx), so a CCW run whose
// band's right edge is x and which starts at y is, turned around, a CW run whose
// band's LEFT edge is vw - 1 - x and which starts at vh - 1 - y. The row and
// reading axes the parser measures (`down`, `across`) are unchanged, so the
// fit, the wrapping and the one-page check are exactly what they were.
//
// Pure, and tested against the real renderer: test/rotated_text draws a line and
// the rule both ways and checks the clockwise page is the old one turned around,
// pixel for pixel.
namespace rotatedtable {

// A line's landing spot for drawTextRotated90CW: x is the band's LEFT edge (the
// ascender side), y the run's start; the run CLIMBS toward y = 0.
struct LinePlace {
  int x = 0;
  int y = 0;
};

// down: the line's distance from the table's top along the row axis (the first
// row starts at `margin`); across: its start along the reading axis (the column
// position, margin included); vh: the viewport's height. The viewport's width
// cancels out of the turn (vw - 1 - (vw - margin - down)), so it is not asked for.
inline LinePlace line(const int down, const int across, const int vh, const int margin) {
  // The CCW placement this replaces: x = vw - margin - down, y = across.
  return LinePlace{down + margin - 1, vh - 1 - across};
}

// The header rule, a vertical line `thickness` wide that drawLine thickens toward
// +x (GfxRenderer::drawLine, the lineWidth overload): its left column, its top,
// its length.
struct RulePlace {
  int x = 0;
  int y = 0;
  int length = 0;
};

// ruleDown: the rule's distance from the table's top along the row axis;
// length: how far it runs along the reading axis, starting `margin` in.
inline RulePlace rule(const int ruleDown, const int length, const int vh, const int margin, const int thickness) {
  // The CCW placement this replaces: columns vw - margin - ruleDown and the
  // thickness to its right, rows margin .. margin + length - 1.
  return RulePlace{ruleDown + margin - thickness, vh - margin - length, length};
}

}  // namespace rotatedtable
