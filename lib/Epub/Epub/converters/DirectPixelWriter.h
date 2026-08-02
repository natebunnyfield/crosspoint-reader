#pragma once

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <stdint.h>

#include <cassert>

// Direct framebuffer writer that eliminates per-pixel overhead from the image
// rendering hot path.  Pre-computes orientation transform as linear coefficients
// and caches render-mode state so the inner loop is: one multiply, one add,
// one shift, and one AND per pixel — no branches, no method calls.
//
// Caller is responsible for ensuring (outX, outY) are within screen bounds.
// ImageBlock::render() already validates this before entering the pixel loop,
// and the JPEG/PNG callbacks pre-clamp destination ranges to screen bounds.
struct DirectPixelWriter {
  uint8_t* fb;
  GfxRenderer::RenderMode mode;
  uint16_t displayWidthBytes;  // Runtime framebuffer stride (X4: 100, X3: 99)
  // Active write target: for tiled grayscale, fb is the band scratch, originY is
  // the band's top physical row, and clipRows is the band height. Off-band
  // pixels are dropped. With no strip active these collapse to the full frame
  // (originY 0, clipRows panelHeight) so the clip doubles as a bounds guard.
  int originY;
  int clipRows;

  // Orientation is collapsed into a linear transform:
  //   phyX = phyXBase + x * phyXStepX + y * phyXStepY
  //   phyY = phyYBase + x * phyYStepX + y * phyYStepY
  int phyXBase, phyYBase;
  int phyXStepX, phyYStepX;  // per logical-X step
  int phyXStepY, phyYStepY;  // per logical-Y step

  // Row-precomputed: the Y-dependent portion of the physical coords
  int rowPhyXBase, rowPhyYBase;

  // Supersampling: one logical image pixel covers S x S framebuffer pixels, so
  // images keep exactly the size and shape they have at scale 1 (this change is
  // about text; image resolution is deliberately left alone). S == 1 on device,
  // where every branch below compiles out.
  static constexpr int S = CROSSPOINT_RENDER_SCALE;

  void init(GfxRenderer& renderer) {
    fb = renderer.getWriteTarget();
    originY = renderer.getWriteOriginY();
    clipRows = renderer.getWriteRows();
    mode = renderer.getRenderMode();
    displayWidthBytes = renderer.getDisplayWidthBytes();

    const int phyW = renderer.getDisplayWidth();
    const int phyH = renderer.getDisplayHeight();

    switch (renderer.getOrientation()) {
      case GfxRenderer::Portrait:
        // phyX = y, phyY = (phyH-1) - x
        phyXBase = 0;
        phyYBase = phyH - 1;
        phyXStepX = 0;
        phyYStepX = -1;
        phyXStepY = 1;
        phyYStepY = 0;
        break;
      case GfxRenderer::LandscapeClockwise:
        // phyX = (phyW-1) - x, phyY = (phyH-1) - y
        phyXBase = phyW - 1;
        phyYBase = phyH - 1;
        phyXStepX = -1;
        phyYStepX = 0;
        phyXStepY = 0;
        phyYStepY = -1;
        break;
      case GfxRenderer::PortraitInverted:
        // phyX = (phyW-1) - y, phyY = x
        phyXBase = phyW - 1;
        phyYBase = 0;
        phyXStepX = 0;
        phyYStepX = 1;
        phyXStepY = -1;
        phyYStepY = 0;
        break;
      case GfxRenderer::LandscapeCounterClockwise:
        // phyX = x, phyY = y
        phyXBase = 0;
        phyYBase = 0;
        phyXStepX = 1;
        phyYStepX = 0;
        phyXStepY = 0;
        phyYStepY = 1;
        break;
      default:
        // Fallback to LandscapeCounterClockwise (identity transform)
        phyXBase = 0;
        phyYBase = 0;
        phyXStepX = 1;
        phyYStepX = 0;
        phyXStepY = 0;
        phyYStepY = 1;
        break;
    }
  }

  // Call once per row before the column loop.
  // Pre-computes the Y-dependent portion so writePixel() only needs the X part.
  inline void beginRow(int logicalY) {
    rowPhyXBase = phyXBase + logicalY * S * phyXStepY;
    rowPhyYBase = phyYBase + logicalY * S * phyYStepY;
  }

