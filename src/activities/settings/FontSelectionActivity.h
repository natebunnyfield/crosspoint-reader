#pragma once

#include <SdCardFontRegistry.h>

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

class FontSelectionActivity final : public Activity {
 public:
  explicit FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 const SdCardFontRegistry* registry);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Applies the highlighted font immediately and stays on screen. There is no
  // commit step: the user leaves with Back and keeps whatever is applied.
  void applySelectedFont();
  // Long hold on Confirm: switch the highlighted family off, or back on.
  // Every refusal (the last active family, an unstorable name, a full spec)
  // is decided by src/FontActivation.h and reported through notice_.
  void toggleSelectedFontActive();
  // Step the reader font size by `delta` through the point sizes the active
  // family actually ships, clamped at both ends, and reload the SD font at the
  // new size. Bound to the side buttons so a font can be judged at the size it
  // will actually be read at.
  void changeFontSize(int delta);
  // Point size the stored SETTINGS.fontPointSize actually resolves to for the
  // font being previewed, or 0 if it cannot be determined. Families only ship
  // the sizes they ship, so the stored value snaps per-family — the stored
  // number alone is not an accurate readout.
  uint8_t resolvedPointSize() const;
  // Typefaces shown per page: kVisibleFontRows (three), fixed.
  //
  // The list rect and the navigation stride MUST derive from the same number.
  // drawList() computes its own pageItems from rect.height / rowHeight, so if
  // the two ever disagree the highlight desynchronises from the page and
  // ButtonNavigator steps past entries the screen never drew. Both sides ask the
  // theme (getNumberOfItemsPerPage / the same row height) against the same
  // reserved preview height, which is one source rather than two.

  int getFontIdForPreview(int index) const;
  void renderPreviewPane(int top, int height, int fontId, const char* fontName) const;
  // Draws only the preview pane's specimen text (no label, no prewarm). Split
  // out of renderPreviewPane so the grayscale anti-aliasing passes can
  // re-render exactly the glyphs the BW pass drew — it is the content callback
  // handed to ReaderUtils::renderAntiAliased() from render().
  void renderPreviewSpecimen(int top, int height, int fontId) const;
  // The colophon for the face the pane is previewing, wrapped to `width` and
  // capped at kColophonLines. Empty for built-ins and for families the display
  // table does not carry. Both pane passes call this and size their bottom
  // reserve from it, so the BW pass and the grayscale AA pass cannot disagree
  // about where the specimen ends.
  std::vector<std::string> previewColophonLines(int width) const;
  // Height the colophon block occupies below the pane's label, gap included.
  int previewColophonHeight(int width) const;

  struct FontEntry {
    std::string name;
    bool isBuiltin;
    uint8_t settingIndex;
  };

  const SdCardFontRegistry* registry_;
  ButtonNavigator buttonNavigator_;
  std::vector<FontEntry> fonts_;
  int selectedIndex_ = 0;
  int previewFontIndex_ = 0;
  // false: the pangram/diacritic grid, one line per style. true: a prose
  // passage, which is what actually shows how a face reads at length. Toggled
  // with Confirm while the cursor sits on the applied font — see loop().
  bool proseSpecimen_ = false;
  // Latched when a Confirm hold has already fired, so the release that ends
  // the hold does not also apply the font. Cleared on that release.
  bool confirmHoldFired_ = false;
  // A one-shot line shown under the preview caption, for the cases where the
  // toggle REFUSES. A success needs no notice -- the row's own "Off" flipping
  // is the feedback -- but a refusal that drew nothing would be
  // indistinguishable from the hold not being recognised at all.
  std::string notice_;

  ThemeMetrics metrics_ = {};
  int afterHeader = 0;
  int bottomReserved = 0;
  int usableHeight = 0;
  int previewHeight = 0;
  int listHeight = 0;
};
