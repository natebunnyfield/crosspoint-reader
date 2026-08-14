// Dark mode counter-inversion — GfxRenderer::preserveImagePolarity.
//
// Whole-screen dark mode is applied by the display driver on the way to the
// panel (HalDisplay::setInverted -> FreeInkDisplay::invertBytes). That is
// polarity-blind: a photograph or a book cover comes out as a negative. The fix
// is to flip the image's own rectangle a second time in the framebuffer, so the
// two inversions cancel and the picture keeps its original polarity inside an
// otherwise inverted page.
//
// This is bit-masking against a rotated, byte-packed framebuffer, which is
// exactly where an off-by-one hides: a wrong head or tail mask flips seven
// neighbouring pixels and reads on the panel as a hairline of the wrong colour
// along one edge of every image. Nothing else in the firmware would notice.
//
// The invariants under test:
//
//   1. It does nothing at all unless the display is inverted.
//   2. It flips EXACTLY width*height pixels — not the byte-aligned rectangle
//      containing them.
//   3. The flipped set sits exactly where the logical rectangle maps to after
//      the portrait rotation.
//   4. Applying it twice is the identity (it is an XOR).
//   5. A rectangle running off the screen flips only the on-screen part.

#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <cstdint>

HalDisplay display;

namespace {

constexpr int S = CROSSPOINT_RENDER_SCALE;

class ImagePolarity : public ::testing::Test {
 protected:
  static GfxRenderer& renderer() {
    static GfxRenderer r(display);
    static bool begun = [] {
      r.begin();
      return true;
    }();
    (void)begun;
    return r;
  }

  void SetUp() override {
    r = &renderer();
    display.setInverted(false);
    r->clearScreen();  // 0xFF everywhere: every pixel white (bit set)
  }

  void TearDown() override { display.setInverted(false); }

  // Physical framebuffer geometry, straight off the panel stub.
  static const uint8_t* fb() { return display.getFrameBuffer(); }
  static int stride() { return HalDisplay::DISPLAY_WIDTH_BYTES; }
  static int rows() { return HalDisplay::DISPLAY_HEIGHT; }
  static int cols() { return HalDisplay::DISPLAY_WIDTH; }

  static bool bitSet(const int phyX, const int phyY) {
    return (fb()[phyY * stride() + (phyX >> 3)] >> (7 - (phyX & 7))) & 1;
  }

  // Every physical pixel that is NOT white, i.e. every one the flip touched
  // (the screen starts all-white).
  struct Cleared {
    int count = 0;
    int minX = INT32_MAX, maxX = -1, minY = INT32_MAX, maxY = -1;
  };
  static Cleared cleared() {
    Cleared c;
    for (int y = 0; y < rows(); y++) {
      for (int x = 0; x < cols(); x++) {
        if (bitSet(x, y)) continue;
        c.count++;
        if (x < c.minX) c.minX = x;
        if (x > c.maxX) c.maxX = x;
        if (y < c.minY) c.minY = y;
        if (y > c.maxY) c.maxY = y;
      }
    }
    return c;
  }

  GfxRenderer* r = nullptr;
};

// Invariant 1. Light mode must not pay for dark mode.
TEST_F(ImagePolarity, DoesNothingWhileNotInverted) {
  r->preserveImagePolarity(11, 7, 33, 21);
  EXPECT_EQ(cleared().count, 0);
}

// Invariants 2 and 3, with a rectangle chosen so that neither edge lands on a
// byte boundary in PANEL memory. Portrait maps logical (x, y) to physical
// (y, panelHeight - 1 - x) (GfxRenderer.cpp rotateCoordinates), so the logical
// Y extent is what becomes the physical column range the masks operate on:
// y = 7..27 starts 7 bits into a byte and ends 4 bits into another.
TEST_F(ImagePolarity, FlipsExactlyTheRectangleAcrossByteBoundaries) {
  constexpr int x = 13, y = 7, w = 29, h = 21;
  display.setInverted(true);
  r->preserveImagePolarity(x, y, w, h);

  const Cleared c = cleared();
  EXPECT_EQ(c.count, w * S * h * S) << "flipped a byte-aligned rectangle, not the requested one";

  // Rotate the logical corners the same way the renderer does and compare.
  const int expMinX = y * S;
  const int expMaxX = (y + h) * S - 1;
  const int expMaxY = rows() - 1 - x * S;
  const int expMinY = rows() - 1 - ((x + w) * S - 1);
  EXPECT_EQ(c.minX, expMinX);
  EXPECT_EQ(c.maxX, expMaxX);
  EXPECT_EQ(c.minY, expMinY);
  EXPECT_EQ(c.maxY, expMaxY);
}

// A rectangle whose physical column range sits entirely inside ONE byte takes
// the byteStart == byteEnd branch, which is the branch a naive head/tail
// implementation gets wrong.
TEST_F(ImagePolarity, FlipsARectangleContainedInASingleByte) {
  constexpr int x = 40, y = 9, w = 6, h = 3;  // physical columns 9..11 at S=1
  display.setInverted(true);
  r->preserveImagePolarity(x, y, w, h);
  EXPECT_EQ(cleared().count, w * S * h * S);
}

// Invariant 4. XOR is its own inverse, which is what makes the two inversions
// cancel on the panel in the first place.
TEST_F(ImagePolarity, IsItsOwnInverse) {
  display.setInverted(true);
  r->preserveImagePolarity(21, 17, 40, 40);
  r->preserveImagePolarity(21, 17, 40, 40);
  EXPECT_EQ(cleared().count, 0);
}

// Invariant 5. An image clipped by the screen edge must not wrap into the next
// row or walk off the buffer.
TEST_F(ImagePolarity, ClipsARectangleRunningOffTheScreen) {
  const int sw = r->getScreenWidth();
  const int sh = r->getScreenHeight();
  const int x = sw - 10;
  const int y = sh - 6;
  display.setInverted(true);
  r->preserveImagePolarity(x, y, 200, 200);
  EXPECT_EQ(cleared().count, 10 * S * 6 * S);
}

// A degenerate or fully off-screen rectangle is a no-op, not a crash.
TEST_F(ImagePolarity, IgnoresEmptyAndOffscreenRectangles) {
  display.setInverted(true);
  r->preserveImagePolarity(10, 10, 0, 20);
  r->preserveImagePolarity(10, 10, 20, -5);
  r->preserveImagePolarity(r->getScreenWidth() + 50, 10, 20, 20);
  r->preserveImagePolarity(-100, -100, 40, 40);
  EXPECT_EQ(cleared().count, 0);
}

// The grayscale planes are never sent while inverted (FreeInkDisplay.cpp:779,
// :859), so a flip during a grayscale pass would corrupt a plane for no gain.
TEST_F(ImagePolarity, DoesNothingOutsideTheBwPass) {
  display.setInverted(true);
  r->setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  r->preserveImagePolarity(13, 7, 29, 21);
  r->setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  r->preserveImagePolarity(13, 7, 29, 21);
  r->setRenderMode(GfxRenderer::BW);
  EXPECT_EQ(cleared().count, 0);
}

}  // namespace
