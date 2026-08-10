#include "BaseTheme.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "I18n.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Internal constants
namespace {
constexpr int homeMenuMargin = 20;
constexpr int homeMarginTop = 30;
constexpr int subtitleY = 738;
}  // namespace

void BaseTheme::drawBatteryOutline(const GfxRenderer& renderer, int x, int y, int battWidth, int rectHeight) {
  // Top line
  renderer.drawLine(x + 1, y, x + battWidth - 3, y);
  // Bottom line
  renderer.drawLine(x + 1, y + rectHeight - 1, x + battWidth - 3, y + rectHeight - 1);
  // Left line
  renderer.drawLine(x, y + 1, x, y + rectHeight - 2);
  // Battery end
  renderer.drawLine(x + battWidth - 2, y + 1, x + battWidth - 2, y + rectHeight - 2);
  renderer.drawPixel(x + battWidth - 1, y + 3);
  renderer.drawPixel(x + battWidth - 1, y + rectHeight - 4);
  renderer.drawLine(x + battWidth - 0, y + 4, x + battWidth - 0, y + rectHeight - 5);
}

void BaseTheme::drawBatteryLightningBolt(const GfxRenderer& renderer, int boltX, int boltY) {
  // Draw lightning bolt (white/inverted on black fill for visibility)
  renderer.drawLine(boltX + 4, boltY + 0, boltX + 5, boltY + 0, false);
  renderer.drawLine(boltX + 3, boltY + 1, boltX + 4, boltY + 1, false);
  renderer.drawLine(boltX + 2, boltY + 2, boltX + 5, boltY + 2, false);
  renderer.drawLine(boltX + 3, boltY + 3, boltX + 4, boltY + 3, false);
  renderer.drawLine(boltX + 2, boltY + 4, boltX + 3, boltY + 4, false);
  renderer.drawLine(boltX + 1, boltY + 5, boltX + 4, boltY + 5, false);
  renderer.drawLine(boltX + 2, boltY + 6, boltX + 3, boltY + 6, false);
  renderer.drawLine(boltX + 1, boltY + 7, boltX + 2, boltY + 7, false);
}

void BaseTheme::fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const {
  const bool charging = gpio.isUsbConnected();

  const int maxFillWidth = rect.width - 5;
  const int fillHeight = rect.height - 4;
  if (maxFillWidth <= 0 || fillHeight <= 0) {
    return;
  }
  // +1 to round up so we always fill at least one pixel
  int filledWidth = percentage * maxFillWidth / 100 + 1;
  if (filledWidth > maxFillWidth) {
    filledWidth = maxFillWidth;
  }

  // When charging, ensure minimum fill so lightning bolt is fully visible
  constexpr int minFillForBolt = 8;
  if (charging && filledWidth < minFillForBolt) {
    filledWidth = std::min(minFillForBolt, maxFillWidth);
  }

  renderer.fillRect(rect.x + 2, rect.y + 2, filledWidth, fillHeight);

  if (charging) {
    drawBatteryLightningBolt(renderer, rect.x + 4, rect.y + 2);
  }
}

void BaseTheme::drawBatteryLeft(const GfxRenderer& renderer, Rect rect, const bool showPercentage) const {
  // Left aligned: icon on left, percentage on right (reader mode)
  const uint16_t percentage = powerManager.getBatteryPercentage();
  const int y = rect.y + 6;

  if (showPercentage) {
    const auto percentageText = std::to_string(percentage) + "%";
    renderer.drawText(SMALL_FONT_ID, rect.x + batteryPercentSpacing + rect.width, rect.y, percentageText.c_str());
  }

  const Rect iconRect{rect.x, y, rect.width, rect.height};
  drawBatteryOutline(renderer, rect.x, y, rect.width, rect.height);
  fillBatteryIcon(renderer, iconRect, percentage);
}

void BaseTheme::drawBatteryRight(const GfxRenderer& renderer, Rect rect, const bool showPercentage) const {
  // Right aligned: percentage on left, icon on right (UI headers)
  // rect.x is already positioned for the icon (drawHeader calculated it)
  const uint16_t percentage = powerManager.getBatteryPercentage();
  const int y = rect.y + 6;

  if (showPercentage) {
    const auto percentageText = std::to_string(percentage) + "%";
    const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, percentageText.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x - textWidth - batteryPercentSpacing, rect.y, percentageText.c_str());
  }

  const Rect iconRect{rect.x, y, rect.width, rect.height};
  drawBatteryOutline(renderer, rect.x, y, rect.width, rect.height);
  fillBatteryIcon(renderer, iconRect, percentage);
}

void BaseTheme::drawProgressBar(const GfxRenderer& renderer, Rect rect, const size_t current,
                                const size_t total) const {
  if (total == 0) {
    return;
  }

  // Use 64-bit arithmetic to avoid overflow for large files
  const int percent = static_cast<int>((static_cast<uint64_t>(current) * 100) / total);

  LOG_DBG("UI", "Drawing progress bar: current=%u, total=%u, percent=%d", current, total, percent);
  // Draw outline
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  // Draw filled portion
  const int fillWidth = (rect.width - 4) * percent / 100;
  if (fillWidth > 0) {
    renderer.fillRect(rect.x + 2, rect.y + 2, fillWidth, rect.height - 4);
  }

  // Draw percentage text centered below bar
  const std::string percentText = std::to_string(percent) + "%";
  renderer.drawCenteredText(UI_10_FONT_ID, rect.y + rect.height + 15, percentText.c_str());
}

void BaseTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                const char* btn4) const {
  if (gpio.hasTouch()) {
    return;
  }

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 106;
  constexpr int buttonHeight = BaseMetrics::values.buttonHintsHeight;
  constexpr int buttonY = BaseMetrics::values.buttonHintsHeight;  // Distance from bottom
  constexpr int textYOffset = 7;                                  // Distance from top of button to text baseline
  // X3 has wider screen in portrait (528 vs 480), use more spacing
  constexpr int x4ButtonPositions[] = {25, 130, 245, 350};
  constexpr int x3ButtonPositions[] = {38, 154, 268, 384};
  const int* buttonPositions = gpio.deviceIsX3() ? x3ButtonPositions : x4ButtonPositions;
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    // Only draw if the label is non-empty
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      const int x = buttonPositions[i];
      renderer.fillRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, false);
      renderer.drawRect(x, pageHeight - buttonY, buttonWidth, buttonHeight);
      const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, labels[i]);
      const int textX = x + (buttonWidth - 1 - textWidth) / 2;
      renderer.drawText(UI_10_FONT_ID, textX, pageHeight - buttonY + textYOffset, labels[i]);
    }
  }
}

