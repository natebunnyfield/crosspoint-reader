#include "ButtonNavigator.h"

#include <algorithm>

const MappedInputManager* ButtonNavigator::mappedInput = nullptr;

void ButtonNavigator::onNext(const Callback& callback) {
  onNextPress(callback);
  onNextContinuous(callback);
}

void ButtonNavigator::onPrevious(const Callback& callback) {
  onPreviousPress(callback);
  onPreviousContinuous(callback);
}

void ButtonNavigator::onPressAndContinuous(const Buttons& buttons, const Callback& callback) {
  onPress(buttons, callback);
  onContinuous(buttons, callback);
}

void ButtonNavigator::onNextPress(const Callback& callback) { onPress(getNextButtons(), callback); }

void ButtonNavigator::onPreviousPress(const Callback& callback) { onPress(getPreviousButtons(), callback); }

void ButtonNavigator::onNextRelease(const Callback& callback) { onRelease(getNextButtons(), callback); }

void ButtonNavigator::onPreviousRelease(const Callback& callback) { onRelease(getPreviousButtons(), callback); }

void ButtonNavigator::onNextContinuous(const Callback& callback) { onContinuous(getNextButtons(), callback); }

void ButtonNavigator::onPreviousContinuous(const Callback& callback) { onContinuous(getPreviousButtons(), callback); }

void ButtonNavigator::onPress(const Buttons& buttons, const Callback& callback) {
  const bool wasPressed = std::any_of(buttons.begin(), buttons.end(), [](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->wasPressed(button);
  });

  if (wasPressed) {
    callback();
  }
}

void ButtonNavigator::onRelease(const Buttons& buttons, const Callback& callback) {
  const bool wasReleased = std::any_of(buttons.begin(), buttons.end(), [](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->wasReleased(button);
  });

  if (wasReleased) {
    if (lastContinuousNavTime == 0) {
      callback();
    }

    lastContinuousNavTime = 0;
  }
}

void ButtonNavigator::onContinuous(const Buttons& buttons, const Callback& callback) {
  const bool isPressed = std::any_of(buttons.begin(), buttons.end(), [this](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->isPressed(button) &&
           shouldNavigateContinuously(lastContinuousNavTime);
  });

  if (isPressed) {
    callback();
    lastContinuousNavTime = millis();
  }
}

void ButtonNavigator::onPageNext(const Callback& callback) {
  onPress(getPageNextButtons(), callback);
  onPageContinuous(getPageNextButtons(), callback);
}

void ButtonNavigator::onPagePrevious(const Callback& callback) {
  onPress(getPagePreviousButtons(), callback);
  onPageContinuous(getPagePreviousButtons(), callback);
}

void ButtonNavigator::onPageContinuous(const Buttons& buttons, const Callback& callback) {
  const bool isPressed = std::any_of(buttons.begin(), buttons.end(), [this](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->isPressed(button) &&
           shouldNavigateContinuously(lastContinuousPageTime);
  });

  if (isPressed) {
    callback();
    lastContinuousPageTime = millis();
  }
}

bool ButtonNavigator::shouldNavigateContinuously(const uint32_t lastTime) const {
  if (!mappedInput) return false;

  const bool buttonHeldLongEnough = mappedInput->getHeldTime() > continuousStartMs;
  const bool navigationIntervalElapsed = (millis() - lastTime) > continuousIntervalMs;

  return buttonHeldLongEnough && navigationIntervalElapsed;
}

int ButtonNavigator::nextIndex(const int currentIndex, const int totalItems) {
  if (totalItems <= 0) return 0;

  // Calculate the next index with wrap-around
  return (currentIndex + 1) % totalItems;
}

int ButtonNavigator::previousIndex(const int currentIndex, const int totalItems) {
  if (totalItems <= 0) return 0;

  // Calculate the previous index with wrap-around
  return (currentIndex + totalItems - 1) % totalItems;
}

int ButtonNavigator::nextPageIndex(const int currentIndex, const int totalItems, const int itemsPerPage,
                                   const bool wrap) {
  if (totalItems <= 0 || itemsPerPage <= 0) return 0;

  // When items fit on one page, use index navigation instead
  if (totalItems <= itemsPerPage) {
    if (!wrap) return currentIndex + 1 < totalItems ? currentIndex + 1 : totalItems - 1;
    return nextIndex(currentIndex, totalItems);
  }

  const int lastPageIndex = (totalItems - 1) / itemsPerPage;
  const int currentPageIndex = currentIndex / itemsPerPage;

  if (currentPageIndex < lastPageIndex) {
    return (currentPageIndex + 1) * itemsPerPage;
  }

  // On the last page: wrap to the top, or stop on the final row.
  return wrap ? 0 : totalItems - 1;
}

int ButtonNavigator::previousPageIndex(const int currentIndex, const int totalItems, const int itemsPerPage,
                                       const bool wrap) {
  if (totalItems <= 0 || itemsPerPage <= 0) return 0;

  // When items fit on one page, use index navigation instead
  if (totalItems <= itemsPerPage) {
    if (!wrap) return currentIndex > 0 ? currentIndex - 1 : 0;
    return previousIndex(currentIndex, totalItems);
  }

  const int lastPageIndex = (totalItems - 1) / itemsPerPage;
  const int currentPageIndex = currentIndex / itemsPerPage;

  if (currentPageIndex > 0) {
    return (currentPageIndex - 1) * itemsPerPage;
  }

  // On the first page: wrap to the last page, or stop on the first row.
  return wrap ? lastPageIndex * itemsPerPage : 0;
}

bool ButtonNavigator::pageDown(int& index, const int totalItems, const int itemsPerPage) {
  // Nothing to page: an empty list, a nonsense page size, or a list that fits
  // one screen (the side buttons are deliberately dead there, they do not fall
  // back to stepping a row). Returning false before any caller redraw is what
  // makes a held side button on a short list cost nothing.
  if (totalItems <= 0 || itemsPerPage <= 0 || totalItems <= itemsPerPage) return false;

  const int next = std::min(totalItems - 1, index + itemsPerPage);
  if (next == index) return false;  // already on the last row
  index = next;
  return true;
}

bool ButtonNavigator::pageUp(int& index, const int totalItems, const int itemsPerPage) {
  if (totalItems <= 0 || itemsPerPage <= 0 || totalItems <= itemsPerPage) return false;

  const int previous = std::max(0, index - itemsPerPage);
  if (previous == index) return false;  // already on the first row
  index = previous;
  return true;
}
