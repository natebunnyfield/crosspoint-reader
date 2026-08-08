#include "XtcReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

int XtcReaderChapterSelectionActivity::getPageItems() const {
  return UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);
}

int XtcReaderChapterSelectionActivity::findChapterIndexForPage(uint32_t page) const {
  if (!xtc) {
    return 0;
  }

  const auto& chapters = xtc->getChapters();
  for (size_t i = 0; i < chapters.size(); i++) {
    if (page >= chapters[i].startPage && page <= chapters[i].endPage) {
      return static_cast<int>(i);
    }
  }
  return 0;
}

void XtcReaderChapterSelectionActivity::onEnter() {
  Activity::onEnter();

  if (!xtc) {
    return;
  }

  selectorIndex = findChapterIndexForPage(currentPage);

  requestUpdate();
}

void XtcReaderChapterSelectionActivity::onExit() { Activity::onExit(); }

void XtcReaderChapterSelectionActivity::loop() {
  const int pageItems = getPageItems();
  const int totalItems = static_cast<int>(xtc->getChapters().size());

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  auto selectChapter = [this] {
    const auto& chapters = xtc->getChapters();
    if (!chapters.empty() && selectorIndex >= 0 && selectorIndex < static_cast<int>(chapters.size())) {
      setResult(PageResult{chapters[selectorIndex].startPage});
      finish();
    }
  };

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
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
}

void XtcReaderChapterSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_SELECT_CHAPTER));

  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;

  const auto& chapters = xtc->getChapters();
  if (chapters.empty()) {
    renderer.drawText(UI_10_FONT_ID, screen.x + metrics.contentSidePadding, contentTop + 20, tr(STR_NO_CHAPTERS));
  } else {
    GUI.drawList(renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, static_cast<int>(chapters.size()),
                 selectorIndex, [this](int index) {
                   const auto& item = xtc->getChapters()[index];
                   return item.name.empty() ? std::string(tr(STR_UNNAMED)) : item.name;
                 });
  }

  // Orientation is fixed at Portrait (GfxRenderer::orientation is a
  // constexpr), so the old LandscapeClockwise hint-suppression branch was
  // already dead; drawButtonHints is unconditional, matching
  // EpubReaderChapterSelectionActivity.
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
