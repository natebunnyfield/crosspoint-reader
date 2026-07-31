#pragma once

#include "components/themes/lyra/Lyra3CoversTheme.h"

class GfxRenderer;

// "Lyra Six" — Lyra Extended with six covers instead of three, and the menu
// rows moved to a second page.
//
// The metrics are a COPY of Lyra Extended's with three fields changed, so every
// other value (fonts, paddings, keyboard, popups, list rows...) tracks Lyra
// Extended automatically and nothing is duplicated. homeCoverTileHeight stays
// at Lyra Extended's 300 because that is now the height of ONE grid row;
// homeCoverRows = 2 is what makes the grid 600 tall.
//
// homeCoverHeight is deliberately unchanged at Lyra's 226: the cover thumbnail
// cache is keyed by that height (UITheme::getCoverThumbPath -> thumb_226.bmp),
// so this theme reuses the thumbnails Lyra and Lyra Extended already generated
// instead of forcing a re-render of every cover at a new size.
namespace LyraSixMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = Lyra3CoversMetrics::values;
  v.homeRecentBooksCount = 6;
  v.homeCoverRows = 2;
  v.homeCoverColumns = 3;
  v.homeMenuOnSecondPage = true;
  // 312, not Lyra Extended's 300, so TWO title lines still fit once titles are set
  // in GTAlpinaCond 12pt (the smallest size that family ships) instead of the
  // built-in Noto Sans 8pt. Per row:
  //   8 (top strip) + 226 (cover) + 8 + 5 (gap) + 2 * lineHeight
  // At 300 the leftover was 53px, under 2 lines of a ~28-31px face, so
  // titleLinesThatFit() correctly dropped to a single line and titles truncated
  // much sooner. 312 leaves 65px, enough for two.
  //
  // Still fits both panels with room to spare: the grid band runs from y=56 to
  // (height - 40) for the button hints, so 2 * 312 = 624 against 696 usable on the
  // X3 (792 tall) and 704 on the X4 (800 tall).
  v.homeCoverTileHeight = 312;
  return v;
}();
}  // namespace LyraSixMetrics

class LyraSixTheme : public Lyra3CoversTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
};