void BaseTheme::drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const {
  if (gpio.hasTouch()) {
    return;
  }
#ifdef CROSSPOINT_NO_PHYSICAL_SIDE_BUTTONS
  // No side buttons on the chassis (the iOS harness draws its own on-screen
  // pad instead), so the bracket hints label controls that do not exist -- and
  // on a tablet they land inside the text column, running through the note
  // being typed. Owner ruling 2026-08-09: remove them from the visual UI.
  (void)renderer;
  (void)topBtn;
  (void)bottomBtn;
  return;
#else

  const int screenWidth = renderer.getScreenWidth();
  constexpr int buttonWidth = BaseMetrics::values.sideButtonHintsWidth;  // Width on screen (height when rotated)
  constexpr int buttonHeight = 80;                                       // Height on screen (width when rotated)
  constexpr int buttonMargin = 4;

  if (gpio.deviceIsX3()) {
    // X3 layout: Up on left side, Down on right side, positioned higher
    constexpr int x3ButtonY = 155;

    if (topBtn != nullptr && topBtn[0] != '\0') {
      const int leftX = buttonMargin;
      renderer.drawRect(leftX, x3ButtonY, buttonWidth, buttonHeight);
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, topBtn);
      const int textHeight = renderer.getTextHeight(SMALL_FONT_ID);
      const int textX = leftX + (buttonWidth - textHeight) / 2;
      const int textY = x3ButtonY + (buttonHeight + textWidth) / 2;
      renderer.drawTextRotated90CW(SMALL_FONT_ID, textX, textY, topBtn);
    }

    if (bottomBtn != nullptr && bottomBtn[0] != '\0') {
      const int rightX = screenWidth - buttonMargin - buttonWidth;
      renderer.drawRect(rightX, x3ButtonY, buttonWidth, buttonHeight);
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, bottomBtn);
      const int textHeight = renderer.getTextHeight(SMALL_FONT_ID);
      const int textX = rightX + (buttonWidth - textHeight) / 2;
      const int textY = x3ButtonY + (buttonHeight + textWidth) / 2;
      renderer.drawTextRotated90CW(SMALL_FONT_ID, textX, textY, bottomBtn);
    }
  } else {
    // X4 layout: Both buttons stacked on right side
    constexpr int topButtonY = 345;
    const char* labels[] = {topBtn, bottomBtn};
    const int x = screenWidth - buttonMargin - buttonWidth;

    if (topBtn != nullptr && topBtn[0] != '\0') {
      renderer.drawLine(x, topButtonY, x + buttonWidth - 1, topButtonY);
      renderer.drawLine(x, topButtonY, x, topButtonY + buttonHeight - 1);
      renderer.drawLine(x + buttonWidth - 1, topButtonY, x + buttonWidth - 1, topButtonY + buttonHeight - 1);
    }

    if ((topBtn != nullptr && topBtn[0] != '\0') || (bottomBtn != nullptr && bottomBtn[0] != '\0')) {
      renderer.drawLine(x, topButtonY + buttonHeight, x + buttonWidth - 1, topButtonY + buttonHeight);
    }

    if (bottomBtn != nullptr && bottomBtn[0] != '\0') {
      renderer.drawLine(x, topButtonY + buttonHeight, x, topButtonY + 2 * buttonHeight - 1);
      renderer.drawLine(x + buttonWidth - 1, topButtonY + buttonHeight, x + buttonWidth - 1,
                        topButtonY + 2 * buttonHeight - 1);
      renderer.drawLine(x, topButtonY + 2 * buttonHeight - 1, x + buttonWidth - 1, topButtonY + 2 * buttonHeight - 1);
    }

    for (int i = 0; i < 2; i++) {
      if (labels[i] != nullptr && labels[i][0] != '\0') {
        const int y = topButtonY + i * buttonHeight;
        const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
        const int textHeight = renderer.getTextHeight(SMALL_FONT_ID);
        const int textX = x + (buttonWidth - textHeight) / 2;
        const int textY = y + (buttonHeight + textWidth) / 2;
        renderer.drawTextRotated90CW(SMALL_FONT_ID, textX, textY, labels[i]);
      }
    }
  }
#endif
}

int BaseTheme::getListRowStep(bool hasSubtitle, int subtitleLines) const {
  if (!hasSubtitle) return BaseMetrics::values.listRowHeight;
  const int extraLines = std::max(0, subtitleLines - 1);
  return BaseMetrics::values.listWithSubtitleRowHeight + extraLines * BaseMetrics::values.listSubtitleLineStep;
}

int BaseTheme::getListPageItems(int contentHeight, bool hasSubtitle, int subtitleLines) const {
  const int rowStep = getListRowStep(hasSubtitle, subtitleLines);
  if (rowStep <= 0) return 1;
  return std::max(1, contentHeight / rowStep);
}

void BaseTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                         const std::function<std::string(int index)>& rowTitle,
                         const std::function<std::string(int index)>& rowSubtitle,
                         const std::function<UIIcon(int index)>& rowIcon,
                         const std::function<std::string(int index)>& rowValue, bool highlightValue,
                         const std::function<bool(int index)>& rowDimmed, int subtitleLines) const {
  int rowHeight = getListRowStep(rowSubtitle != nullptr, subtitleLines);
  int pageItems = rowHeight > 0 ? std::max(1, rect.height / rowHeight) : 1;

  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  if (totalPages > 1) {
    constexpr int indicatorWidth = 20;
    constexpr int arrowSize = 6;
    constexpr int margin = 15;  // Offset from right edge

    const int centerX = rect.x + rect.width - indicatorWidth / 2 - margin;
    const int indicatorTop = rect.y;  // Offset to avoid overlapping side button hints
    const int indicatorBottom = rect.y + rect.height - arrowSize;

    // Draw up arrow at top (^) - narrow point at top, wide base at bottom
    for (int i = 0; i < arrowSize; ++i) {
      const int lineWidth = 1 + i * 2;
      const int startX = centerX - i;
      renderer.drawLine(startX, indicatorTop + i, startX + lineWidth - 1, indicatorTop + i);
    }

    // Draw down arrow at bottom (v) - wide base at top, narrow point at bottom
    for (int i = 0; i < arrowSize; ++i) {
      const int lineWidth = 1 + (arrowSize - 1 - i) * 2;
      const int startX = centerX - (arrowSize - 1 - i);
      renderer.drawLine(startX, indicatorBottom - arrowSize + 1 + i, startX + lineWidth - 1,
                        indicatorBottom - arrowSize + 1 + i);
    }
  }

  // Draw selection
  int contentWidth = rect.width - 5;
  if (selectedIndex >= 0) {
    renderer.fillRect(rect.x, rect.y + selectedIndex % pageItems * rowHeight - 2, rect.width, rowHeight);
  }
  constexpr int minValueGap = 10;

  // Draw all items
  const auto pageStartIndex = selectedIndex / pageItems * pageItems;

  // The value badge OVERLAYS the row text — see LyraTheme::drawList. It carves
  // no column out of it, so a badge that appears and disappears with the
  // selection cannot re-wrap the title or the subtitle under it.
  const int rowTextWidth = contentWidth - BaseMetrics::values.contentSidePadding * 2;

  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    const int itemY = rect.y + (i % pageItems) * rowHeight;

    std::string valueText;
    if (rowValue != nullptr) {
      valueText = rowValue(i);
      if (!valueText.empty()) {
        const int maxValW = std::max(0, rowTextWidth - 40 - minValueGap);
        valueText = renderer.truncatedText(UI_10_FONT_ID, valueText.c_str(), maxValW);
      }
    }

    auto itemName = rowTitle(i);
    auto font = UI_10_FONT_ID;
    auto item = renderer.truncatedText(font, itemName.c_str(), rowTextWidth);
    renderer.drawText(font, rect.x + BaseMetrics::values.contentSidePadding, itemY, item.c_str(), i != selectedIndex);
    // Rightmost pixel this row's text reaches; the badge below only needs an
    // outline when it is actually covering some of it.
    int textRight = rect.x + BaseMetrics::values.contentSidePadding + renderer.getTextWidth(font, item.c_str());

    // Apply checkerboard dither to create gray text effect for dimmed items
    if (rowDimmed && rowDimmed(i) && i != selectedIndex) {
      const int titleWidth = renderer.getTextWidth(font, item.c_str());
      const int lineH = renderer.getLineHeight(font);
      const int tx = rect.x + BaseMetrics::values.contentSidePadding;
      for (int py = itemY; py < itemY + lineH; py++)
        for (int px = tx; px < tx + titleWidth; px++)
          if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
    }

    if (rowSubtitle != nullptr) {
      std::string subtitleText = rowSubtitle(i);
      if (!subtitleText.empty()) {
        // subtitleLines == 1 keeps the original single truncated line, so every
        // list that never asks for more is byte-identical to before.
        const int subtitleX = rect.x + BaseMetrics::values.contentSidePadding;
        if (subtitleLines <= 1) {
          auto subtitle = renderer.truncatedText(SMALL_FONT_ID, subtitleText.c_str(), rowTextWidth);
          renderer.drawText(SMALL_FONT_ID, subtitleX, itemY + 22, subtitle.c_str(), i != selectedIndex);
          textRight = std::max(textRight, subtitleX + renderer.getTextWidth(SMALL_FONT_ID, subtitle.c_str()));
        } else {
          // Full row width: the badge is painted over these lines, not beside
          // them, so the wrap points are the same whether the row carries one.
          const auto lines = renderer.wrappedText(SMALL_FONT_ID, subtitleText.c_str(), rowTextWidth, subtitleLines);
          int subtitleY = itemY + 22;
          for (const auto& line : lines) {
            renderer.drawText(SMALL_FONT_ID, subtitleX, subtitleY, line.c_str(), i != selectedIndex);
            textRight = std::max(textRight, subtitleX + renderer.getTextWidth(SMALL_FONT_ID, line.c_str()));
            subtitleY += BaseMetrics::values.listSubtitleLineStep;
          }
        }
      }
    }

    if (!valueText.empty()) {
      const auto valueTextWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str());
      // Opaque backing first — the badge sits ON the row text now, so without
      // it the label would be drawn over whatever line runs underneath. Matches
      // the row's own background: black on the selected row, white elsewhere.
      const int badgeX = rect.x + contentWidth - BaseMetrics::values.contentSidePadding - valueTextWidth;
      renderer.fillRect(badgeX - minValueGap, itemY, valueTextWidth + minValueGap * 2, rowHeight, i == selectedIndex);
      // Outline only when it covers something: on a row whose value sits in
      // clear space this would be a box around nothing.
      if (i != selectedIndex && textRight > badgeX - minValueGap) {
        renderer.drawRect(badgeX - minValueGap, itemY, valueTextWidth + minValueGap * 2, rowHeight, true);
      }
      // Centred in the row rather than pinned near its top, so the label stays
      // put as extra subtitle lines make the row taller.
      const int valueY = itemY + (rowHeight - renderer.getTextHeight(UI_10_FONT_ID)) / 2;
      renderer.drawText(UI_10_FONT_ID, rect.x + contentWidth - BaseMetrics::values.contentSidePadding - valueTextWidth,
                        valueY, valueText.c_str(), i != selectedIndex);
    }
  }
}

void BaseTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const {
  // Hide last battery draw
  constexpr int maxBatteryWidth = 80;
  renderer.fillRect(rect.x + rect.width - maxBatteryWidth, rect.y + 5, maxBatteryWidth,
                    BaseMetrics::values.batteryHeight + 10, false);

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  // Position icon at right edge, drawBatteryRight will place text to the left
  const int batteryX = rect.x + rect.width - 12 - BaseMetrics::values.batteryWidth;
  drawBatteryRight(renderer,
                   Rect{batteryX, rect.y + 5, BaseMetrics::values.batteryWidth, BaseMetrics::values.batteryHeight},
                   showBatteryPercentage);

  if (title) {
    int padding = rect.width - batteryX + BaseMetrics::values.batteryWidth;
    auto truncatedTitle = renderer.truncatedText(UI_12_FONT_ID, title,
                                                 rect.width - padding * 2 - BaseMetrics::values.contentSidePadding * 2,
                                                 EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_12_FONT_ID, rect.y + 5, truncatedTitle.c_str(), true, EpdFontFamily::BOLD);
  }

  if (subtitle) {
    auto truncatedSubtitle = renderer.truncatedText(
        SMALL_FONT_ID, subtitle, rect.width - BaseMetrics::values.contentSidePadding * 2, EpdFontFamily::REGULAR);
    int truncatedSubtitleWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedSubtitle.c_str());
    renderer.drawText(SMALL_FONT_ID,
                      rect.x + rect.width - BaseMetrics::values.contentSidePadding - truncatedSubtitleWidth, subtitleY,
                      truncatedSubtitle.c_str(), true);
  }
}

void BaseTheme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label, const char* rightLabel) const {
  constexpr int maxListValueWidth = 200;

  int currentX = rect.x + BaseMetrics::values.contentSidePadding;
  int rightSpace = BaseMetrics::values.contentSidePadding;
  if (rightLabel) {
    auto truncatedRightLabel =
        renderer.truncatedText(SMALL_FONT_ID, rightLabel, maxListValueWidth, EpdFontFamily::REGULAR);
    int rightLabelWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedRightLabel.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - BaseMetrics::values.contentSidePadding - rightLabelWidth,
                      rect.y + 7, truncatedRightLabel.c_str());
    rightSpace += rightLabelWidth + 10;
  }

  auto truncatedLabel = renderer.truncatedText(
      UI_12_FONT_ID, label, rect.width - BaseMetrics::values.contentSidePadding - rightSpace, EpdFontFamily::REGULAR);
  renderer.drawText(UI_12_FONT_ID, currentX, rect.y, truncatedLabel.c_str(), true, EpdFontFamily::REGULAR);
}

