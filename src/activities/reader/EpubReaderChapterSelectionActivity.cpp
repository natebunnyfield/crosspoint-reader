#include "EpubReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

int EpubReaderChapterSelectionActivity::getTotalItems() const { return epub->getTocItemsCount(); }

void EpubReaderChapterSelectionActivity::onEnter() {
  Activity::onEnter();

  if (!epub) {
    return;
  }

  selectorIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (selectorIndex == -1) {
    selectorIndex = 0;
  }

  // Trigger first update
  requestUpdate();
}

void EpubReaderChapterSelectionActivity::onExit() { Activity::onExit(); }

void EpubReaderChapterSelectionActivity::loop() {
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);
  const int totalItems = getTotalItems();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  auto selectChapter = [this] {
    const auto tocItem = epub->getTocItem(selectorIndex);
    if (tocItem.spineIndex == -1) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
    } else {
      setResult(ChapterResult{tocItem.spineIndex, tocItem.anchor});
      finish();
    }
  };

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Keep in step with render(): the thin book-progress bar sits between the
  // header and the list, so the list starts one bar band lower.
  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing +
                         metrics.popupProgressBarHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;
  switch (handleListTouch(selectorIndex, totalItems, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      selectChapter();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    selectChapter();
  }

  buttonNavigator.onNextRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });

  // The SIDE pair pages by a whole screenful; the FRONT pair above steps one
  // row. They used to be the same action (docs/ui-conventions.md, "Side buttons
  // should page, not repeat the front buttons"). pageDown/pageUp clamp at the
  // ends and return false when nothing moved, so a short list costs no redraw.
  buttonNavigator.onPageNext([this, totalItems, pageItems] {
    if (!ButtonNavigator::pageDown(selectorIndex, totalItems, pageItems)) return;
    requestUpdate();
  });

  buttonNavigator.onPagePrevious([this, totalItems, pageItems] {
    if (!ButtonNavigator::pageUp(selectorIndex, totalItems, pageItems)) return;
    requestUpdate();
  });
}

void EpubReaderChapterSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_SELECT_CHAPTER));

  // Thin book-position bar (2026-08-22): the reader's byte-weighted progress
  // through the whole book, at the page the chapter screen was opened from.
  // Same outline-plus-fill idiom as BaseTheme::drawProgressBar, at the popup
  // bar's 4 px height and without the percent label — the list needs the room.
  const int barTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int barWidth = screen.width - metrics.contentSidePadding * 2;
  const int barHeight = metrics.popupProgressBarHeight;
  renderer.drawRect(screen.x + metrics.contentSidePadding, barTop, barWidth, barHeight);
  const float clamped = bookProgress < 0.0f ? 0.0f : (bookProgress > 1.0f ? 1.0f : bookProgress);
  const int fillWidth = static_cast<int>((barWidth - 2) * clamped);
  if (fillWidth > 0) {
    renderer.fillRect(screen.x + metrics.contentSidePadding + 1, barTop + 1, fillWidth, barHeight - 2);
  }

  const int contentTop = barTop + barHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;

  const int totalItems = getTotalItems();
  GUI.drawList(renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, totalItems, selectorIndex,
               [this](int index) {
                 auto item = epub->getTocItem(index);
                 std::string indent((item.level - 1) * 2, ' ');
                 return indent + item.title;
               });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
