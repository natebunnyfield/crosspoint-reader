#pragma once

// WHERE A WIDE TABLE'S LINES LAND ON ITS TURNED PAGE ([T-021]).
//
// THE DIRECTION, settled 2026-10-04 after a wrong turn the same day. The
// owner's "rotation is clockwise" (ruling 2026-08-19, docs/ui-conventions.md)
// is about the TABLE: it is turned 90 degrees clockwise on the portrait page --
// header down the page's RIGHT edge, every run descending -- and the reader
// reads it by turning the device COUNTER-clockwise. That is the page the
// 2026-08-19 renders showed (tools/table_preview), the page T-021 shipped, and
// what drawTextRotated90CCW draws (the call is named for the reader's turn).
// On 2026-10-04 an adversarial review read the ruling as the READER's turn,
// the page was flipped to drawTextRotated90CW for one TestFlight build (304),
// and the owner corrected it: *"the iphone would need to be turned ccw not
// clockwise, you've mixed things up"*. This file restores the original page.
//
// Pure, so the arithmetic is tested against the real renderer
// (test/rotated_text): the header is the band nearest the page's right edge,
// every run starts at the top and descends, nothing lands off the screen.
namespace rotatedtable {

// A line's landing spot for drawTextRotated90CCW: x is the band's RIGHT edge
// (the ascender side; ink lies to its left), y where the run starts; the run
// DESCENDS toward larger y.
struct LinePlace {
  int x = 0;
  int y = 0;
};

// down: the line's distance from the table's top along the row axis (the first
// row starts at `margin`); across: its start along the reading axis (the column
// position, margin included); vw: the viewport's width. The row axis runs
// right to left across the page, so the first row hugs the right edge.
inline LinePlace line(const int down, const int across, const int vw, const int margin) {
  return LinePlace{vw - margin - down, across};
}

// The header rule, a vertical line that drawLine thickens toward +x
// (GfxRenderer::drawLine, the lineWidth overload): its left column, its top,
// its length.
struct RulePlace {
  int x = 0;
  int y = 0;
  int length = 0;
};

// ruleDown: the rule's distance from the table's top along the row axis;
// length: how far it runs along the reading axis, starting `margin` down.
inline RulePlace rule(const int ruleDown, const int length, const int vw, const int margin) {
  return RulePlace{vw - margin - ruleDown, margin, length};
}

}  // namespace rotatedtable
