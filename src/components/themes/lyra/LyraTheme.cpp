#include "LyraTheme.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/lucide_icons.h"
#include "fontIds.h"

// Internal constants
namespace {
constexpr int hPaddingInSelection = 8;
constexpr int cornerRadius = 6;
constexpr int topHintButtonY = 345;
constexpr int maxListValueWidth = 200;
constexpr int mainMenuIconSize = 32;
constexpr int listIconSize = 24;
constexpr int mainMenuColumns = 2;
// A list row's first subtitle line, measured from the row's top. The selection
// band is sized from this and the subtitle is drawn at it; they have to be the
// same number or the band marks lines that are not where it thinks they are.
constexpr int kSubtitleTopOffset = 30;
// Breathing room the selection band leaves above the title and below the last
// subtitle line. Asymmetric on purpose: the title's own glyphs start a few
// pixels below its draw origin, so an equal pad reads as bottom-heavy.
constexpr int kSelectionPadTop = 2;
constexpr int kSelectionPadBottom = 5;
int coverWidth = 0;

const uint8_t* iconForName(UIIcon icon, int size) {
  // Every UIIcon now exists at both sizes, so the 24px table no longer has to
  // borrow a neighboring glyph -- ManageFiles used to fall back to the plain
  // file icon at list size because there was no 24px cut of folder-cog.
  //
  // Bitmaps come from scripts/gen_lucide_icons.py and are stored pre-rotated to
  // cancel drawIcon's own 90 degree mapping. Do not hand-edit them.
  const bool small = size == 24;
  switch (icon) {
    case UIIcon::Folder:
      return small ? Folder24LucideIcon : Folder32LucideIcon;
    case UIIcon::Text:
      return small ? Text24LucideIcon : Text32LucideIcon;
    case UIIcon::Image:
      return small ? Image24LucideIcon : Image32LucideIcon;
    case UIIcon::Book:
      return small ? Book24LucideIcon : Book32LucideIcon;
    case UIIcon::File:
      return small ? File24LucideIcon : File32LucideIcon;
    case UIIcon::Recent:
      return small ? Recent24LucideIcon : Recent32LucideIcon;
    case UIIcon::Settings:
      return small ? Settings24LucideIcon : Settings32LucideIcon;
    case UIIcon::Transfer:
      return small ? Transfer24LucideIcon : Transfer32LucideIcon;
    case UIIcon::Library:
      return small ? Library24LucideIcon : Library32LucideIcon;
    case UIIcon::Wifi:
      return small ? Wifi24LucideIcon : Wifi32LucideIcon;
    case UIIcon::Hotspot:
      return small ? Hotspot24LucideIcon : Hotspot32LucideIcon;
    case UIIcon::ManageFiles:
      return small ? ManageFiles24LucideIcon : ManageFiles32LucideIcon;
    case UIIcon::CreateNote:
      return small ? CreateNote24LucideIcon : CreateNote32LucideIcon;
    case UIIcon::ClaudeMark:
      return small ? ClaudeMark24LucideIcon : ClaudeMark32LucideIcon;
    case UIIcon::None:
      break;
  }
  return nullptr;
}
}  // namespace

void LyraTheme::fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const {
  const bool charging = gpio.isUsbConnected();

  if (charging) {
    // Solid fill when charging so lightning bolt is visible
    renderer.fillRect(rect.x + 2, rect.y + 2, rect.width - 5, rect.height - 4);
    drawBatteryLightningBolt(renderer, rect.x + 4, rect.y + 2);
  } else {
    if (percentage > 10) {
      renderer.fillRect(rect.x + 2, rect.y + 2, 3, rect.height - 4);
    }
    if (percentage > 40) {
      renderer.fillRect(rect.x + 6, rect.y + 2, 3, rect.height - 4);
    }
    if (percentage > 70) {
      renderer.fillRect(rect.x + 10, rect.y + 2, 3, rect.height - 4);
    }
  }
}

void LyraTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  // Position icon at right edge, drawBatteryRight will place text to the left
  const int batteryX = rect.x + rect.width - 12 - LyraMetrics::values.batteryWidth;
  drawBatteryRight(renderer,
                   Rect{batteryX, rect.y + 5, LyraMetrics::values.batteryWidth, LyraMetrics::values.batteryHeight},
                   showBatteryPercentage);

  int maxTitleWidth = title != nullptr ? renderer.getTextWidth(UI_12_FONT_ID, title, EpdFontFamily::BOLD) : 0;
  int maxSubtitleWidth =
      subtitle != nullptr ? renderer.getTextWidth(SMALL_FONT_ID, subtitle, EpdFontFamily::REGULAR) : 0;

  // Available space is the distance between the side paddings, and a with side padding between title and subtitle.
  const int availableSpace = rect.width - LyraMetrics::values.contentSidePadding * 3;

  if (maxTitleWidth + maxSubtitleWidth > availableSpace) {
    if ((maxTitleWidth > availableSpace / 2) && (maxSubtitleWidth > availableSpace / 2)) {
      // Both are wider then half the space, truncate both.
      maxTitleWidth = availableSpace / 2;
      maxSubtitleWidth = availableSpace / 2;
    } else {
      // Truncate the the longest one
      if (maxTitleWidth > maxSubtitleWidth) {
        maxTitleWidth = availableSpace - maxSubtitleWidth;
      } else {
        maxSubtitleWidth = availableSpace - maxTitleWidth;
      }
    }
  }

  if (title) {
    auto truncatedTitle = renderer.truncatedText(UI_12_FONT_ID, title, maxTitleWidth, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, rect.x + LyraMetrics::values.contentSidePadding,
                      rect.y + LyraMetrics::values.batteryBarHeight + 3, truncatedTitle.c_str(), true,
                      EpdFontFamily::BOLD);
    renderer.drawLine(rect.x, rect.y + rect.height - 3, rect.x + rect.width - 1, rect.y + rect.height - 3, 3, true);
  }

  if (subtitle) {
    auto truncatedSubtitle = renderer.truncatedText(SMALL_FONT_ID, subtitle, maxSubtitleWidth, EpdFontFamily::REGULAR);
    int truncatedSubtitleWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedSubtitle.c_str());
    renderer.drawText(SMALL_FONT_ID,
                      rect.x + rect.width - LyraMetrics::values.contentSidePadding - truncatedSubtitleWidth,
                      rect.y + 50, truncatedSubtitle.c_str(), true);
  }
}

void LyraTheme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label, const char* rightLabel) const {
  int currentX = rect.x + LyraMetrics::values.contentSidePadding;
  int rightSpace = LyraMetrics::values.contentSidePadding;
  if (rightLabel) {
    auto truncatedRightLabel =
        renderer.truncatedText(SMALL_FONT_ID, rightLabel, maxListValueWidth, EpdFontFamily::REGULAR);
    int rightLabelWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedRightLabel.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - LyraMetrics::values.contentSidePadding - rightLabelWidth,
                      rect.y + 7, truncatedRightLabel.c_str());
    rightSpace += rightLabelWidth + hPaddingInSelection;
  }

  auto truncatedLabel = renderer.truncatedText(
      UI_10_FONT_ID, label, rect.width - LyraMetrics::values.contentSidePadding - rightSpace, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, currentX, rect.y + 6, truncatedLabel.c_str(), true, EpdFontFamily::REGULAR);

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

int LyraTheme::getListRowStep(bool hasSubtitle, int subtitleLines) const {
  if (!hasSubtitle) return LyraMetrics::values.listRowHeight;
  const int extraLines = std::max(0, subtitleLines - 1);
  return LyraMetrics::values.listWithSubtitleRowHeight + extraLines * LyraMetrics::values.listSubtitleLineStep;
}

int LyraTheme::getListPageItems(int contentHeight, bool hasSubtitle, int subtitleLines) const {
  const int rowStep = getListRowStep(hasSubtitle, subtitleLines);
  if (rowStep <= 0) return 1;
  return std::max(1, contentHeight / rowStep);
}

void LyraTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                         const std::function<std::string(int index)>& rowTitle,
                         const std::function<std::string(int index)>& rowSubtitle,
                         const std::function<UIIcon(int index)>& rowIcon,
                         const std::function<std::string(int index)>& rowValue, bool highlightValue,
                         const std::function<bool(int index)>& rowDimmed, int subtitleLines) const {
  int rowHeight = getListRowStep(rowSubtitle != nullptr, subtitleLines);
  int pageItems = rowHeight > 0 ? std::max(1, rect.height / rowHeight) : 1;

  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  if (totalPages > 1) {
    const int scrollAreaHeight = rect.height;

    // Draw scroll bar
    const int scrollBarHeight = (scrollAreaHeight * pageItems) / itemCount;
    const int currentPage = selectedIndex / pageItems;
    const int scrollBarY = rect.y + ((scrollAreaHeight - scrollBarHeight) * currentPage) / (totalPages - 1);
    const int scrollBarX = rect.x + rect.width - LyraMetrics::values.scrollBarRightOffset;
    renderer.drawLine(scrollBarX, rect.y, scrollBarX, rect.y + scrollAreaHeight, true);
    renderer.fillRect(scrollBarX - LyraMetrics::values.scrollBarWidth, scrollBarY, LyraMetrics::values.scrollBarWidth,
                      scrollBarHeight, true);
  }

  // Draw selection
  int contentWidth =
      rect.width -
      (totalPages > 1 ? (LyraMetrics::values.scrollBarWidth + LyraMetrics::values.scrollBarRightOffset) : 1);
  int textX = rect.x + LyraMetrics::values.contentSidePadding + hPaddingInSelection;
  int textWidth = contentWidth - LyraMetrics::values.contentSidePadding * 2 - hPaddingInSelection * 2;
  int iconSize;
  if (rowIcon != nullptr) {
    iconSize = (rowSubtitle != nullptr) ? mainMenuIconSize : listIconSize;
    textX += iconSize + hPaddingInSelection;
    textWidth -= iconSize + hPaddingInSelection;
  }

  // The subtitle lines a row actually draws, in draw order. The selection
  // highlight below sizes itself to these and the draw loop renders them, so
  // the band cannot disagree with the text it sits behind — the two used to be
  // derived separately, which is how a band ended up marking the wrong lines.
  const auto subtitleLinesFor = [&](int index) {
    std::vector<std::string> out;
    if (rowSubtitle == nullptr || subtitleLines <= 1) return out;
    const std::string text = rowSubtitle(index);
    size_t segStart = 0;
    while (segStart <= text.size() && static_cast<int>(out.size()) < subtitleLines) {
      const size_t nl = text.find('\n', segStart);
      const std::string segment = text.substr(segStart, nl == std::string::npos ? std::string::npos : nl - segStart);
      // An EMPTY segment is a deliberate blank line — the font colophon puts one
      // between an originator and the person who digitised their work. It has to
      // be emitted directly: wrappedText() has no words to lay out and returns
      // nothing, which would silently close the gap the blank exists to open.
      if (segment.empty()) {
        out.push_back(std::string());
        if (nl == std::string::npos) break;
        segStart = nl + 1;
        continue;
      }
      const auto wrapped =
          renderer.wrappedText(SMALL_FONT_ID, segment.c_str(), textWidth, subtitleLines - static_cast<int>(out.size()));
      for (const auto& line : wrapped) {
        if (static_cast<int>(out.size()) >= subtitleLines) break;
        out.push_back(line);
      }
      if (nl == std::string::npos) break;
      segStart = nl + 1;
    }
    return out;
  };

  // Where a row's subtitle block STARTS, given how many lines it actually has.
  // The block is sized for the worst case in the list — four lineage stages in
  // the font picker — so a family with fewer draws BOTTOM-ALIGNED within it: a
  // one-line colophon lands on the last reserved line, a two-liner on the
  // bottom two, and a four-liner fills the block exactly as before (owner
  // ruling 2026-08-13, docs/ui-conventions.md).
  //
  // Both the selection band and the draw loop go through this, so the band
  // cannot mark lines the text does not occupy — the failure the band already
  // shipped once, when its geometry was derived separately.
  const auto subtitleTopFor = [&](int lineCount) {
    return kSubtitleTopOffset + std::max(0, subtitleLines - lineCount) * LyraMetrics::values.listSubtitleLineStep;
  };

  // Draw selection
  if (selectedIndex >= 0) {
    // The band hugs the selected row's CONTENT, not its box. A list sizes every
    // row for its longest entry — four colophon lines in the font picker — so a
    // band cut to the row leaves a two-line family sitting in 40 px of empty
    // grey, and a band of fixed height leaves the rest of a tall row outside it.
    //
    // Centring a fixed 60 px band is what shipped before, and it broke the
    // moment kColophonLines went 2 -> 4: the centring offset grew from 9 px to
    // 27 px and the band started BELOW the family name it exists to mark.
    //
    // Row height, text layout, and the hit target are unchanged either way.
    const int selRow = rect.y + selectedIndex % pageItems * rowHeight;
    const int selLines = static_cast<int>(subtitleLinesFor(selectedIndex).size());
    int highlightH;
    int highlightY;
    if (selLines > 0) {
      // Bottom-aligned subtitles mean the row's content now runs from its title
      // down to the LAST reserved line whatever the row's own line count, so the
      // band reaches the bottom of the block rather than stopping under a short
      // colophon. Expressed through subtitleTopFor() rather than as
      // `subtitleLines - 1` so it stays tied to where the text actually starts.
      highlightH = std::min(subtitleTopFor(selLines) + (selLines - 1) * LyraMetrics::values.listSubtitleLineStep +
                                renderer.getLineHeight(SMALL_FONT_ID) + kSelectionPadBottom,
                            rowHeight - kSelectionPadTop);
      highlightY = selRow + kSelectionPadTop;
    } else {
      // Every list that does NOT ask for multiple subtitle lines keeps the
      // original geometry exactly — a full-height band, centred. Those rows are
      // sized to their content already, so there is nothing to hug, and moving
      // them even 2 px would retune every screen in the firmware to fix one.
      highlightH = std::min(rowHeight, LyraMetrics::values.listRowHeight * 3 / 2);
      highlightY = selRow + (rowHeight - highlightH) / 2;
    }
    renderer.fillRoundedRect(rect.x + LyraMetrics::values.contentSidePadding, highlightY,
                             contentWidth - LyraMetrics::values.contentSidePadding * 2, highlightH, cornerRadius,
                             Color::LightGray);
  }

  // Draw all items
  const auto pageStartIndex = selectedIndex / pageItems * pageItems;

  int iconY = (rowSubtitle != nullptr) ? 16 : 10;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    const int itemY = rect.y + (i % pageItems) * rowHeight;
    int rowTextWidth = textWidth;

    int valueWidth = 0;
    std::string valueText = "";
    if (rowValue != nullptr) {
      valueText = rowValue(i);
      valueText = renderer.truncatedText(UI_10_FONT_ID, valueText.c_str(), maxListValueWidth);
      valueWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str()) + hPaddingInSelection;
    }

    // The value badge OVERLAPS the row text; it does NOT reserve a column out of
    // it (owner ruling: do not reflow the text when a row is Selected -- overlap
    // instead, the same call as the keyboard overlapping the page rather than
    // shrinking it). Reserving the badge's width per row made a row's wrap points
    // depend on whether it carried a badge, so the "Selected" pill -- which marks
    // the APPLIED font, not the cursor -- re-wrapped the title and the two-line
    // colophon under it. Text keeps its full width; the badge paints its own
    // opaque backing below and outlines whatever it covers.

    auto itemName = rowTitle(i);
    auto item = renderer.truncatedText(UI_10_FONT_ID, itemName.c_str(), rowTextWidth);
    renderer.drawText(UI_10_FONT_ID, textX, itemY + 7, item.c_str(), true);
    // Rightmost pixel any of this row's text reaches. Only used to decide
    // whether the badge below is actually covering something.
    int textRight = textX + renderer.getTextWidth(UI_10_FONT_ID, item.c_str());

    // Apply checkerboard dither to create gray text effect for dimmed items
    if (rowDimmed && rowDimmed(i) && i != selectedIndex) {
      const int titleWidth = renderer.getTextWidth(UI_10_FONT_ID, item.c_str());
      const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
      for (int py = itemY + 7; py < itemY + 7 + lineH; py++)
        for (int px = textX; px < textX + titleWidth; px++)
          if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
    }

    if (rowIcon != nullptr) {
      UIIcon icon = rowIcon(i);
      const uint8_t* iconBitmap = iconForName(icon, iconSize);
      if (iconBitmap != nullptr) {
        renderer.drawIcon(iconBitmap, rect.x + LyraMetrics::values.contentSidePadding + hPaddingInSelection,
                          itemY + iconY, iconSize);
      }
    }

    if (rowSubtitle != nullptr) {
      // Draw subtitle. subtitleLines == 1 keeps the original single truncated
      // line, so lists that never ask for more render exactly as before.
      std::string subtitleText = rowSubtitle(i);
      if (subtitleLines <= 1) {
        auto subtitle = renderer.truncatedText(SMALL_FONT_ID, subtitleText.c_str(), rowTextWidth);
        renderer.drawText(SMALL_FONT_ID, textX, itemY + kSubtitleTopOffset, subtitle.c_str(), true);
        textRight = std::max(textRight, textX + renderer.getTextWidth(SMALL_FONT_ID, subtitle.c_str()));
      } else {
        // A newline in the subtitle is an EXPLICIT line break, not whitespace:
        // the font picker pairs each lineage stage with its own designer
        // ("Original author · YEAR PLACE" over "Digitizer · YEAR PLACE"), and
        // word-wrapping that as one run puts the break wherever the words run
        // out — mid-stage, splitting a year from its place. subtitleLinesFor()
        // above does the segmenting; the selection band measures the same call,
        // so the band and these lines cannot drift apart.
        const auto subtitleTextLines = subtitleLinesFor(i);
        int subtitleY = itemY + subtitleTopFor(static_cast<int>(subtitleTextLines.size()));
        for (const auto& line : subtitleTextLines) {
          renderer.drawText(SMALL_FONT_ID, textX, subtitleY, line.c_str(), true);
          textRight = std::max(textRight, textX + renderer.getTextWidth(SMALL_FONT_ID, line.c_str()));
          subtitleY += LyraMetrics::values.listSubtitleLineStep;
        }
      }
    }

    // Draw value
    if (!valueText.empty()) {
      // ALWAYS filled, even when the badge is not the highlighted row's: the
      // fill is what makes the overlay opaque. Without it the label would be
      // drawn straight on top of the colophon line running underneath.
      const bool inverted = (i == selectedIndex && highlightValue);
      const Color badgeFill = inverted ? Color::Black : (i == selectedIndex) ? Color::LightGray : Color::White;
      const int badgeX =
          rect.x + contentWidth - LyraMetrics::values.contentSidePadding - hPaddingInSelection - valueWidth;
      const int badgeW = valueWidth + hPaddingInSelection;
      // The pill is the SAME compact bar as the selection highlight, not the full
      // row height. On a multi-line row (the font picker's two-line colophon makes
      // the row 78px tall) a full-height pill juts past the 60px highlight top and
      // bottom; matching the highlight's height and vertical centring keeps the two
      // one bar. Same expression the highlight uses above: 1.5x the base
      // single-line row, clamped so a short row is unaffected.
      const int badgeH = std::min(rowHeight, LyraMetrics::values.listRowHeight * 3 / 2);
      const int badgeY = itemY + (rowHeight - badgeH) / 2;
      renderer.fillRoundedRect(badgeX, badgeY, badgeW, badgeH, cornerRadius, badgeFill);
      // Outline ONLY when the badge actually covers text. The boundary is what
      // stops a covered line from reading as a broken render — it just stops
      // mid-word otherwise. On a settings row, where the value sits in clear
      // space past a short label, an outline would be a box around nothing.
      if (!inverted && textRight > badgeX) {
        renderer.drawRoundedRect(badgeX, badgeY, badgeW, badgeH, 1, cornerRadius, true);
      }

      // Centred in the pill (same centre as the row, since the pill is centred in
      // the row), not pinned near its top.
      //
      // Centre THIS LABEL'S OWN INK, not the font's theoretical box. drawText()
      // takes the top of the ascender box and adds the ascender to reach the
      // baseline (GfxRenderer.cpp:659), so subtracting getTextHeight() — the
      // ascender alone — parked the label half a descender LOW, and subtracting
      // ascender+descender parked it high by whatever descender depth the word
      // does not use ("Selected" has no descenders at all). Measuring the drawn
      // string lands it on the pill's centre either way. Both errors were
      // invisible on a tall row and obvious once the rows became a single line.
      int inkTop = 0;
      int inkBottom = 0;
      int valueY;
      if (renderer.getTextInkBounds(UI_10_FONT_ID, valueText.c_str(), inkTop, inkBottom)) {
        valueY = itemY + (rowHeight - (inkBottom - inkTop)) / 2 - inkTop;
      } else {
        valueY = itemY + (rowHeight - renderer.getFontAscenderSize(UI_10_FONT_ID)) / 2;
      }
      renderer.drawText(UI_10_FONT_ID, rect.x + contentWidth - LyraMetrics::values.contentSidePadding - valueWidth,
                        valueY, valueText.c_str(), !(i == selectedIndex && highlightValue));
    }
  }
}

void LyraTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                const char* btn4) const {
  if (gpio.hasTouch()) {
    return;
  }

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 80;
  constexpr int smallButtonHeight = 15;
  constexpr int buttonHeight = LyraMetrics::values.buttonHintsHeight;
  constexpr int buttonY = LyraMetrics::values.buttonHintsHeight;  // Distance from bottom
  constexpr int textYOffset = 7;                                  // Distance from top of button to text baseline
  // X3 has wider screen in portrait (528 vs 480), use more spacing
  constexpr int x4ButtonPositions[] = {58, 146, 254, 342};
  constexpr int x3ButtonPositions[] = {65, 157, 291, 383};
  const int* buttonPositions = gpio.deviceIsX3() ? x3ButtonPositions : x4ButtonPositions;
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    const int x = buttonPositions[i];
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
      // A label wider than the standard chip ("Open/Menu", long translations)
      // grows its chip symmetrically around the physical button position
      // instead of bleeding over the chip border.
      const int chipWidth = std::max(buttonWidth, textWidth + 10);
      const int chipX = x - (chipWidth - buttonWidth) / 2;
      // Draw the filled background and border for a FULL-sized button
      renderer.fillRoundedRect(chipX, pageHeight - buttonY, chipWidth, buttonHeight, cornerRadius, Color::White);
      renderer.drawRoundedRect(chipX, pageHeight - buttonY, chipWidth, buttonHeight, 1, cornerRadius, true, true, false,
                               false, true);
      const int textX = chipX + (chipWidth - 1 - textWidth) / 2;
      renderer.drawText(SMALL_FONT_ID, textX, pageHeight - buttonY + textYOffset, labels[i]);
    } else {
      // Draw the filled background and border for a SMALL-sized button
      renderer.fillRoundedRect(x, pageHeight - smallButtonHeight, buttonWidth, smallButtonHeight, cornerRadius,
                               Color::White);
      renderer.drawRoundedRect(x, pageHeight - smallButtonHeight, buttonWidth, smallButtonHeight, 1, cornerRadius, true,
                               true, false, false, true);
    }
  }
}

