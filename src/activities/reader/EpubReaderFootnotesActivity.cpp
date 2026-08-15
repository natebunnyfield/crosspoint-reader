#include "EpubReaderFootnotesActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void EpubReaderFootnotesActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void EpubReaderFootnotesActivity::onExit() { Activity::onExit(); }

void EpubReaderFootnotesActivity::loop() {
  auto selectFootnote = [this] {
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(footnotes.size())) {
      setResult(FootnoteResult{footnotes[selectedIndex].href});
      finish();
    }
  };

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      mappedInput.wasReleased(MappedInputManager::Button::Power)) {
    selectFootnote();
    return;
  }

  if (!footnotes.empty()) {
    auto metrics = UITheme::getInstance().getMetrics();
    Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;
    const int totalItems = static_cast<int>(footnotes.size());
    switch (handleListTouch(selectedIndex, totalItems, contentTop, contentHeight, false)) {
      case ListTouchResult::Activated:
        selectFootnote();
        return;
      case ListTouchResult::Consumed:
        return;
      case ListTouchResult::None:
        break;
    }

    const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);
    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Up) {
      selectedIndex = std::min(totalItems - 1, selectedIndex + pageItems);
      requestUpdate();
      return;
    }
    if (swipe == MappedInputManager::SwipeDir::Down) {
      selectedIndex = std::max(0, selectedIndex - pageItems);
      requestUpdate();
      return;
    }

    // The SIDE pair pages by a whole screenful; the FRONT pair below steps one
    // row. They used to be the same action (docs/ui-conventions.md, "Side
    // buttons should page, not repeat the front buttons"). This is the same
    // clamped arithmetic the swipes above already used, now shared.
    buttonNavigator.onPageNext([this, totalItems, pageItems] {
      if (!ButtonNavigator::pageDown(selectedIndex, totalItems, pageItems)) return;
      requestUpdate();
    });

    buttonNavigator.onPagePrevious([this, totalItems, pageItems] {
      if (!ButtonNavigator::pageUp(selectedIndex, totalItems, pageItems)) return;
      requestUpdate();
    });
  }

  buttonNavigator.onNext([this] {
    if (!footnotes.empty()) {
      selectedIndex = (selectedIndex + 1) % footnotes.size();
      requestUpdate();
    }
  });

  buttonNavigator.onPrevious([this] {
    if (!footnotes.empty()) {
      selectedIndex = (selectedIndex - 1 + footnotes.size()) % footnotes.size();
      requestUpdate();
    }
  });
}

void EpubReaderFootnotesActivity::render(RenderLock&&) {
  renderer.clearScreen();

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_FOOTNOTES));

  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;

  if (footnotes.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + 30, tr(STR_NO_FOOTNOTES));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  GUI.drawList(renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, static_cast<int>(footnotes.size()),
               selectedIndex, [this](int index) {
                 std::string label = footnotes[index].number;
                 return label.empty() ? std::string(tr(STR_LINK)) : label;
               });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
