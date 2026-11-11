// glyphBitmap::draw against a per-pixel reference. From upstream 04ca1d53c,
// adapted to this fork's interface: the painted levels are passed in (every one
// of the 16 masks is exercised, not upstream's three fixed planes), and the
// clip cases go through the panel and the strip band only, because this fork
// has no logical clip rectangle (upstream's comes with the held 652ae0d83).
#include <gtest/gtest.h>

#include <array>
#include <utility>
#include <vector>

#include "lib/GfxRenderer/GlyphBitmap.h"

namespace {
constexpr int PANEL_WIDTH = 40;
constexpr int PANEL_HEIGHT = 32;
constexpr int STRIDE = PANEL_WIDTH / 8;

// Glyph-axis steps in logical space: unrotated text, Rotated90CW (glyph x runs
// up, glyph y right) and Rotated90CCW (glyph x runs down, glyph y left) --
// exactly the three frames renderCharImpl builds.
constexpr std::array<std::array<int, 4>, 3> AXES = {{{1, 0, 0, 1}, {0, -1, 1, 0}, {0, 1, -1, 0}}};

std::pair<int, int> physical(int orientation, int x, int y) {
  switch (orientation) {
    case 0:
      return {y, PANEL_HEIGHT - 1 - x};
    case 1:
      return {PANEL_WIDTH - 1 - x, PANEL_HEIGHT - 1 - y};
    case 2:
      return {PANEL_WIDTH - 1 - y, x};
    default:
      return {x, y};
  }
}

void compare(int orientation, int rotation, bool twoBit, uint8_t levels, bool clearBits, int width, int height, int x,
             int y, int originY, int rows) {
  SCOPED_TRACE(::testing::Message() << orientation << ',' << rotation << ',' << twoBit
                                    << ",levels=" << static_cast<int>(levels) << ',' << clearBits << " size=" << width
                                    << 'x' << height << " at=" << x << ',' << y << " band=" << originY << ',' << rows);
  const auto [dxX, dxY, dyX, dyY] = AXES[rotation];
  std::vector<uint8_t> bitmap((width * height * (twoBit ? 2 : 1) + 7) / 8);
  for (size_t i = 0; i < bitmap.size(); ++i) bitmap[i] = static_cast<uint8_t>(i * 73 + 0x1b);
  // Nonuniform background and guards verify transparent pixels and all off-band bytes.
  std::vector<uint8_t> expected(rows * STRIDE + 32);
  for (size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<uint8_t>(i * 53 + 0xa5);
  auto actual = expected;
  for (int gy = 0; gy < height; ++gy) {
    for (int gx = 0; gx < width; ++gx) {
      const int lx = x + gx * dxX + gy * dyX;
      const int ly = y + gx * dxY + gy * dyY;
      const auto [px, py] = physical(orientation, lx, ly);
      if (px < 0 || px >= PANEL_WIDTH || py < originY || py >= originY + rows) continue;
      const int source = gy * width + gx;
      bool draw = false;
      if (twoBit) {
        const int raw = (bitmap[source / 4] >> ((3 - source % 4) * 2)) & 3;
        draw = (levels >> raw) & 1;
      } else {
        draw = (bitmap[source / 8] >> (7 - source % 8)) & 1;
      }
      if (!draw) continue;
      auto& byte = expected[16 + (py - originY) * STRIDE + px / 8];
      if (clearBits)
        byte &= static_cast<uint8_t>(~(0x80 >> (px % 8)));
      else
        byte |= 0x80 >> (px % 8);
    }
  }

  const auto [px, py] = physical(orientation, x, y);
  const auto [xx, xy] = physical(orientation, x + dxX, y + dxY);
  const auto [yx, yy] = physical(orientation, x + dyX, y + dyY);
  const glyphBitmap::Target target{
      actual.data() + 16, PANEL_WIDTH, STRIDE, originY, rows, {px, py, xx - px, xy - py, yx - px, yy - py}};
  glyphBitmap::draw(bitmap.data(), width, height, twoBit, levels, clearBits, target,
                    glyphBitmap::Clip{0, 0, width, height});
  EXPECT_EQ(expected, actual);
}
}  // namespace

TEST(GlyphBitmap, RawLevelsReversesTheSwappedNumbering) {
  // Swapped level n is raw value 3 - n (GlyphAaPlanes.h).
  for (int swapped = 0; swapped < 16; ++swapped) {
    uint8_t want = 0;
    for (int n = 0; n < 4; ++n)
      if (swapped & (1 << n)) want |= static_cast<uint8_t>(1 << (3 - n));
    EXPECT_EQ(want, glyphBitmap::rawLevels(static_cast<uint8_t>(swapped))) << swapped;
  }
  // Light mode's BW ink (swapped levels 0,1,2 = every non-white level) is
  // upstream's hard-coded BW mask, raw 1,2,3.
  EXPECT_EQ(0x0e, glyphBitmap::rawLevels(0x07));
}

TEST(GlyphBitmap, MatchesPerPixelReferenceAcrossOrientationsRotationsLevelsAndBands) {
  for (int orientation = 0; orientation < 4; ++orientation) {
    for (int rotation = 0; rotation < 3; ++rotation) {
      for (bool twoBit : {false, true}) {
        for (int levels = 0; levels < (twoBit ? 16 : 1); ++levels) {
          for (bool clearBits : {false, true}) {
            for (int width : {1, 3, 7, 16, 31}) {
              for (const auto [x, y] : {std::pair{-5, -3}, std::pair{0, 0}, std::pair{9, 11}, std::pair{29, 31}}) {
                for (const auto [origin, rows] :
                     {std::pair{0, 32}, std::pair{0, 7}, std::pair{7, 11}, std::pair{28, 4}}) {
                  compare(orientation, rotation, twoBit, static_cast<uint8_t>(levels), clearBits, width, 13, x, y,
                          origin, rows);
                }
              }
            }
          }
        }
      }
    }
  }
}

TEST(GlyphBitmap, EmptyAndFullyClippedGlyphsDoNotWrite) {
  for (int orientation = 0; orientation < 4; ++orientation) {
    for (int rotation = 0; rotation < 3; ++rotation) {
      compare(orientation, rotation, true, 0x0e, true, 0, 0, 0, 0, 0, 32);
      compare(orientation, rotation, true, 0x0e, true, 11, 9, 100, 100, 0, 32);
      compare(orientation, rotation, true, 0x0e, true, 11, 9, -100, -100, 0, 32);
      compare(orientation, rotation, true, 0x00, true, 11, 9, 4, 4, 0, 32);
    }
  }
}
