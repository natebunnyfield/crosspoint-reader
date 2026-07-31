#include "LyraSixTheme.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

// Internal constants
namespace {
constexpr int hPaddingInSelection = 8;
constexpr int cornerRadius = 6;
// Text block below a cover: 5px gap under the cover, then up to two lines.
//
// Two lines, not Lyra Extended's three, because in a grid the row below is real
// estate rather than empty screen. The arithmetic, with SMALL_FONT_ID
// (notosans_8, advanceY = 23, see lib/EpdFont/builtinFonts/notosans_8_regular.h):
//
//   8 (top strip) + 226 (cover) + 8 + 5 (gap) + 2 * 23 (text) = 293
//
// which fits the 300px row with 7px to spare. Three lines would need 316 and
// would paint the first row's titles over the second row's covers.
constexpr int titleMaxLinesCap = 2;
constexpr int titleTopGap = 5;

// Titles are set in GTAlpinaCond when it is installed on the SD card, falling
// back to the built-in Noto Sans. GTAlpinaCond is a condensed face, which is what
// makes it worth the SD read here: in a 3-across grid the tile is only ~146px
// wide, so a condensed face fits noticeably more of a title before truncation.
//
// The smallest size it ships is 12pt against Noto Sans 8pt, so the line height
// GROWS when it resolves. The line budget above is therefore no longer a constant
// — see titleLinesThatFit(), which derives it from the measured line height so a
// tall face drops to one line rather than painting into the row below.
constexpr const char* TITLE_SD_FAMILY = "GTAlpinaCond";

// Slot 0 (SMALL). findClosestReaderSize maps the ordinal slot onto whatever sizes
// the family actually ships, so this asks for the smallest GTAlpinaCond has.
constexpr uint8_t TITLE_SD_SIZE_SLOT = 0;

int titleLinesThatFit(const int lineHeight, const int rowHeight, const int coverHeight) {
  if (lineHeight <= 0) return 1;
  const int available = rowHeight - hPaddingInSelection - coverHeight - hPaddingInSelection - titleTopGap;
  return std::clamp(available / lineHeight, 1, titleMaxLinesCap);
}
}  // namespace

void LyraSixTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                       const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                       bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  (void)bufferRestored;

  constexpr int columns = LyraSixMetrics::values.homeCoverColumns;
  constexpr int rows = LyraSixMetrics::values.homeCoverRows;
  constexpr int coverHeight = LyraSixMetrics::values.homeCoverHeight;
  // Height of ONE grid row. rect.height is rowHeight * (rows actually used), so
  // it must not be divided here — with fewer than columns+1 books HomeActivity
  // passes a single row.
  constexpr int rowHeight = LyraSixMetrics::values.homeCoverTileHeight;

  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  const int tileWidth = (rect.width - 2 * LyraSixMetrics::values.contentSidePadding) / columns;
  const int coverWidth = tileWidth - 2 * hPaddingInSelection;
  const int visible =
      std::min({static_cast<int>(recentBooks.size()), LyraSixMetrics::values.homeRecentBooksCount, columns * rows});

  // Covers are the expensive part (an SD read plus a scaled blit each), so they
  // are drawn once and then snapshotted; every later render restores the
  // snapshot and only repaints the selection box and the titles below.
  if (!coverRendered) {
    for (int i = 0; i < visible; i++) {
      const int tileX = LyraSixMetrics::values.contentSidePadding + tileWidth * (i % columns);
      const int tileY = rect.y + rowHeight * (i / columns);

      bool hasCover = !recentBooks[i].coverBmpPath.empty();
      if (hasCover) {
        const std::string coverBmpPath =
            UITheme::getCoverThumbPath(recentBooks[i].coverBmpPath, LyraSixMetrics::values.homeCoverHeight);
        HalFile file;
        if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            const float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
            const float tileRatio = static_cast<float>(coverWidth) / static_cast<float>(coverHeight);
            // Clamped at 0: a cover narrower than the tile ratio yields a
            // negative crop, and GfxRenderer::drawBitmap then starts its row
            // loop at a negative bmpX and indexes outputRow[bmpX / 4] out of
            // bounds (GfxRenderer.cpp:1249). 0 means "no crop" — the thumbnail
            // is scaled to fit and left-aligned in the tile instead.
            const float cropX = std::max(0.0f, 1.0f - (tileRatio / ratio));
            renderer.drawBitmap(bitmap, tileX + hPaddingInSelection, tileY + hPaddingInSelection, coverWidth,
                                coverHeight, cropX);
          } else {
            hasCover = false;
          }
          file.close();
        } else {
          hasCover = false;
        }
      }

      // Draw either way
      renderer.drawRect(tileX + hPaddingInSelection, tileY + hPaddingInSelection, coverWidth, coverHeight, true);

      if (!hasCover) {
        // Render empty cover
        renderer.fillRect(tileX + hPaddingInSelection, tileY + hPaddingInSelection + (coverHeight / 3), coverWidth,
                          2 * coverHeight / 3, true);
        renderer.drawIcon(CoverIcon, tileX + hPaddingInSelection + 24, tileY + hPaddingInSelection + 24, 32);
      }
    }

    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;  // Only consider it rendered if we successfully stored the buffer
  }

  // GTAlpinaCond if the card has it, else the built-in Noto Sans. loadForDisplay
  // returns 0 when the family is not installed, which is the fallback signal —
  // same shape as CalendarSleepScreen's preferred-family logic. It is size-aware
  // and reuses an already-resident family, so calling it per render (Home
  // re-renders on every selector move) costs an SD read only on the first pass.
  const int sdTitleFontId = sdFontSystem.loadForDisplay(TITLE_SD_FAMILY, TITLE_SD_SIZE_SLOT, renderer);
  const int titleFontId = sdTitleFontId != 0 ? sdTitleFontId : SMALL_FONT_ID;

  // An SD title font arrives here cold: loadForDisplay() runs outside any
  // PrewarmScope, so without an explicit prewarm every wrappedText() measure
  // and every drawText() glyph goes through SdCardFont's on-demand overflow
  // path — and a screenful of titles is a working set larger than that cache,
  // so the same letters were re-read from the card over and over within one
  // render (hundreds of SD loads per frame, measured). One batched prewarm
  // makes the whole title set resident; SdCardFont's subset check then makes
  // the repeat prewarm on every selector-move re-render free (zero SD reads).
  if (sdTitleFontId != 0) {
    if (auto* fcm = renderer.getFontCacheManager()) {
      size_t prewarmLen = 0;
      for (int i = 0; i < visible; i++) prewarmLen += recentBooks[i].title.size() + 1;
      std::string prewarmText;
      prewarmText.reserve(prewarmLen + 3);
      for (int i = 0; i < visible; i++) {
        prewarmText += recentBooks[i].title;
        prewarmText += ' ';
      }
      prewarmText += "\xe2\x80\xa6";  // U+2026: wrappedText/truncatedText append it on truncation
      fcm->prewarmCache(titleFontId, prewarmText.c_str(), 0x01);  // titles draw REGULAR only
    }
  }

  const int titleLineHeight = renderer.getLineHeight(titleFontId);
  const int titleMaxLines = titleLinesThatFit(titleLineHeight, rowHeight, coverHeight);

  for (int i = 0; i < visible; i++) {
    const int tileX = LyraSixMetrics::values.contentSidePadding + tileWidth * (i % columns);
    const int tileY = rect.y + rowHeight * (i / columns);

    auto titleLines = renderer.wrappedText(titleFontId, recentBooks[i].title.c_str(), coverWidth, titleMaxLines);
    // Same padding below the text as above it (titleTopGap + hPaddingInSelection)
    const int titleBoxHeight =
        static_cast<int>(titleLines.size()) * titleLineHeight + hPaddingInSelection + titleTopGap;

    if (selectorIndex == i) {
      // Draw selection box
      renderer.fillRoundedRect(tileX, tileY, tileWidth, hPaddingInSelection, cornerRadius, true, true, false, false,
                               Color::LightGray);
      renderer.fillRectDither(tileX, tileY + hPaddingInSelection, hPaddingInSelection, coverHeight, Color::LightGray);
      renderer.fillRectDither(tileX + tileWidth - hPaddingInSelection, tileY + hPaddingInSelection,
                              hPaddingInSelection, coverHeight, Color::LightGray);
      renderer.fillRoundedRect(tileX, tileY + coverHeight + hPaddingInSelection, tileWidth, titleBoxHeight,
                               cornerRadius, false, false, true, true, Color::LightGray);
    }

    int currentY = tileY + coverHeight + hPaddingInSelection + titleTopGap;
    for (const auto& line : titleLines) {
      renderer.drawText(titleFontId, tileX + hPaddingInSelection, currentY, line.c_str(), true);
      currentY += titleLineHeight;
    }
  }
}