// Draw the "Recent Book" cover card on the home screen
// TODO: Refactor method to make it cleaner, split into smaller methods
void BaseTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                    const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                    bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  const bool hasContinueReading = !recentBooks.empty();
  const bool bookSelected = hasContinueReading && selectorIndex == 0;

  // --- Top "book" card for the current title (selectorIndex == 0) ---
  // When there's no cover image, use fixed size (half screen)
  // When there's cover image, adapt width to image aspect ratio, keep height fixed at 400px
  const int baseHeight = rect.height;  // Fixed height (400px)

  int bookWidth, bookX;
  bool hasCoverImage = false;

  if (hasContinueReading && !recentBooks[0].coverBmpPath.empty()) {
    // Try to get actual image dimensions from BMP header
    const std::string coverBmpPath =
        UITheme::getCoverThumbPath(recentBooks[0].coverBmpPath, BaseMetrics::values.homeCoverHeight);

    HalFile file;
    if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        hasCoverImage = true;
        const int imgWidth = bitmap.getWidth();
        const int imgHeight = bitmap.getHeight();

        // Calculate width based on aspect ratio, maintaining baseHeight
        if (imgWidth > 0 && imgHeight > 0) {
          const float aspectRatio = static_cast<float>(imgWidth) / static_cast<float>(imgHeight);
          bookWidth = static_cast<int>(baseHeight * aspectRatio);

          // Ensure width doesn't exceed reasonable limits (max 90% of screen width)
          const int maxWidth = static_cast<int>(rect.width * 0.9f);
          if (bookWidth > maxWidth) {
            bookWidth = maxWidth;
          }
        } else {
          bookWidth = rect.width / 2;  // Fallback
        }
      }
    }
  }

  if (!hasCoverImage) {
    // No cover: use half screen size
    bookWidth = rect.width / 2;
  }

  bookX = rect.x + (rect.width - bookWidth) / 2;
  const int bookY = rect.y;
  const int bookHeight = baseHeight;

  // Bookmark dimensions (used in multiple places)
  const int bookmarkWidth = bookWidth / 8;
  const int bookmarkHeight = bookHeight / 5;
  const int bookmarkX = bookX + bookWidth - bookmarkWidth - 10;
  const int bookmarkY = bookY + 5;

  // Draw book card regardless, fill with message based on `hasContinueReading`
  {
    // Draw cover image as background if available (inside the box)
    // Only load from SD on first render, then use stored buffer

    if (hasContinueReading && !recentBooks[0].coverBmpPath.empty() && !coverRendered) {
      const std::string coverBmpPath =
          UITheme::getCoverThumbPath(recentBooks[0].coverBmpPath, BaseMetrics::values.homeCoverHeight);

      // First time: load cover from SD and render
      HalFile file;
      if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          LOG_DBG("THEME", "Rendering bmp");

          // Draw the cover image (bookWidth and bookHeight already match image aspect ratio)
          renderer.drawBitmap(bitmap, bookX, bookY, bookWidth, bookHeight);

          // Draw border around the card
          renderer.drawRect(bookX, bookY, bookWidth, bookHeight);

          // No bookmark ribbon when cover is shown - it would just cover the art

          // Store the buffer with cover image for fast navigation
          coverBufferStored = storeCoverBuffer();
          coverRendered = coverBufferStored;  // Only consider it rendered if we successfully stored the buffer

          // First render: if selected, draw selection indicators now
          if (bookSelected) {
            LOG_DBG("THEME", "Drawing selection");
            renderer.drawRect(bookX + 1, bookY + 1, bookWidth - 2, bookHeight - 2);
            renderer.drawRect(bookX + 2, bookY + 2, bookWidth - 4, bookHeight - 4);
          }
        }
      }
    }

    if (!bufferRestored && !coverRendered) {
      // No cover image: draw border or fill, plus bookmark as visual flair
      if (bookSelected) {
        renderer.fillRect(bookX, bookY, bookWidth, bookHeight);
      } else {
        renderer.drawRect(bookX, bookY, bookWidth, bookHeight);
      }

      // Draw bookmark ribbon when no cover image (visual decoration)
      if (hasContinueReading) {
        const int notchDepth = bookmarkHeight / 3;
        const int centerX = bookmarkX + bookmarkWidth / 2;

        const int xPoints[5] = {
            bookmarkX,                  // top-left
            bookmarkX + bookmarkWidth,  // top-right
            bookmarkX + bookmarkWidth,  // bottom-right
            centerX,                    // center notch point
            bookmarkX                   // bottom-left
        };
        const int yPoints[5] = {
            bookmarkY,                                // top-left
            bookmarkY,                                // top-right
            bookmarkY + bookmarkHeight,               // bottom-right
            bookmarkY + bookmarkHeight - notchDepth,  // center notch point
            bookmarkY + bookmarkHeight                // bottom-left
        };

        // Draw bookmark ribbon (inverted if selected)
        renderer.fillPolygon(xPoints, yPoints, 5, !bookSelected);
      }
    }

    // If buffer was restored, draw selection indicators if needed
    if (bufferRestored && bookSelected && coverRendered) {
      // Draw selection border (no bookmark inversion needed since cover has no bookmark)
      renderer.drawRect(bookX + 1, bookY + 1, bookWidth - 2, bookHeight - 2);
      renderer.drawRect(bookX + 2, bookY + 2, bookWidth - 4, bookHeight - 4);
    } else if (!coverRendered && !bufferRestored) {
      // Selection border already handled above in the no-cover case
    }
  }

  if (hasContinueReading) {
    const std::string& lastBookTitle = recentBooks[0].title;
    const std::string& lastBookAuthor = recentBooks[0].author;

    // Invert text colors based on selection state:
    // - With cover: selected = white text on black box, unselected = black text on white box
    // - Without cover: selected = white text on black card, unselected = black text on white card

    auto lines = renderer.wrappedText(UI_12_FONT_ID, lastBookTitle.c_str(), bookWidth - 40, 3);

    // Book title text
    int totalTextHeight = renderer.getLineHeight(UI_12_FONT_ID) * static_cast<int>(lines.size());
    if (!lastBookAuthor.empty()) {
      totalTextHeight += renderer.getLineHeight(UI_10_FONT_ID) * 3 / 2;
    }

    // Vertically center the title block within the card
    int titleYStart = bookY + (bookHeight - totalTextHeight) / 2;

    const auto truncatedAuthor = lastBookAuthor.empty()
                                     ? std::string{}
                                     : renderer.truncatedText(UI_10_FONT_ID, lastBookAuthor.c_str(), bookWidth - 40);

    // If cover image was rendered, draw box behind title and author
    if (coverRendered) {
      constexpr int boxPadding = 8;
      // Calculate the max text width for the box
      int maxTextWidth = 0;
      for (const auto& line : lines) {
        const int lineWidth = renderer.getTextWidth(UI_12_FONT_ID, line.c_str());
        if (lineWidth > maxTextWidth) {
          maxTextWidth = lineWidth;
        }
      }
      if (!truncatedAuthor.empty()) {
        const int authorWidth = renderer.getTextWidth(UI_10_FONT_ID, truncatedAuthor.c_str());
        if (authorWidth > maxTextWidth) {
          maxTextWidth = authorWidth;
        }
      }

      const int boxWidth = maxTextWidth + boxPadding * 2;
      const int boxHeight = totalTextHeight + boxPadding * 2;
      const int boxX = rect.x + (rect.width - boxWidth) / 2;
      const int boxY = titleYStart - boxPadding;

      // Draw box (inverted when selected: black box instead of white)
      renderer.fillRect(boxX, boxY, boxWidth, boxHeight, bookSelected);
      // Draw border around the box (inverted when selected: white border instead of black)
      renderer.drawRect(boxX, boxY, boxWidth, boxHeight, !bookSelected);
    }

    for (const auto& line : lines) {
      renderer.drawCenteredText(UI_12_FONT_ID, titleYStart, line.c_str(), !bookSelected);
      titleYStart += renderer.getLineHeight(UI_12_FONT_ID);
    }

    if (!truncatedAuthor.empty()) {
      titleYStart += renderer.getLineHeight(UI_10_FONT_ID) / 2;
      renderer.drawCenteredText(UI_10_FONT_ID, titleYStart, truncatedAuthor.c_str(), !bookSelected);
    }

    // "Continue Reading" label at the bottom
    const int continueY = bookY + bookHeight - renderer.getLineHeight(UI_10_FONT_ID) * 3 / 2;
    if (coverRendered) {
      // Draw box behind "Continue Reading" text (inverted when selected: black box instead of white)
      const char* continueText = tr(STR_CONTINUE_READING);
      const int continueTextWidth = renderer.getTextWidth(UI_10_FONT_ID, continueText);
      constexpr int continuePadding = 6;
      const int continueBoxWidth = continueTextWidth + continuePadding * 2;
      const int continueBoxHeight = renderer.getLineHeight(UI_10_FONT_ID) + continuePadding;
      const int continueBoxX = rect.x + (rect.width - continueBoxWidth) / 2;
      const int continueBoxY = continueY - continuePadding / 2;
      renderer.fillRect(continueBoxX, continueBoxY, continueBoxWidth, continueBoxHeight, bookSelected);
      renderer.drawRect(continueBoxX, continueBoxY, continueBoxWidth, continueBoxHeight, !bookSelected);
      renderer.drawCenteredText(UI_10_FONT_ID, continueY, continueText, !bookSelected);
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, continueY, tr(STR_CONTINUE_READING), !bookSelected);
    }
  } else {
    // No book to continue reading
    const int y =
        bookY + (bookHeight - renderer.getLineHeight(UI_12_FONT_ID) - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_NO_OPEN_BOOK));
    renderer.drawCenteredText(UI_10_FONT_ID, y + renderer.getLineHeight(UI_12_FONT_ID), tr(STR_START_READING));
  }
}

