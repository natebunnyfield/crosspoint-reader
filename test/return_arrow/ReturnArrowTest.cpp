// The keyboard's Return/OK arrowhead: it has to come to a POINT.
//
// Reported by the owner, 2026-08-15: "the arrowhead on the return icon does not
// come to a triangular point."
//
// The old construction was two thick line()s fanning from a shared origin
// (freeink-sdk .../components/keyboard/keyboard.h, before this change). That
// cannot produce a point, and the reason is mechanical rather than a matter of
// taste: GfxRenderer::drawLine's thick form replicates a 1px Bresenham run
// lineWidth times along the run's MINOR axis (GfxRenderer.cpp:940-954), so each
// stroke terminates in a FLAT CAP kArrowStroke wide. Two flat caps at the same
// origin overlap into a lozenge. The apex column is as wide as the stroke.
//
// A filled triangle has an actual vertex. DrawTarget::triangle() already existed
// (FreeInkUICore.h:704) and already routed to GfxRenderer::fillPolygon
// (FreeInkUIGfxRenderer.h:123-128); the old code simply did not use it.
//
// This suite reproduces BOTH constructions against the real renderer and pins
// the difference, because "it looks pointy now" is not a claim anyone can check
// later. It is compiled twice -- once at RENDER_SCALE=1 (every device build) and
// once at 3 (the value the shipped iOS app pins,
// crosspoint-simulator/ios/CMakeLists.txt:89,208) -- because the apex is exactly
// where the two resolutions disagree: fillPolygon rasterizes at device
// resolution now, so the hypotenuse sharpens with the panel instead of
// staircasing at a third of it.

#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

HalDisplay display;

namespace {

class Gfx {
 public:
  static GfxRenderer& renderer() {
    static Gfx g;
    return g.renderer_;
  }

