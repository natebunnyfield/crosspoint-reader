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
// Why three outcomes rather than a bool: the caller must be able to tell "this
// panel cannot do strips" from "the heap would not give me the scratch". They
// used to be the same `false`, and the fallback for both was the whole-frame
// path -- which needs SIX blocks of exactly the size that just failed. See
// B-052.
enum class StripResult { DONE, UNSUPPORTED, OUT_OF_MEMORY };

StripResult overlayViaStrips(GfxRenderer& renderer, const DrawFn draw, void* ctx) {
  if (!renderer.supportsStripGrayscale()) return StripResult::UNSUPPORTED;

  const int gh = static_cast<int>(renderer.getDisplayHeight());
  const int gwBytes = static_cast<int>(renderer.getDisplayWidthBytes());
  const size_t scratchBytes = static_cast<size_t>(gwBytes) * STRIP_ROWS;
  auto scratch = makeUniqueNoThrow<uint8_t[]>(scratchBytes);
  if (!scratch) {
    LOG_ERR("TAA", "OOM: grayscale strip scratch (%zu bytes)", scratchBytes);
    return StripResult::OUT_OF_MEMORY;
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
  return StripResult::DONE;
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
  const StripResult strips = overlayViaStrips(renderer, draw, ctx);
  if (strips == StripResult::DONE) return;

  // THE WHOLE-FRAME FALLBACK IS ONLY FOR A PANEL THAT CANNOT DO STRIPS.
  //
  // It costs a 48 KB chunked save of the BW frame, taken as SIX blocks of
  // gwBytes * STRIP_ROWS -- 8,000 bytes each on an 800x480 panel, which is
  // EXACTLY the size of the strip scratch that just failed. So escalating from
  // a refused 8 KB allocation to six of them cannot succeed; it can only
  // fragment what is left and take longer to give the same answer. B-052 is
  // that sequence on an X3: "Failed to allocate BW buffer chunk 4 (8000
  // bytes)", then an abort a moment later on some other allocation.
  //
  // Out of memory therefore means SKIP the anti-aliasing for this frame. The
  // page still renders, in plain 1-bit, which is what the fallback path would
  // have produced anyway after failing.
  if (strips == StripResult::OUT_OF_MEMORY) {
    LOG_ERR("TAA", "Skipping anti-aliasing: no memory for the strip scratch, and the whole-frame path needs six of it");
    return;
  }
  overlayViaWholeFrame(renderer, draw, ctx);
}

}  // namespace TextAa