namespace {
// Decorrelates pattern choice from the raw path hash: std::hash of short
// strings can leave structured low bits, and books added from one folder
// would then all draw the same band.
constexpr uint32_t mixCoverSeed(uint32_t s) {
  s ^= s >> 16;
  s *= 0x7feb352dU;
  s ^= s >> 15;
  s *= 0x846ca68bU;
  s ^= s >> 16;
  return s;
}

// --- Clipped-span helpers ---------------------------------------------------
//
// The circle-built patterns (seigaiha, shippo) place discs whose bounding
// boxes cross the band edges, so they cannot use drawRoundedRect/drawArc —
// those clip to the screen, not to the band, and at home the pixels past the
// band edge belong to a neighboring tile. Everything rasterises through
// hspanClipped instead.

// Integer sqrt (Newton). The ESP32-C3 is RV32IMC: hardware divide, no FPU.
int isqrt32(int v) {
  if (v <= 0) return 0;
  int c = v, n = (c + 1) / 2;
  while (n < c) {
    c = n;
    n = (c + v / c) / 2;
  }
  return c;
}

struct BandClip {
  int x0, y0, x1, y1;  // inclusive
};

void hspanClipped(const GfxRenderer& renderer, const BandClip& c, int xa, int xb, const int yy, const bool state) {
  if (yy < c.y0 || yy > c.y1) return;
  xa = std::max(xa, c.x0);
  xb = std::min(xb, c.x1);
  if (xb >= xa) renderer.fillRect(xa, yy, xb - xa + 1, 1, state);
}

void fillDiscClipped(const GfxRenderer& renderer, const BandClip& c, const int cx, const int cy, const int rad,
                     const bool state) {
  for (int dy = -rad; dy <= rad; dy++) {
    const int half = isqrt32(rad * rad - dy * dy);
    hspanClipped(renderer, c, cx - half, cx + half, cy + dy, state);
  }
}

void ringClipped(const GfxRenderer& renderer, const BandClip& c, const int cx, const int cy, const int rad,
                 const int thickness, const bool state) {
  const int ri = std::max(0, rad - thickness);
  for (int dy = -rad; dy <= rad; dy++) {
    const int ho = isqrt32(rad * rad - dy * dy);
    if (dy > -ri && dy < ri) {
      const int hi = isqrt32(ri * ri - dy * dy);
      hspanClipped(renderer, c, cx - ho, cx - hi - 1, cy + dy, state);
      hspanClipped(renderer, c, cx + hi + 1, cx + ho, cy + dy, state);
    } else {
      hspanClipped(renderer, c, cx - ho, cx + ho, cy + dy, state);
    }
  }
}

constexpr int COVER_BAND_PATTERN_COUNT = 12;

// The decorative band across the top of a generated cover. Every variant
// draws only via fillRect/fillRectDither/fillPolygon/hspanClipped with
// extents clamped to the band, so nothing can spill into a neighboring
// home-grid tile. The set mixes plain geometry with traditional motifs —
// Japanese wagara (ichimatsu, seigaiha, shippo, yagasuri, uroko), the Greek
// meander, kente-inspired stripe blocks and an Andean step motif — all of
// which reduce to bold 1-bit shapes that survive both the ~67px home tile
// band and the ~240px sleep-screen band.
void drawGeneratedCoverBand(const GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                            const uint32_t seed) {
  const bool dense = (seed >> 8) % 2 == 0;
  const BandClip clip{x, y, x + w - 1, y + h - 1};
  switch (seed % COVER_BAND_PATTERN_COUNT) {
    case 0: {  // horizontal bars
      const int bar = std::max(4, h / (dense ? 8 : 5));
      for (int yy = y; yy < y + h; yy += 2 * bar) {
        renderer.fillRect(x, yy, w, std::min(bar, y + h - yy), true);
      }
      break;
    }
    case 1: {  // vertical bars
      const int bar = std::max(4, w / (dense ? 12 : 7));
      for (int xx = x; xx < x + w; xx += 2 * bar) {
        renderer.fillRect(xx, y, std::min(bar, x + w - xx), h, true);
      }
      break;
    }
    case 2: {  // checkerboard (ichimatsu)
      const int cell = std::max(6, h / (dense ? 4 : 3));
      for (int row = 0; row * cell < h; row++) {
        for (int col = 0; col * cell < w; col++) {
          if ((row + col) % 2 == 0) {
            renderer.fillRect(x + col * cell, y + row * cell, std::min(cell, w - col * cell),
                              std::min(cell, h - row * cell), true);
          }
        }
      }
      break;
    }
    case 3: {  // black-to-white dither fade
      const int strip = std::max(1, h / 4);
      renderer.fillRect(x, y, w, strip, true);
      renderer.fillRectDither(x, y + strip, w, strip, Color::DarkGray);
      renderer.fillRectDither(x, y + 2 * strip, w, h - 3 * strip, Color::LightGray);
      break;
    }
    case 4: {  // zigzag teeth: apex-down triangles side by side
      const int tw = std::max(10, w / (dense ? 8 : 5));
      for (int x0 = x; x0 < x + w; x0 += tw) {
        const int x1 = std::min(x0 + tw, x + w);
        const int xPts[3] = {x0, x1, x0 + (x1 - x0) / 2};
        const int yPts[3] = {y, y, y + h};
        renderer.fillPolygon(xPts, yPts, 3, true);
      }
      break;
    }
    case 5: {  // seigaiha: overlapping wave fans of concentric arcs
      const int rad = std::max(10, h / (dense ? 3 : 2));
      const int stroke = std::max(1, rad / 9);
      // Rows march down half a radius at a time; each lower row blanks the
      // bottom of the row above, which is what turns full discs into fans.
      int rowIdx = 0;
      for (int cy = y; cy < y + h + rad; cy += std::max(4, rad / 2), rowIdx++) {
        const int off = (rowIdx % 2 == 0) ? 0 : rad;
        for (int cx = x - rad + off; cx < x + w + rad; cx += 2 * rad) {
          fillDiscClipped(renderer, clip, cx, cy, rad, false);
          ringClipped(renderer, clip, cx, cy, rad, stroke, true);
          ringClipped(renderer, clip, cx, cy, rad * 2 / 3, stroke, true);
          ringClipped(renderer, clip, cx, cy, rad / 3, stroke, true);
          fillDiscClipped(renderer, clip, cx, cy, std::max(1, rad / 9), true);
        }
      }
      break;
    }
    case 6: {  // shippo: interlocking circle lattice (four-petal lenses)
      const int rad = std::max(12, h / (dense ? 3 : 2));
      const int stroke = std::max(1, rad / 8);
      const int pitch = rad * 141 / 100;  // r*sqrt(2): rims meet at neighbors' centres
      for (int cy = y; cy - rad <= y + h; cy += pitch) {
        for (int cx = x; cx - rad <= x + w; cx += pitch) {
          ringClipped(renderer, clip, cx, cy, rad, stroke, true);
          ringClipped(renderer, clip, cx + pitch / 2, cy + pitch / 2, rad, stroke, true);
        }
      }
      break;
    }
    case 7: {  // yagasuri: arrow-fletching chevron columns
      const int cw = std::max(10, w / (dense ? 8 : 5));
      const int drop = cw / 2;                 // how far the V dips
      const int stripe = std::max(2, cw / 4);  // chevron stroke
      const int step = std::max(drop + stripe, cw);
      for (int col = 0; col * cw < w; col++) {
        const int cx0 = x + col * cw;
        const int cx1 = std::min(cx0 + cw, x + w);
        const int cxm = (cx0 + cx1) / 2;
        const int phase = (col % 2 == 0) ? 0 : step / 2;
        for (int ry = y - step + phase; ry < y + h; ry += step) {
          int xs[6] = {cx0, cxm, cx1, cx1, cxm, cx0};
          int ys[6] = {ry, ry + drop, ry, ry + stripe, ry + drop + stripe, ry + stripe};
          for (int& v : ys) v = std::clamp(v, y, y + h);
          renderer.fillPolygon(xs, ys, 6, true);
        }
      }
      break;
    }
    case 8: {  // uroko: staggered rows of scale triangles
      const int th = std::max(6, h / (dense ? 4 : 3));
      const int tw = 2 * th;
      for (int row = 0; row * th < h; row++) {
        const int ry = y + row * th;
        const int rb = std::min(ry + th, y + h);
        const int off = (row % 2 == 0) ? 0 : tw / 2;
        for (int cx = x - tw + off; cx < x + w; cx += tw) {
          int xs[3] = {cx, cx + tw, cx + tw / 2};
          for (int& v : xs) v = std::clamp(v, x, x + w);
          const int ys[3] = {rb, rb, ry};
          renderer.fillPolygon(xs, ys, 3, true);
        }
      }
      break;
    }
    case 9: {  // Greek meander (fret) strips
      // One key cell on a 5x5 grid of stroke units; tiles seamlessly sideways.
      static constexpr int KEY_RECTS[][4] = {
          {0, 0, 5, 1}, {4, 0, 1, 5}, {0, 2, 3, 1}, {0, 2, 1, 3}, {2, 3, 1, 2}, {2, 4, 3, 1},
      };
      const int t = std::max(2, h / (dense ? 18 : 12));
      const int cell = 5 * t;
      for (int ry = y + t; ry + cell <= y + h; ry += cell + 2 * t) {
        for (int cx = x; cx < x + w; cx += cell) {
          for (const auto& rct : KEY_RECTS) {
            const int rx = cx + rct[0] * t;
            const int rw = std::min(rct[2] * t, x + w - rx);
            if (rw > 0) renderer.fillRect(rx, ry + rct[1] * t, rw, rct[3] * t, true);
          }
        }
      }
      break;
    }
    case 10: {  // kente-inspired stripe blocks: weave direction alternates
      const int bs = std::max(10, h / (dense ? 3 : 2));
      const int bar = std::max(2, bs / 5);
      for (int br = 0; br * bs < h; br++) {
        for (int bc = 0; bc * bs < w; bc++) {
          const int bx = x + bc * bs, by = y + br * bs;
          const int bw = std::min(bs, x + w - bx), bh = std::min(bs, y + h - by);
          if ((br + bc) % 2 == 0) {
            for (int yy = by; yy < by + bh; yy += 2 * bar) {
              renderer.fillRect(bx, yy, bw, std::min(bar, by + bh - yy), true);
            }
          } else {
            for (int xx = bx; xx < bx + bw; xx += 2 * bar) {
              renderer.fillRect(xx, by, std::min(bar, bx + bw - xx), bh, true);
            }
          }
        }
      }
      break;
    }
    default: {                                          // Andean stepped diamonds
      const int levels = dense ? 4 : 3;                 // step rows per half
      const int s = std::max(3, h / (2 * levels + 1));  // one square step
      const int diaH = 2 * levels * s;
      const int topY = y + (h - diaH) / 2;
      const int pitch = diaH + 2 * s;
      for (int cx = x + pitch / 2; cx - diaH / 2 < x + w + diaH; cx += pitch) {
        for (int i = 0; i < 2 * levels; i++) {
          const int halfSteps = (i < levels) ? (i + 1) : (2 * levels - i);
          const int rx = std::max(cx - halfSteps * s, x);
          const int rw = std::min(cx + halfSteps * s, x + w) - rx;
          const int ry = topY + i * s;
          if (rw > 0) renderer.fillRect(rx, ry, rw, std::min(s, y + h - ry), true);
        }
      }
      break;
    }
  }
}
}  // namespace