  // For the current row (set via beginRow), narrow [colStart, colEnd) to the
  // columns whose pixels fall inside the active strip band. writePixel() would
  // clip the rest anyway, but on a strip pass that is most of a full-page image
  // (only ~one strip-height worth of columns survive in portrait); skipping them
  // here avoids the per-pixel unpack+transform entirely. For full-frame passes
  // (clipRows == panel height) the range is unchanged. xBase is the logical X of
  // column 0; the band test mirrors writePixel(): 0 <= phyY - originY < clipRows.
  inline void bandColRange(int xBase, int width, int& colStart, int& colEnd) const {
    // init() only ever sets phyYStepX to 0, +1, or -1; the +1/-1 solve below
    // relies on that.
    assert(phyYStepX == 0 || phyYStepX == 1 || phyYStepX == -1);
    colStart = 0;
    colEnd = width;
    if (phyYStepX == 0) {
#if CROSSPOINT_RENDER_SCALE > 1
      // At S > 1 the row occupies S framebuffer rows (phyYStepY is +/-1 exactly
      // when phyYStepX is 0), so keep it if ANY of them lands in the band.
      const int syA = rowPhyYBase - originY;
      const int syB = syA + (S - 1) * phyYStepY;
      const int syLo = syA < syB ? syA : syB;
      const int syHi = syA > syB ? syA : syB;
      if (syHi < 0 || syLo >= clipRows) colEnd = 0;
      return;
#else
      // phyY is constant across the row: the whole row is in-band or out.
      const int sy = rowPhyYBase - originY;
      if (static_cast<unsigned>(sy) >= static_cast<unsigned>(clipRows)) colEnd = 0;
      return;
#endif
    }
    // phyY = rowPhyYBase + deviceX * phyYStepX (phyYStepX is +1 or -1), where
    // deviceX = (xBase + col) * S + i, i in [0, S).
    // Solve originY <= phyY <= originY + clipRows - 1 for deviceX, then widen by
    // one logical column each way; writePixel() clips whatever survives.
    const int loY = originY;
    const int hiY = originY + clipRows - 1;
    int xLo, xHi;
    if (phyYStepX > 0) {
      xLo = loY - rowPhyYBase;
      xHi = hiY - rowPhyYBase;
    } else {
      xLo = rowPhyYBase - hiY;
      xHi = rowPhyYBase - loY;
    }
#if CROSSPOINT_RENDER_SCALE > 1
    // Floor-divide device X back to logical columns, padding by one.
    xLo = (xLo >= 0 ? xLo : xLo - (S - 1)) / S - 1;
    xHi = (xHi >= 0 ? xHi : xHi - (S - 1)) / S + 1;
#endif
    const int cs = xLo - xBase;
    const int ce = xHi - xBase + 1;  // exclusive
    if (cs > colStart) colStart = cs;
    if (ce < colEnd) colEnd = ce;
    if (colStart < 0) colStart = 0;
    if (colEnd > width) colEnd = width;
    if (colStart > colEnd) colStart = colEnd;
  }

