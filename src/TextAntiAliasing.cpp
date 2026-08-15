#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "TextAntiAliasingPass.h"

namespace TextAa {
namespace {

// Band height for the tiled path, matching EpubReaderActivity's tiled
// grayscale. 80 rows of a 100-byte-wide panel is 8 KB of scratch against the
// 48 KB a whole-frame save costs.
constexpr int STRIP_ROWS = 80;

// Tiled path. Renders each plane band-by-band into a small scratch buffer and
// streams it straight to controller RAM, which leaves the BW framebuffer
// untouched — so there is no 48 KB save/restore, and the intact framebuffer is
// itself the clean differential baseline for the next update.
//
// The callback runs once per band per plane rather than once per plane, but
// renderCharImpl culls out-of-band glyphs before decoding them
// (GfxRenderer.cpp, glyphInBand), so the cost stays close to one render per
// plane. That is only true for a callback that draws precomputed text; one
// that re-wraps or re-measures its content pays that work per band.
//
// Returns false when the path is unavailable (no strip support, or the scratch
// would not allocate), leaving the caller to fall back.
bool overlayViaStrips(GfxRenderer& renderer, const DrawFn draw, void* ctx) {
  if (!renderer.supportsStripGrayscale()) return false;

  const int gh = static_cast<int>(renderer.getDisplayHeight());
  const int gwBytes = static_cast<int>(renderer.getDisplayWidthBytes());
  const size_t scratchBytes = static_cast<size_t>(gwBytes) * STRIP_ROWS;
  auto scratch = makeUniqueNoThrow<uint8_t[]>(scratchBytes);
  if (!scratch) {
    LOG_ERR("TAA", "OOM: grayscale strip scratch (%zu bytes)", scratchBytes);
    return false;
  }

  // The strip writes need the panel idle. A no-op unless the caller started an
  // async refresh; blocking panels never do.
  renderer.waitRefreshComplete();

  for (int pass = 0; pass < 2; ++pass) {
    const bool lsbPlane = (pass == 0);
    renderer.setRenderMode(lsbPlane ? GfxRenderer::GRAYSCALE_LSB : GfxRenderer::GRAYSCALE_MSB);
    for (int y = 0; y < gh; y += STRIP_ROWS) {
      const int rows = std::min(STRIP_ROWS, gh - y);
      renderer.beginStripTarget(scratch.get(), y, rows);
      renderer.clearScreen(0x00);
      draw(ctx);
      renderer.endStripTarget();
      renderer.writeGrayscalePlaneStrip(lsbPlane, scratch.get(), y, rows);
    }
  }

  renderer.setRenderMode(GfxRenderer::BW);
  renderer.displayGrayBuffer();
  renderer.cleanupGrayscaleWithFrameBuffer();
  return true;
}

// Whole-frame fallback: the original pipeline, for a controller without strip
// support and for the OOM case above. Costs a 48 KB chunked save of the BW
// frame because the plane passes render over the framebuffer itself.
void overlayViaWholeFrame(GfxRenderer& renderer, const DrawFn draw, void* ctx) {
  if (!renderer.storeBwBuffer()) {
    LOG_ERR("TAA", "Failed to store BW buffer for anti-aliasing");
    return;
  }

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  draw(ctx);
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  draw(ctx);
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.restoreBwBuffer();
}

}  // namespace

void overlay(GfxRenderer& renderer, const GfxRenderer::GrayscaleAaStrength strength, const DrawFn draw, void* ctx) {
  if (draw == nullptr) return;
  renderer.setGrayscaleAaStrength(strength);
  if (overlayViaStrips(renderer, draw, ctx)) return;
  overlayViaWholeFrame(renderer, draw, ctx);
}

}  // namespace TextAa