void BaseTheme::drawGeneratedCover(const GfxRenderer& renderer, const Rect rect, const char* title, const char* author,
                                   const uint32_t seed) const {
  if (rect.width <= 12 || rect.height <= 12) {
    return;
  }
  const uint32_t mixed = mixCoverSeed(seed);

  // Blank plate + frame.
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, true);

  // Decorative band across the top ~30%, closed off by a rule.
  const int ruleH = std::max(2, rect.height / 120);
  const int bandH = rect.height * 3 / 10;
  drawGeneratedCoverBand(renderer, rect.x + 1, rect.y + 1, rect.width - 2, bandH, mixed);
  renderer.fillRect(rect.x + 1, rect.y + 1 + bandH, rect.width - 2, ruleH, true);

  // Full-screen covers (sleep) get the large Libre Franklin reader cuts; home
  // tiles the UI chrome faces. OMIT_FONTS builds drop the 18 pt cut —
  // getLineHeight() returning 0 is the absence signal, and the chrome faces
  // always exist.
  int titleFontId = UI_12_FONT_ID;
  int authorFontId = SMALL_FONT_ID;
  if (rect.height >= 400 && renderer.getLineHeight(LIBREFRANKLIN_READER_18_FONT_ID) > 0) {
    titleFontId = LIBREFRANKLIN_READER_18_FONT_ID;
    authorFontId = LIBREFRANKLIN_READER_14_FONT_ID;
  }

  const int margin = std::max(6, std::min(rect.width, rect.height) / 24);
  const int titleTop = rect.y + 1 + bandH + ruleH + margin;
  const int titleBottom = rect.y + rect.height * 72 / 100;
  const int authorTop = titleBottom;
  const int authorBottom = rect.y + rect.height - margin;

  if (title && *title && titleBottom > titleTop) {
    const Rect titleBounds(rect.x + margin, titleTop, rect.width - 2 * margin, titleBottom - titleTop);
    UITheme::drawCenteredWrappedText(renderer, titleBounds, titleFontId, title, 4, true, EpdFontFamily::BOLD);
  }

  if (author && *author && authorBottom > authorTop) {
    // Short centered rule separating title and author blocks.
    const int sepW = rect.width / 5;
    renderer.fillRect(rect.x + (rect.width - sepW) / 2, authorTop, sepW, std::max(1, ruleH / 2), true);
    const Rect authorBounds(rect.x + margin, authorTop + margin / 2, rect.width - 2 * margin,
                            authorBottom - authorTop - margin / 2);
    UITheme::drawCenteredWrappedText(renderer, authorBounds, authorFontId, author, 2);
  }
}

void BaseTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                               const std::function<std::string(int index)>& buttonLabel,
                               const std::function<UIIcon(int index)>& rowIcon) const {
  // Same fit rule as LyraTheme::drawButtonMenu -- see the note there. Rows are
  // compressed to stay inside rect rather than marching off the bottom of it.
  const int naturalPitch = BaseMetrics::values.menuRowHeight + BaseMetrics::values.menuSpacing;
  const int avail = rect.height - BaseMetrics::values.verticalSpacing;
  int rowHeight = BaseMetrics::values.menuRowHeight;
  int pitch = naturalPitch;
  if (buttonCount > 1 && avail > 0) {
    const int needed = (buttonCount - 1) * naturalPitch + rowHeight;
    if (needed > avail) {
      pitch = (avail - rowHeight) / (buttonCount - 1);
      if (pitch <= rowHeight) {
        pitch = avail / buttonCount;
        rowHeight = pitch > BaseMetrics::values.menuSpacing ? pitch - BaseMetrics::values.menuSpacing : pitch;
      }
    }
  }

  for (int i = 0; i < buttonCount; ++i) {
    const int tileY = BaseMetrics::values.verticalSpacing + rect.y + static_cast<int>(i) * pitch;

    const bool selected = selectedIndex == i;

    if (selected) {
      renderer.fillRect(rect.x + BaseMetrics::values.contentSidePadding, tileY,
                        rect.width - BaseMetrics::values.contentSidePadding * 2, rowHeight);
    } else {
      renderer.drawRect(rect.x + BaseMetrics::values.contentSidePadding, tileY,
                        rect.width - BaseMetrics::values.contentSidePadding * 2, rowHeight);
    }

    std::string labelStr = buttonLabel(i);
    const char* label = labelStr.c_str();
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label);
    const int textX = rect.x + (rect.width - textWidth) / 2;
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int textY = tileY + (rowHeight - lineHeight) / 2;  // vertically centered assuming y is top of text
    // Invert text when the tile is selected, to contrast with the filled background
    renderer.drawText(UI_10_FONT_ID, textX, textY, label, selectedIndex != i);
  }
}

Rect BaseTheme::drawPopup(const GfxRenderer& renderer, const char* message) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int marginX = metrics.popupMarginX;
  const int marginY = metrics.popupMarginY;
  const int frameThickness = metrics.popupFrameThickness;
  const EpdFontFamily::Style popupFontFamily = metrics.popupTextBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  // Scale y position proportionally to screen height
  const int y = static_cast<int>(renderer.getScreenHeight() * metrics.popupTopOffsetRatio);
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, message, popupFontFamily);
  const int textHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int w = textWidth + marginX * 2;
  const int h = textHeight + marginY * 2;
  const int x = (renderer.getScreenWidth() - w) / 2;

  const bool useRoundedPopup = metrics.popupCornerRadius > 0;
  if (useRoundedPopup) {
    renderer.fillRoundedRect(x - frameThickness, y - frameThickness, w + frameThickness * 2, h + frameThickness * 2,
                             metrics.popupCornerRadius + frameThickness, Color::White);
    renderer.fillRoundedRect(x, y, w, h, metrics.popupCornerRadius, Color::Black);
  } else {
    renderer.fillRect(x - frameThickness, y - frameThickness, w + frameThickness * 2, h + frameThickness * 2, true);
    renderer.fillRect(x, y, w, h, false);
  }

  const int textX = x + (w - textWidth) / 2;
  const int textY = y + marginY + metrics.popupTextBaselineOffsetY;
  renderer.drawText(UI_12_FONT_ID, textX, textY, message, metrics.popupTextInverted, popupFontFamily);
  renderer.displayBuffer();
  return Rect{x, y, w, h};
}

void BaseTheme::fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, const int progress) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int barHeight = metrics.popupProgressBarHeight;
  const int barWidth =
      std::max(0, layout.width - metrics.popupMarginX * 2);  // twice the margin in drawPopup to match text width
  const int barX = layout.x + (layout.width - barWidth) / 2;
  const int barY = layout.y + layout.height - metrics.popupMarginY / 2 - barHeight / 2 - 1;
  if (barWidth <= 0 || barHeight <= 0) {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  const int scaledProgress = metrics.popupProgressClampPercent ? std::clamp(progress, 0, 100) : progress;
  const int fillWidth = barWidth * scaledProgress / 100;

  if (metrics.popupProgressDrawOutline) {
    renderer.drawRect(barX, barY, barWidth, barHeight, 1, metrics.popupProgressOutlineInverted);
  }
  if (fillWidth > 0) {
    renderer.fillRect(barX, barY, fillWidth, barHeight, metrics.popupProgressFillInverted);
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void BaseTheme::drawHelpText(const GfxRenderer& renderer, Rect rect, const char* label) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  auto truncatedLabel =
      renderer.truncatedText(SMALL_FONT_ID, label, rect.width - metrics.contentSidePadding * 2, EpdFontFamily::REGULAR);
  renderer.drawCenteredText(SMALL_FONT_ID, rect.y, truncatedLabel.c_str());
}

void BaseTheme::drawTextField(const GfxRenderer& renderer, Rect rect, const int textWidth, bool cursorMode,
                              int contentStartX, int contentWidth) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineY = rect.y + rect.height + lineHeight + metrics.verticalSpacing;
  const int thickness = cursorMode ? metrics.textFieldCursorThickness : metrics.textFieldNormalThickness;
  if (contentWidth > 0) {
    renderer.drawLine(rect.x + contentStartX, lineY,
                      rect.x + contentStartX + contentWidth + metrics.textFieldLineEndOffset, lineY, thickness, true);
  } else {
    const int lineW = textWidth + metrics.textFieldHorizontalPadding * 2;
    const int lineStart = rect.x + (rect.width - lineW) / 2;
    renderer.drawLine(lineStart, lineY, lineStart + lineW + metrics.textFieldLineEndOffset, lineY, thickness, true);
  }
}

