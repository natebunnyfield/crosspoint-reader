// Slanted strokes on a SUPERSAMPLED build.
//
// `GfxRenderer::drawPixel` paints a RENDER_SCALE x RENDER_SCALE block of device
// pixels (GfxRenderer.cpp:557-565), which keeps every non-glyph primitive
// looking exactly as it does at scale 1. For an axis-aligned edge that is
// correct -- a scaled straight line is still straight. For a DIAGONAL it scales
// the staircase with the shape, so a drawn diagonal sits at 1/RENDER_SCALE of
// the resolution of the text beside it, which does take the device-pixel path.
//
// That gap is only reachable above scale 1, and the shipped iOS app pins
// RENDER_SCALE=3 (crosspoint-simulator/ios/CMakeLists.txt:89,208) -- so it is a
// real surface, not a lab curiosity. It was reported against the on-screen
// keyboard's Return arrow, whose two barbs are the only slanted strokes drawn on
// the panel (freeink-sdk .../components/keyboard/keyboard.h:681-686).
//
// This suite is built at RENDER_SCALE=3 for the same reason test/system_font is
// built at 2: at scale 1 the whole path is preprocessed away and every case here
// would assert nothing.
//
// What is pinned, and why each half matters:
//   * a slanted thick line advances by ONE device pixel per device row, so the
//     fix cannot silently regress to block replication;
//   * its total weight is unchanged (lineWidth logical rows), so the fix cannot
//     drift into a different stroke weight than the device renders;
//   * an axis-aligned thick line still block-replicates exactly, so the change
//     stays confined to diagonals.

#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <set>
#include <vector>

HalDisplay display;

namespace {

static_assert(GfxRenderer::RENDER_SCALE > 1, "this suite is meaningless at scale 1");

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
// phyY = panelHeight - 1 - x) and clears the bit for ink, MSB first
// (GfxRenderer.cpp:234-239, :608-614). The panel constants are already
// device-sized (HalDisplay.h:20-21 multiplies by CROSSPOINT_RENDER_SCALE), so no
// scaling belongs here.
bool inkAtDevice(const int dx, const int dy) {
  const int phyX = dy;
  const int phyY = HalDisplay::DISPLAY_HEIGHT - 1 - dx;
  if (phyX < 0 || phyX >= HalDisplay::DISPLAY_WIDTH || phyY < 0 || phyY >= HalDisplay::DISPLAY_HEIGHT) return false;
  const uint8_t* fb = display.getFrameBuffer();
  const uint32_t byteIndex = static_cast<uint32_t>(phyY) * HalDisplay::DISPLAY_WIDTH_BYTES + (phyX / 8);
  return ((fb[byteIndex] >> (7 - (phyX % 8))) & 1) == 0;
}

// Leftmost inked device column on a device row, or -1.
int leftmostInk(const int dy, const int searchFrom, const int searchTo) {
  for (int dx = searchFrom; dx < searchTo; ++dx) {
    if (inkAtDevice(dx, dy)) return dx;
  }
  return -1;
}

class HiResShapes : public ::testing::Test {
 protected:
  void SetUp() override {
    r = &Gfx::renderer();
    r->clearScreen();
  }
  GfxRenderer* r = nullptr;
};

constexpr int S = GfxRenderer::RENDER_SCALE;

// A 45-degree run, the shape the Return arrow's barbs are.
constexpr int kX1 = 60, kY1 = 60, kX2 = 80, kY2 = 40, kWidth = 4;

// The regression this suite exists for. Under block replication the leading edge
// holds still for RENDER_SCALE device rows and then jumps RENDER_SCALE columns,
// so the distinct leading-edge positions number (rows / S). At device
// resolution it moves every row.
TEST_F(HiResShapes, SlantedThickLineAdvancesEveryDeviceRow) {
  r->drawLine(kX1, kY1, kX2, kY2, kWidth, true);

  // Only the rows whose leading edge IS the diagonal. Thickness is a shear
  // along y, so the band's lower-left end is a vertical cut lineWidth * S rows
  // tall; its leftmost column is legitimately constant there and says nothing
  // about the sampling grid.
  const int firstRow = kY2 * S;
  const int lastRow = kY1 * S - 1;
  std::vector<int> edges;
  for (int dy = firstRow; dy <= lastRow; ++dy) {
    const int e = leftmostInk(dy, (kX1 - 4) * S, (kX2 + 4) * S);
    if (e >= 0) edges.push_back(e);
  }
  ASSERT_GT(edges.size(), 8u) << "the line did not draw";

  // Every row that has ink must have moved from the row above it. Allowing one
  // repeat would let a two-thirds regression pass at S=3.
  int held = 0;
  for (size_t i = 1; i < edges.size(); ++i) {
    if (edges[i] == edges[i - 1]) ++held;
  }
  EXPECT_EQ(held, 0) << held << " of " << edges.size() - 1
                     << " device rows repeat the previous row's leading edge — the diagonal is being drawn from "
                        "logical blocks, not device pixels";
}

// Weight is a separate assertion from smoothness: rasterizing at device
// resolution must not quietly thin or fatten the stroke. lineWidth logical rows
// is lineWidth * RENDER_SCALE device rows.
TEST_F(HiResShapes, SlantedThickLineKeepsItsLogicalWeight) {
  r->drawLine(kX1, kY1, kX2, kY2, kWidth, true);

  // Count inked device rows in a column well inside the run, away from both ends
  // where the shear taper makes the count legitimately smaller.
  const int probe = ((kX1 + kX2) / 2) * S + S / 2;
  int rows = 0;
  for (int dy = (kY2 - 4) * S; dy < (kY1 + kWidth + 4) * S; ++dy) {
    if (inkAtDevice(probe, dy)) ++rows;
  }
  EXPECT_EQ(rows, kWidth * S);
}

// The change is confined to diagonals. An axis-aligned thick line must still be
// the block-replicated logical shape: exact device extents, no half-blocks.
TEST_F(HiResShapes, AxisAlignedThickLineStaysBlockReplicated) {
  constexpr int x = 40, y = 100, len = 30, w = 3;
  r->drawLine(x, y, x + len, y, w, true);

  for (int dy = y * S; dy < (y + w) * S; ++dy) {
    EXPECT_TRUE(inkAtDevice(x * S, dy)) << "row " << dy << " missing the first device column";
    EXPECT_TRUE(inkAtDevice((x + len) * S + S - 1, dy)) << "row " << dy << " missing the last device column";
  }
  // One device row above and below the block must be clear.
  EXPECT_FALSE(inkAtDevice(x * S + S, y * S - 1));
  EXPECT_FALSE(inkAtDevice(x * S + S, (y + w) * S));
}

}  // namespace