void LyraTheme::drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const {
#ifdef CROSSPOINT_NO_PHYSICAL_SIDE_BUTTONS
  // See BaseTheme::drawSideButtonHints -- same ruling, same reason.
  (void)renderer;
  (void)topBtn;
  (void)bottomBtn;
  return;
#endif
  if (gpio.hasTouch()) {
    return;
  }

  const int screenWidth = renderer.getScreenWidth();
  constexpr int buttonWidth = LyraMetrics::values.sideButtonHintsWidth;  // Width on screen (height when rotated)
  constexpr int buttonHeight = 78;                                       // Height on screen (width when rotated)
  constexpr int buttonMargin = 0;

  if (gpio.deviceIsX3()) {
    // X3 layout: Up on left side, Down on right side, positioned higher
    constexpr int x3ButtonY = 155;

    if (topBtn != nullptr && topBtn[0] != '\0') {
      renderer.drawRoundedRect(buttonMargin, x3ButtonY, buttonWidth, buttonHeight, 1, cornerRadius, false, true, false,
                               true, true);
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, topBtn);
      renderer.drawTextRotated90CW(SMALL_FONT_ID, buttonMargin, x3ButtonY + (buttonHeight + textWidth) / 2, topBtn);
    }

    if (bottomBtn != nullptr && bottomBtn[0] != '\0') {
      const int rightX = screenWidth - buttonWidth;
      renderer.drawRoundedRect(rightX, x3ButtonY, buttonWidth, buttonHeight, 1, cornerRadius, true, false, true, false,
                               true);
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, bottomBtn);
      renderer.drawTextRotated90CW(SMALL_FONT_ID, rightX, x3ButtonY + (buttonHeight + textWidth) / 2, bottomBtn);
    }
  } else {
    // X4 layout: Both buttons stacked on right side
    const char* labels[] = {topBtn, bottomBtn};
    const int x = screenWidth - buttonWidth;

    if (topBtn != nullptr && topBtn[0] != '\0') {
      renderer.drawRoundedRect(x, topHintButtonY, buttonWidth, buttonHeight, 1, cornerRadius, true, false, true, false,
                               true);
    }

    if (bottomBtn != nullptr && bottomBtn[0] != '\0') {
      renderer.drawRoundedRect(x, topHintButtonY + buttonHeight + 5, buttonWidth, buttonHeight, 1, cornerRadius, true,
                               false, true, false, true);
    }

    for (int i = 0; i < 2; i++) {
      if (labels[i] != nullptr && labels[i][0] != '\0') {
        const int y = topHintButtonY + (i * buttonHeight) + 5;
        const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
        renderer.drawTextRotated90CW(SMALL_FONT_ID, x, y + (buttonHeight + textWidth) / 2, labels[i]);
      }
    }
  }
}

void LyraTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                    const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                    bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  const int tileWidth = rect.width - 2 * LyraMetrics::values.contentSidePadding;
  const int tileHeight = rect.height;
  const int tileY = rect.y;
  const bool hasContinueReading = !recentBooks.empty();
  if (coverWidth == 0) {
    coverWidth = LyraMetrics::values.homeCoverHeight * 0.6;
  }

  // Draw book card regardless, fill with message based on `hasContinueReading`
  // Draw cover image as background if available (inside the box)
  // Only load from SD on first render, then use stored buffer
  if (hasContinueReading) {
    RecentBook book = recentBooks[0];
    if (!coverRendered) {
      std::string coverPath = book.coverBmpPath;
      bool hasCover = true;
      int tileX = LyraMetrics::values.contentSidePadding;
      if (coverPath.empty()) {
        hasCover = false;
      } else {
        const std::string coverBmpPath = UITheme::getCoverThumbPath(coverPath, LyraMetrics::values.homeCoverHeight);

        // First time: load cover from SD and render
        HalFile file;
        if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            coverWidth = bitmap.getWidth();
            renderer.drawBitmap(bitmap, tileX + hPaddingInSelection, tileY + hPaddingInSelection, coverWidth,
                                LyraMetrics::values.homeCoverHeight);
          } else {
            hasCover = false;
          }
          file.close();
        }
      }

      // Draw either way
      renderer.drawRect(tileX + hPaddingInSelection, tileY + hPaddingInSelection, coverWidth,
                        LyraMetrics::values.homeCoverHeight, true);

      if (!hasCover) {
        drawGeneratedCover(renderer,
                           Rect(tileX + hPaddingInSelection, tileY + hPaddingInSelection, coverWidth,
                                LyraMetrics::values.homeCoverHeight),
                           book.title.c_str(), book.author.c_str(),
                           static_cast<uint32_t>(std::hash<std::string>{}(book.path)));
      }

      coverBufferStored = storeCoverBuffer();
      coverRendered = coverBufferStored;  // Only consider it rendered if we successfully stored the buffer
    }

    bool bookSelected = (selectorIndex == 0);

    int tileX = LyraMetrics::values.contentSidePadding;
    int textWidth = tileWidth - 2 * hPaddingInSelection - LyraMetrics::values.verticalSpacing - coverWidth;

    if (bookSelected) {
      // Draw selection box
      renderer.fillRoundedRect(tileX, tileY, tileWidth, hPaddingInSelection, cornerRadius, true, true, false, false,
                               Color::LightGray);
      renderer.fillRectDither(tileX, tileY + hPaddingInSelection, hPaddingInSelection,
                              LyraMetrics::values.homeCoverHeight, Color::LightGray);
      renderer.fillRectDither(tileX + hPaddingInSelection + coverWidth, tileY + hPaddingInSelection,
                              tileWidth - hPaddingInSelection - coverWidth, LyraMetrics::values.homeCoverHeight,
                              Color::LightGray);
      renderer.fillRoundedRect(tileX, tileY + LyraMetrics::values.homeCoverHeight + hPaddingInSelection, tileWidth,
                               hPaddingInSelection, cornerRadius, false, false, true, true, Color::LightGray);
    }

    auto titleLines = renderer.wrappedText(UI_12_FONT_ID, book.title.c_str(), textWidth, 3, EpdFontFamily::BOLD);

    auto author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textWidth);
    const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int titleBlockHeight = titleLineHeight * static_cast<int>(titleLines.size());
    const int authorHeight = book.author.empty() ? 0 : (renderer.getLineHeight(UI_10_FONT_ID) * 3 / 2);
    const int totalBlockHeight = titleBlockHeight + authorHeight;
    int titleY = tileY + tileHeight / 2 - totalBlockHeight / 2;
    const int textX = tileX + hPaddingInSelection + coverWidth + LyraMetrics::values.verticalSpacing;
    for (const auto& line : titleLines) {
      renderer.drawText(UI_12_FONT_ID, textX, titleY, line.c_str(), true, EpdFontFamily::BOLD);
      titleY += titleLineHeight;
    }
    if (!book.author.empty()) {
      titleY += renderer.getLineHeight(UI_10_FONT_ID) / 2;
      renderer.drawText(UI_10_FONT_ID, textX, titleY, author.c_str(), true);
    }
  } else {
    drawEmptyRecents(renderer, rect);
  }
}