void BaseTheme::drawOptionPopup(const GfxRenderer& renderer, const char* title, const std::vector<std::string>& options,
                                int selectedIndex, const std::vector<std::string>* infoLines) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const int optionFontId = metrics.optionPopupUseSmallFont ? UI_10_FONT_ID : UI_12_FONT_ID;
  const EpdFontFamily::Style optionStyle =
      metrics.optionPopupOptionFontBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;

  const int itemSpacing = metrics.optionPopupItemSpacing;
  const int innerPadding = metrics.optionPopupInnerPadding;
  const int selectionHPadding = metrics.optionPopupSelectionHPadding;
  const int selectionVPadding = metrics.optionPopupSelectionVPadding;

  const int optionLineHeight = renderer.getLineHeight(optionFontId);
  const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int rowHeight = optionLineHeight + selectionVPadding * 2;

  int maxTextWidth = renderer.getTextWidth(UI_12_FONT_ID, title, EpdFontFamily::BOLD);
  for (const auto& opt : options) {
    int w = renderer.getTextWidth(optionFontId, opt.c_str(), optionStyle);
    if (w > maxTextWidth) maxTextWidth = w;
  }

  const int infoCount = infoLines ? static_cast<int>(infoLines->size()) : 0;
  const int infoLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int infoHeight = infoCount > 0 ? infoCount * infoLineHeight + metrics.optionPopupTitleGap : 0;
  for (int i = 0; i < infoCount; i++) {
    const int w = renderer.getTextWidth(SMALL_FONT_ID, (*infoLines)[i].c_str());
    if (w > maxTextWidth) maxTextWidth = w;
  }

  const int optionCount = static_cast<int>(options.size());
  const int listHeight = rowHeight * optionCount + itemSpacing * (optionCount - 1);
  const int dialogW = std::min((maxTextWidth + innerPadding * 2 + selectionHPadding * 2) * 12 / 10,
                               pageWidth - metrics.optionPopupDialogSideMargin * 2);
  const int contentHeight = titleLineHeight + infoHeight + metrics.optionPopupTitleGap + listHeight;
  const int dialogH = contentHeight + innerPadding * 2;
  const int dialogX = (pageWidth - dialogW) / 2;
  const int dialogY = (pageHeight - dialogH) / 2;

  const int frameThickness = metrics.popupFrameThickness;
  const int frameRadius = metrics.popupCornerRadius;

  if (frameRadius > 0) {
    renderer.fillRoundedRect(dialogX - frameThickness, dialogY - frameThickness, dialogW + frameThickness * 2,
                             dialogH + frameThickness * 2, frameRadius + frameThickness, Color::White);
    renderer.fillRoundedRect(dialogX, dialogY, dialogW, dialogH, frameRadius, Color::Black);
    renderer.fillRoundedRect(dialogX + frameThickness, dialogY + frameThickness, dialogW - frameThickness * 2,
                             dialogH - frameThickness * 2,
                             frameRadius - frameThickness > 0 ? frameRadius - frameThickness : 0, Color::White);
  } else {
    renderer.fillRect(dialogX - frameThickness, dialogY - frameThickness, dialogW + frameThickness * 2,
                      dialogH + frameThickness * 2, true);
    renderer.fillRect(dialogX, dialogY, dialogW, dialogH, false);
  }

  int y = dialogY + innerPadding;

  // Title can be a raw filename; keep it inside the dialog.
  const int maxContentWidth = dialogW - innerPadding * 2;
  const std::string safeTitle = renderer.truncatedText(UI_12_FONT_ID, title, maxContentWidth, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_12_FONT_ID, y, safeTitle.c_str(), true, EpdFontFamily::BOLD);
  y += titleLineHeight;

  if (infoCount > 0) {
    for (int i = 0; i < infoCount; i++) {
      renderer.drawCenteredText(SMALL_FONT_ID, y, (*infoLines)[i].c_str(), true);
      y += infoLineHeight;
    }
    y += metrics.optionPopupTitleGap;
  }

  if (metrics.optionPopupTitleSeparator) {
    const int sepY = y + metrics.optionPopupTitleGap / 2;
    renderer.drawLine(dialogX + innerPadding, sepY, dialogX + dialogW - innerPadding, sepY, true);
  }

  y += metrics.optionPopupTitleGap;

  const int itemRectX = dialogX + innerPadding;
  const int itemRectW = dialogW - innerPadding * 2;
  const int selectionRadius = metrics.optionPopupSelectionRadius;

  const int maxOptionWidth = itemRectW - selectionHPadding * 2;
  for (int i = 0; i < optionCount; i++) {
    const int itemY = y + i * (rowHeight + itemSpacing);
    const bool selected = (i == selectedIndex);
    const std::string safeLabel = renderer.truncatedText(optionFontId, options[i].c_str(), maxOptionWidth, optionStyle);
    const char* labelText = safeLabel.c_str();

    if (metrics.optionPopupDrawAllRows || selected) {
      Color rowColor;
      if (selected) {
        rowColor = metrics.optionPopupSelectionLight ? Color::LightGray : Color::Black;
      } else {
        rowColor = Color::White;
      }
      if (selectionRadius > 0) {
        renderer.fillRoundedRect(itemRectX, itemY, itemRectW, rowHeight, selectionRadius, rowColor);
      } else {
        renderer.fillRect(itemRectX, itemY, itemRectW, rowHeight, rowColor == Color::Black);
      }
    }

    const int textW = renderer.getTextWidth(optionFontId, labelText, optionStyle);
    const int textY = itemY + (rowHeight - optionLineHeight) / 2;
    const int textX = itemRectX + (itemRectW - textW) / 2;
    // Unselected items: text is dark (invert=true means draw on white bg).
    // Selected on dark bg: text must be white (invert=false).
    // Selected on light bg: text stays dark (invert=true).
    const bool invertText = selected ? metrics.optionPopupSelectionLight : true;
    renderer.drawText(optionFontId, textX, textY, labelText, invertText, optionStyle);
  }
}
