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
  int getFontIdForPreview(int index) const;
  void renderPreviewPane(int top, int height, int fontId, const char* fontName) const;

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

  ThemeMetrics metrics_ = {};
  int afterHeader = 0;
  int bottomReserved = 0;
  int usableHeight = 0;
  int previewHeight = 0;
};