void LyraTheme::drawEmptyRecents(const GfxRenderer& renderer, const Rect rect) const {
  constexpr int padding = 48;
  renderer.drawText(UI_12_FONT_ID, rect.x + padding,
                    rect.y + rect.height / 2 - renderer.getLineHeight(UI_12_FONT_ID) - 2, tr(STR_NO_OPEN_BOOK), true,
                    EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, rect.x + padding, rect.y + rect.height / 2 + 2, tr(STR_START_READING), true);
}

void LyraTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                               const std::function<std::string(int index)>& buttonLabel,
                               const std::function<UIIcon(int index)>& rowIcon) const {
  // Rows must fit the rect they are handed. This used to lay out at a fixed
  // pitch from rect.y and never read rect.height, so a caller with less room
  // than buttonCount needs pushed its last row off the panel -- Home with no
  // recent books still reserves a full cover tile, which put "Settings" at
  // y=833 on an 800px screen and cost 1756 dropped-pixel ERR lines per paint
  // (B-012). Compress the gap first, then the tiles, so every row stays
  // reachable rather than silently vanishing.
  const int naturalPitch = LyraMetrics::values.menuRowHeight + LyraMetrics::values.menuSpacing;
  int rowHeight = LyraMetrics::values.menuRowHeight;
  int pitch = naturalPitch;
  if (buttonCount > 1 && rect.height > 0) {
    const int needed = (buttonCount - 1) * naturalPitch + rowHeight;
    if (needed > rect.height) {
      pitch = (rect.height - rowHeight) / (buttonCount - 1);
      if (pitch <= rowHeight) {
        pitch = rect.height / buttonCount;
        rowHeight = pitch > LyraMetrics::values.menuSpacing ? pitch - LyraMetrics::values.menuSpacing : pitch;
      }
    }
  }

  for (int i = 0; i < buttonCount; ++i) {
    int tileWidth = rect.width - LyraMetrics::values.contentSidePadding * 2;
    Rect tileRect = Rect{rect.x + LyraMetrics::values.contentSidePadding, rect.y + i * pitch, tileWidth, rowHeight};

    const bool selected = selectedIndex == i;

    if (selected) {
      renderer.fillRoundedRect(tileRect.x, tileRect.y, tileRect.width, tileRect.height, cornerRadius, Color::LightGray);
    }

    std::string labelStr = buttonLabel(i);
    const char* label = labelStr.c_str();
    int textX = tileRect.x + 16;
    const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int textY = tileRect.y + (rowHeight - lineHeight) / 2;

    if (rowIcon != nullptr) {
      UIIcon icon = rowIcon(i);
      const uint8_t* iconBitmap = iconForName(icon, mainMenuIconSize);
      if (iconBitmap != nullptr) {
        renderer.drawIcon(iconBitmap, textX, textY, mainMenuIconSize);
        textX += mainMenuIconSize + hPaddingInSelection + 2;
      }
    }

    renderer.drawText(UI_12_FONT_ID, textX, textY, label, true);
  }
}