  // Write a single 2-bit dithered pixel value to the framebuffer.
  // Must be called after beginRow() for the current row.
  // No bounds checking — caller guarantees coordinates are valid.
  inline void writePixel(int logicalX, uint8_t pixelValue) const {
    // Determine whether to draw based on render mode
    bool draw;
    bool state;
    switch (mode) {
      case GfxRenderer::BW:
        draw = (pixelValue < 3);
        state = true;
        break;
      case GfxRenderer::GRAYSCALE_MSB:
        draw = (pixelValue == 1 || pixelValue == 2);
        state = false;
        break;
      case GfxRenderer::GRAYSCALE_LSB:
        draw = (pixelValue == 1);
        state = false;
        break;
      default:
        return;
    }

    if (!draw) return;

#if CROSSPOINT_RENDER_SCALE > 1
    // One logical pixel -> an S x S device block, so the image keeps its scale-1
    // size and shape. byteIndex is uint32_t here: at S = 2 on X3 the largest
    // index is 1055 * 198 + 197 = 209,087, which does NOT fit the uint16_t the
    // scale-1 path uses (that one is safe because 479 * 100 + 99 = 48,-odd).
    const int bx = logicalX * S;
    for (int j = 0; j < S; ++j) {
      const int rx = rowPhyXBase + j * phyXStepY;
      const int ry = rowPhyYBase + j * phyYStepY;
      for (int i = 0; i < S; ++i) {
        const int phyX = rx + (bx + i) * phyXStepX;
        const int phyY = ry + (bx + i) * phyYStepX;
        const int sy = phyY - originY;
        if (static_cast<unsigned>(sy) >= static_cast<unsigned>(clipRows)) continue;
        const uint32_t byteIndex = static_cast<uint32_t>(sy) * displayWidthBytes + (phyX >> 3);
        const uint8_t bitMask = 1 << (7 - (phyX & 7));
        if (state) {
          fb[byteIndex] &= ~bitMask;  // Clear bit (draw black)
        } else {
          fb[byteIndex] |= bitMask;  // Set bit (draw white)
        }
      }
    }
#else
    const int phyX = rowPhyXBase + logicalX * phyXStepX;
    const int phyY = rowPhyYBase + logicalX * phyYStepX;

    // Band-local row. The unsigned compare drops both off-band pixels (strip
    // mode) and any out-of-frame row (full-frame mode) in one branch.
    const int sy = phyY - originY;
    if (static_cast<unsigned>(sy) >= static_cast<unsigned>(clipRows)) return;

    const uint16_t byteIndex = static_cast<uint16_t>(sy * displayWidthBytes + (phyX >> 3));
    const uint8_t bitMask = 1 << (7 - (phyX & 7));

    if (state) {
      fb[byteIndex] &= ~bitMask;  // Clear bit (draw black)
    } else {
      fb[byteIndex] |= bitMask;  // Set bit (draw white)
    }
#endif
  }
};

// Direct cache writer that eliminates per-pixel overhead from PixelCache::setPixel().
// Pre-computes row pointer so the inner loop is just byte index + bit manipulation.
//
// The cache buffer is a small streaming band (e.g. 16 rows), not the full image,
// so a band-relative row/column that lands outside it would corrupt adjacent
// heap. This writer therefore bounds-checks every access: beginRow() invalidates
// the row when it falls outside the band, and writePixel() drops out-of-range
// columns. This path only runs during the single decode that populates the
// cache, never on the screen render hot path, so the checks are cheap.
struct DirectCacheWriter {
  uint8_t* buffer;
  int bytesPerRow;
  int bandRows;
  int originX;
  uint8_t* rowPtr;  // Pre-computed for current row; nullptr if row is out of band

  void init(uint8_t* cacheBuffer, int cacheBytesPerRow, int cacheBandRows, int cacheOriginX) {
    buffer = cacheBuffer;
    bytesPerRow = cacheBytesPerRow;
    bandRows = cacheBandRows;
    originX = cacheOriginX;
    rowPtr = nullptr;
  }

  // Call once per row before the column loop. Drops rows outside the band.
  inline void beginRow(int screenY, int cacheOriginY) {
    const int localRow = screenY - cacheOriginY;
    rowPtr = (static_cast<unsigned>(localRow) < static_cast<unsigned>(bandRows))
                 ? buffer + (size_t)localRow * bytesPerRow
                 : nullptr;
  }

  // Write a 2-bit pixel value. Drops the write if the row is out of band or the
  // column is out of range.
  inline void writePixel(int screenX, uint8_t value) const {
    if (!rowPtr) return;
    const int localX = screenX - originX;
    const int byteIdx = localX >> 2;  // localX / 4
    if (static_cast<unsigned>(byteIdx) >= static_cast<unsigned>(bytesPerRow)) return;
    const int bitShift = 6 - (localX & 3) * 2;  // MSB first: pixel 0 at bits 6-7
    rowPtr[byteIdx] = (rowPtr[byteIdx] & ~(0x03 << bitShift)) | ((value & 0x03) << bitShift);
  }
};
