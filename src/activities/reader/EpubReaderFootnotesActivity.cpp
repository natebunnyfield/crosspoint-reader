#include "EpubReaderFootnotesActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

EpubReaderFootnotesActivity::EpubReaderFootnotesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                         const std::vector<FootnoteEntry>& footnotes)
    : UiListActivity("EpubReaderFootnotes", renderer, mappedInput), footnotes(footnotes) {}

void EpubReaderFootnotesActivity::describeRow(const int index, ListRow& row) const {
  const auto& footnote = footnotes[index];
  row.title = footnote.number[0] ? footnote.number : tr(STR_LINK);
}

void EpubReaderFootnotesActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  // The activated row leaves this screen; a lingering flash would gray an
  // unrelated element on the next render.
  app.clearTapFlash();
  setResult(FootnoteResult{footnotes[index].href});
  finish();
}

bool EpubReaderFootnotesActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      mappedInput.wasReleased(MappedInputManager::Button::Power)) {
    activateIndex(nav.selected);
    return true;
  }

  return false;
}

void EpubReaderFootnotesActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - safe.height + metrics.verticalSpacing),
                  static_cast<int16_t>(safe.x)});
  // The list band ends where the pre-port screen's did: verticalSpacing above
  // the safe area's height (its contentHeight was safe.height - contentTop -
  // verticalSpacing).
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (footnotes.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, screen.body().y + 30, tr(STR_NO_FOOTNOTES));
    return;
  }

  drawRows(screen, ListLook{});
}

void EpubReaderFootnotesActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Header via GUI.drawHeader (already FreeInkUI-themed); the rest of the
  // screen renders through the app.
  GUI.drawHeader(renderer, Rect{safe.x, safe.y + metrics.topPadding, safe.width, metrics.headerHeight},
                 tr(STR_FOOTNOTES));
}

void EpubReaderFootnotesActivity::drawFooter() {
  const auto labels = footnotes.empty() ? mappedInput.mapLabels(tr(STR_BACK), "", "", "")
                                        : mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