 private:
  Gfx() : renderer_(display) { renderer_.begin(); }
  GfxRenderer renderer_;
};

// Ink at a DEVICE coordinate. Portrait maps (x, y) -> (phyX = y,
// phyY = panelHeight - 1 - x) and clears the bit for ink, MSB first. The panel
// constants are already device-sized (HalDisplay.h multiplies by
// CROSSPOINT_RENDER_SCALE), so no scaling belongs here. Lifted from
// test/hires_shapes, which pins the slanted-thick-line path the same way.
bool inkAtDevice(const int dx, const int dy) {
  const int phyX = dy;
  const int phyY = HalDisplay::DISPLAY_HEIGHT - 1 - dx;
  if (phyX < 0 || phyX >= HalDisplay::DISPLAY_WIDTH || phyY < 0 || phyY >= HalDisplay::DISPLAY_HEIGHT) return false;
  const uint8_t* fb = display.getFrameBuffer();
  const uint32_t byteIndex = static_cast<uint32_t>(phyY) * HalDisplay::DISPLAY_WIDTH_BYTES + (phyX / 8);
  return ((fb[byteIndex] >> (7 - (phyX % 8))) & 1) == 0;
}

constexpr int S = GfxRenderer::RENDER_SCALE;

// The glyph's inputs, in LOGICAL pixels, exactly as keyboard.h derives them.
// lh is a representative UI label line height; the geometry below is a
// transcription of the drawing code, not an approximation of it.
constexpr int16_t kArrowStroke = 4;  // kSpecialStroke + 1, keyboard.h
constexpr int lh = 20;
constexpr int keyX = 40, keyY = 40, keyW = 44, keyH = 44;

struct Box {
  int x0, y0, x1, y1;  // device coords, inclusive
};

// Draw the arrow the way it was drawn BEFORE this change.
void drawOldArrow(const GfxRenderer& r) {
  const int w = lh * 3 / 4, h = lh / 2;
  const int cx = keyX + keyW / 2, cy = keyY + keyH / 2;
  const int left = cx - w / 2, right = cx + w / 2;
  const int top = cy - h / 2, bottom = cy + h / 2;
  const int barb = h / 2;
  r.fillRect(right - kArrowStroke + 1, top, kArrowStroke, bottom - top + 1, true);
  r.fillRoundedRect(left, bottom, right - left + 1, kArrowStroke, kArrowStroke, false, false, false, true, Black);
  r.drawLine(left, bottom, left + barb, bottom - barb, kArrowStroke, true);
  r.drawLine(left, bottom, left + barb, bottom + barb, kArrowStroke, true);
}

// Draw the arrow the way it is drawn NOW.
void drawNewArrow(const GfxRenderer& r) {
  const int w = lh * 3 / 4, h = lh / 2;
  const int cx = keyX + keyW / 2, cy = keyY + keyH / 2;
  const int left = cx - w / 2, right = cx + w / 2;
  const int headHalf = h / 2, headLen = h * 2 / 3;
  const int above = h;
  const int below = headHalf > kArrowStroke / 2 ? headHalf : kArrowStroke / 2;
  const int yMid = cy + (above - below) / 2;
  const int top = yMid - above;
  const int shaftTop = yMid - kArrowStroke / 2;
  const int baseX = left + headLen;
  r.fillRect(right - kArrowStroke + 1, top, kArrowStroke, shaftTop - top + 1, true);
  r.fillRoundedRect(baseX, shaftTop, right - baseX + 1, kArrowStroke, kArrowStroke, false, false, false, true, Black);
  const int xs[3] = {left, baseX, baseX};
  const int ys[3] = {yMid, yMid - headHalf, yMid + headHalf};
  r.fillPolygon(xs, ys, 3, true);
}

// The device-space box the glyph can occupy, generously padded.
Box glyphBox() { return Box{(keyX - 4) * S, (keyY - 4) * S, (keyX + keyW + 4) * S, (keyY + keyH + 4) * S}; }

// Height, in device pixels, of the inked run in a device COLUMN.
int inkHeightInColumn(const int dx, const Box& b) {
  int n = 0;
  for (int dy = b.y0; dy <= b.y1; ++dy)
    if (inkAtDevice(dx, dy)) n++;
  return n;
}

// The leftmost device column carrying any ink.
int leftmostInkColumn(const Box& b) {
  for (int dx = b.x0; dx <= b.x1; ++dx)
    if (inkHeightInColumn(dx, b) > 0) return dx;
  return -1;
}

// Dump the glyph as a PGM so the change can be LOOKED at, not just asserted.
// Written only when CROSSPOINT_ARROW_DUMP names a directory.
void dumpPgm(const std::string& name, const Box& b) {
  const char* dir = std::getenv("CROSSPOINT_ARROW_DUMP");
  if (!dir) return;
  const std::string path = std::string(dir) + "/" + name + "_s" + std::to_string(S) + ".pgm";
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return;
  const int w = b.x1 - b.x0 + 1, h = b.y1 - b.y0 + 1;
  std::fprintf(f, "P5\n%d %d\n255\n", w, h);
  std::vector<unsigned char> row(w);
  for (int dy = b.y0; dy <= b.y1; ++dy) {
    for (int dx = b.x0; dx <= b.x1; ++dx) row[dx - b.x0] = inkAtDevice(dx, dy) ? 0 : 255;
    std::fwrite(row.data(), 1, row.size(), f);
  }
  std::fclose(f);
}

class ReturnArrow : public ::testing::Test {
 protected:
  void SetUp() override {
    r_ = &Gfx::renderer();
    r_->clearScreen(0xFF);
  }
  const GfxRenderer* r_ = nullptr;
};

// The heart of it. The apex column of a real triangle carries a SINGLE run that
// is far thinner than the stroke; the old lozenge carried a full-stroke cap.
TEST_F(ReturnArrow, NewHeadComesToAPoint) {
  drawNewArrow(*r_);
  const Box b = glyphBox();
  dumpPgm("new", b);

  const int apex = leftmostInkColumn(b);
  ASSERT_GE(apex, 0) << "nothing was drawn";

  // A point means the apex column is at most one device pixel tall per unit of
  // supersampling -- i.e. the tip is a vertex, not a cap. The old construction
  // put a full kArrowStroke-wide flat cap here.
  const int apexHeight = inkHeightInColumn(apex, b);
  EXPECT_LE(apexHeight, S) << "apex column is " << apexHeight << " device px tall; a point should be <= " << S;
  EXPECT_LT(apexHeight, kArrowStroke * S) << "apex is as thick as the stroke -- that is a flat cap, not a point";
}

// ... and it must WIDEN away from the apex, monotonically, on both sides of the
// axis. A lozenge widens then narrows again; a wedge only widens.
TEST_F(ReturnArrow, NewHeadWidensMonotonicallyFromTheApex) {
  drawNewArrow(*r_);
  const Box b = glyphBox();
  const int apex = leftmostInkColumn(b);
  ASSERT_GE(apex, 0);

  // Sample only the head: from the apex to just short of the base, so the
  // shaft's own rows cannot be counted as head growth.
  const int headLen = (lh / 2) * 2 / 3;
  int prev = 0;
  for (int dx = apex; dx < apex + (headLen - 1) * S; ++dx) {
    const int height = inkHeightInColumn(dx, b);
    EXPECT_GE(height, prev) << "head narrows at device column " << dx << " -- not a wedge";
    prev = height;
  }
  EXPECT_GT(prev, kArrowStroke * S) << "head never grew wider than the shaft; it would not read as a head";
}

// The regression this replaces. Kept as an executable record of WHY the shape
// changed: it asserts the old construction's failure, so nobody can quietly
// reinstate two fanned strokes and still pass the suite above.
TEST_F(ReturnArrow, OldTwoStrokeHeadCouldNotFormAPoint) {
  drawOldArrow(*r_);
  const Box b = glyphBox();
  dumpPgm("old", b);

  const int apex = leftmostInkColumn(b);
  ASSERT_GE(apex, 0);
  const int apexHeight = inkHeightInColumn(apex, b);
  // Both barbs and the shaft all start at {left, bottom}, so the leftmost column
  // is a stack of flat caps -- provably blunt.
  EXPECT_GE(apexHeight, kArrowStroke * S)
      << "the old head was expected to be a full-stroke flat cap; it measured " << apexHeight;
}

// ---------------------------------------------------------------------------
// The text cursor's I-beam, from the same keyboard screen
// (src/activities/util/KeyboardEntryActivity.cpp). Same class of fault as the
// arrowhead -- strokes placed by eye around a stem rather than measured against
// it -- so it is pinned here rather than in a suite of its own.

constexpr int kSerifW = 3, kStemW = 2, kSerifH = 2, kCursorLineH = 18;
constexpr int curX = 60, curY = 30;

void drawOldIBeam(const GfxRenderer& r) {
  const int cBottom = curY + kCursorLineH - 1;
  r.fillRect(curX, curY, 2, kCursorLineH, true);
  r.drawLine(curX - kSerifW, curY, curX - 1, curY, 2, true);
  r.drawLine(curX + 1, curY, curX + kSerifW, curY, 2, true);
  r.drawLine(curX - kSerifW, cBottom, curX - 1, cBottom, 2, true);
  r.drawLine(curX + 1, cBottom, curX + kSerifW, cBottom, 2, true);
}

void drawNewIBeam(const GfxRenderer& r) {
  const int cBottom = curY + kCursorLineH - 1;
  const int notchH = 1;
  r.fillRect(curX, curY + notchH, kStemW, kCursorLineH - notchH * 2, true);
  r.fillRect(curX - kSerifW, curY, kSerifW, notchH, true);
  r.fillRect(curX + kStemW, curY, kSerifW, notchH, true);
  r.fillRect(curX - kSerifW, curY + notchH, kStemW + kSerifW * 2, kSerifH - notchH, true);
  r.fillRect(curX - kSerifW, cBottom - kSerifH + 1, kStemW + kSerifW * 2, kSerifH - notchH, true);
  r.fillRect(curX - kSerifW, cBottom, kSerifW, notchH, true);
  r.fillRect(curX + kStemW, cBottom, kSerifW, notchH, true);
  r.fillRect(curX - 1, curY + kCursorLineH / 2 + 1, kStemW + 2, 1, true);
}

// A stem drawn FULL HEIGHT bridges its own notch. This reproduces that mistake
// so the byte-identical result is on the record: it is the reason the stem is
// inset by notchH rather than the serif being deepened.
void drawIBeamNotchBridgedByStem(const GfxRenderer& r) {
  const int cBottom = curY + kCursorLineH - 1;
  r.fillRect(curX, curY, kStemW, kCursorLineH, true);
  r.fillRect(curX - kSerifW, curY, kSerifW, 1, true);
  r.fillRect(curX + kStemW, curY, kSerifW, 1, true);
  r.fillRect(curX - kSerifW, curY + 1, kStemW + kSerifW * 2, kSerifH - 1, true);
  r.fillRect(curX - kSerifW, cBottom - kSerifH + 1, kStemW + kSerifW * 2, kSerifH - 1, true);
  r.fillRect(curX - kSerifW, cBottom, kSerifW, 1, true);
  r.fillRect(curX + kStemW, cBottom, kSerifW, 1, true);
}

// The plain bar, kept only as the comparison the test above needs.
void drawIBeamSolidSerifs(const GfxRenderer& r) {
  const int cBottom = curY + kCursorLineH - 1;
  r.fillRect(curX, curY, kStemW, kCursorLineH, true);
  r.fillRect(curX - kSerifW, curY, kStemW + kSerifW * 2, kSerifH, true);
  r.fillRect(curX - kSerifW, cBottom - kSerifH + 1, kStemW + kSerifW * 2, kSerifH, true);
}

Box cursorBox() {
  return Box{(curX - kSerifW - 3) * S, (curY - 3) * S, (curX + kStemW + kSerifW + 3) * S,
             (curY + kCursorLineH + 3) * S};
}

// The serif bar must overhang the stem EQUALLY on both sides. The old form put
// three columns left of the stem and two right of it, because the right stroke
// started on the stem's own second column instead of past it.
TEST_F(ReturnArrow, NewCursorSerifsAreSymmetricAboutTheStem) {
  drawNewIBeam(*r_);
  const Box b = cursorBox();
  dumpPgm("cursor_new", b);

  const int stemLeft = curX * S;
  const int stemRight = (curX + kStemW) * S - 1;
  int barLeft = -1, barRight = -1;
  const int topRow = curY * S;
  for (int dx = b.x0; dx <= b.x1; ++dx) {
    if (!inkAtDevice(dx, topRow)) continue;
    if (barLeft < 0) barLeft = dx;
    barRight = dx;
  }
  ASSERT_GE(barLeft, 0);
  EXPECT_EQ(stemLeft - barLeft, barRight - stemRight)
      << "serif overhang is " << (stemLeft - barLeft) << " left vs " << (barRight - stemRight) << " right";
}

// Both serif bars must sit INSIDE the stem's vertical extent. drawLine grows a
// horizontal run's thickness downward, so the old bottom bar -- anchored on the
// stem's last row -- hung one row below the stem while the top bar did not.
TEST_F(ReturnArrow, NewCursorSerifsStayWithinTheStem) {
  drawNewIBeam(*r_);
  const Box b = cursorBox();
  const int stemBottom = (curY + kCursorLineH) * S - 1;
  int lowest = -1;
  for (int dy = b.y0; dy <= b.y1; ++dy)
    for (int dx = b.x0; dx <= b.x1; ++dx)
      if (inkAtDevice(dx, dy)) {
        lowest = dy;
        break;
      }
  EXPECT_EQ(lowest, stemBottom) << "ink runs to device row " << lowest << ", stem ends at " << stemBottom;
}

TEST_F(ReturnArrow, OldCursorSerifsWereLopsided) {
  drawOldIBeam(*r_);
  const Box b = cursorBox();
  dumpPgm("cursor_old", b);

  const int stemLeft = curX * S;
  const int stemRight = (curX + kStemW) * S - 1;
  int barLeft = -1, barRight = -1;
  const int topRow = curY * S;
  for (int dx = b.x0; dx <= b.x1; ++dx) {
    if (!inkAtDevice(dx, topRow)) continue;
    if (barLeft < 0) barLeft = dx;
    barRight = dx;
  }
  ASSERT_GE(barLeft, 0);
  EXPECT_NE(stemLeft - barLeft, barRight - stemRight)
      << "the old cursor was expected to be lopsided; it measured symmetric";
}

// The head sits on the shaft's centreline, not on its top row. The old barbs
// fanned from the shaft's TOP row, so the head rode half a stroke high.
TEST_F(ReturnArrow, NewHeadIsCentredOnTheShaft) {
  drawNewArrow(*r_);
  const Box b = glyphBox();
  const int apex = leftmostInkColumn(b);
  ASSERT_GE(apex, 0);

  // Find the apex's device row, then the shaft's vertical centre far to the
  // right of the head, and require them to agree within one logical pixel.
  int apexRow = -1;
  for (int dy = b.y0; dy <= b.y1 && apexRow < 0; ++dy)
    if (inkAtDevice(apex, dy)) apexRow = dy;
  ASSERT_GE(apexRow, 0);

  const int w = lh * 3 / 4;
  const int cx = keyX + keyW / 2;
  const int shaftSampleX = (cx + w / 2 - kArrowStroke - 1) * S;
  int first = -1, last = -1;
  for (int dy = b.y0; dy <= b.y1; ++dy) {
    if (!inkAtDevice(shaftSampleX, dy)) continue;
    if (first < 0) first = dy;
    last = dy;
  }
  ASSERT_GE(first, 0) << "found no shaft to measure against";
  const int shaftMid = (first + last) / 2;
  EXPECT_LE(std::abs(apexRow - shaftMid), S)
      << "apex row " << apexRow << " vs shaft centre " << shaftMid << " -- head is not on the shaft's axis";
}

// The head's two halves must be MIRROR IMAGES, row for row.
//
// Owner report, 2026-08-15: "the top half is missing ink on its left side."
// Measured before the fix, with the apex on row 28: the left edge stepped
// 19, 21, 22, 23, 24 going up and 19, 20, 21, 22, 23 going down -- one column
// tighter above throughout -- and the topmost row was absent entirely, so the
// head had four rows above the apex and five below.
//
// Both faults were in GfxRenderer::fillPolygon, not in the geometry here: the
// vertices have always been symmetric about yMid. The scanline sweep computed
// each boundary x by integer division whose truncation direction followed the
// edge-walk order, which is opposite in the two halves, and its parity rule
// excluded the row at each edge's minimum y. Triangles now rasterize through
// exact integer half-space tests, which mirror by construction.
//
// This asserts the SHAPE, so it holds whatever the rasterizer does next.
TEST_F(ReturnArrow, NewHeadIsSymmetricAboutTheApexRow) {
  drawNewArrow(*r_);
  const Box b = glyphBox();
  const int apex = leftmostInkColumn(b);
  ASSERT_GE(apex, 0);

  // The apex's row, taken as the midpoint of the apex column's own ink so a
  // multi-row apex (RENDER_SCALE > 1) does not bias the axis upward.
  int first = -1, last = -1;
  for (int dy = b.y0; dy <= b.y1; ++dy) {
    if (!inkAtDevice(apex, dy)) continue;
    if (first < 0) first = dy;
    last = dy;
  }
  ASSERT_GE(first, 0);
  const int axis2 = first + last;  // twice the axis row, so it stays integral

  // Compare only rows the HEAD spans. Past the base the riser rises on one side
  // only, by design, and would read as asymmetry that is not there.
  const int headHalf = (lh / 2) / 2;
  const int headLen = (lh / 2) * 2 / 3;
  const int baseX = apex + (headLen - 1) * S;

  for (int k = 1; k <= headHalf * S; ++k) {
    const int upRow = (axis2 - 2 * k) / 2;
    const int downRow = (axis2 + 2 * k) / 2;
    int leftUp = -1, leftDown = -1;
    for (int dx = apex; dx <= baseX; ++dx) {
      if (leftUp < 0 && inkAtDevice(dx, upRow)) leftUp = dx;
      if (leftDown < 0 && inkAtDevice(dx, downRow)) leftDown = dx;
    }
    EXPECT_EQ(leftUp, leftDown) << "row " << upRow << " above the apex starts at column " << leftUp << ", its mirror "
                                << downRow << " starts at " << leftDown << " -- the head is lopsided";
  }
}

// The notch is REAL ink removed, not a redraw that looks different in prose.
TEST_F(ReturnArrow, NewIBeamSerifsAreNotchedOnTheCentreline) {
  drawNewIBeam(*r_);
  const Box b = cursorBox();
  dumpPgm("cursor_new", b);
  // On the serif's outer row the centreline must be BLANK and the arms inked.
  for (const int dy : {curY * S, (curY + kCursorLineH - 1) * S + S - 1}) {
    EXPECT_FALSE(inkAtDevice(curX * S, dy)) << "centreline is inked at row " << dy << " -- no notch";
    EXPECT_TRUE(inkAtDevice((curX - 1) * S, dy)) << "left arm missing at row " << dy;
    EXPECT_TRUE(inkAtDevice((curX + kStemW) * S, dy)) << "right arm missing at row " << dy;
  }
}

// A full-height stem fills the notch. Pinning it byte-for-byte, because the
// difference between the two constructions is invisible in the source.
TEST_F(ReturnArrow, AFullHeightStemBridgesItsOwnNotch) {
  const Box b = cursorBox();
  r_->clearScreen(0xFF);
  drawIBeamNotchBridgedByStem(*r_);
  std::vector<bool> bridged;
  for (int dy = b.y0; dy <= b.y1; ++dy)
    for (int dx = b.x0; dx <= b.x1; ++dx) bridged.push_back(inkAtDevice(dx, dy));
  r_->clearScreen(0xFF);
  drawIBeamSolidSerifs(*r_);
  std::vector<bool> solid;
  for (int dy = b.y0; dy <= b.y1; ++dy)
    for (int dx = b.x0; dx <= b.x1; ++dx) solid.push_back(inkAtDevice(dx, dy));
  EXPECT_EQ(bridged, solid) << "expected the bridged notch to be indistinguishable from a solid bar";
}

// The crossbar sits below centre and is PROUD of the stem, or it is just a
// thicker stem row and reads as nothing.
TEST_F(ReturnArrow, NewIBeamCarriesABaselineCrossbar) {
  drawNewIBeam(*r_);
  const int beamY = (curY + kCursorLineH / 2 + 1) * S;
  EXPECT_TRUE(inkAtDevice((curX - 1) * S, beamY)) << "crossbar does not overhang left of the stem";
  EXPECT_TRUE(inkAtDevice((curX + kStemW) * S, beamY)) << "crossbar does not overhang right of the stem";
  // The row above it must be stem-only, so the bar is a distinct mark.
  EXPECT_FALSE(inkAtDevice((curX - 1) * S, beamY - S)) << "row above the crossbar is inked -- not a distinct bar";
  EXPECT_GT(beamY, (curY + kCursorLineH / 2) * S - 1) << "crossbar is not below centre";
}

}  // namespace
