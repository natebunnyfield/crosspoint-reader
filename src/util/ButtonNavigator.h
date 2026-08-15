#pragma once

#include <functional>
#include <vector>

#include "MappedInputManager.h"

class ButtonNavigator final {
  using Callback = std::function<void()>;
  using Buttons = std::vector<MappedInputManager::Button>;

  const uint16_t continuousStartMs;
  const uint16_t continuousIntervalMs;
  uint32_t lastContinuousNavTime = 0;
  // The page pair keeps its OWN repeat clock. onRelease() swallows a release
  // whenever lastContinuousNavTime is non-zero (that is how a hold does not
  // also fire a step on the way up), so sharing one timer would let a held
  // SIDE button eat the next FRONT button's step.
  uint32_t lastContinuousPageTime = 0;
  static const MappedInputManager* mappedInput;

  [[nodiscard]] bool shouldNavigateContinuously(uint32_t lastTime) const;
  void onPageContinuous(const Buttons& buttons, const Callback& callback);

 public:
  explicit ButtonNavigator(const uint16_t continuousIntervalMs = 500, const uint16_t continuousStartMs = 500)
      : continuousStartMs(continuousStartMs), continuousIntervalMs(continuousIntervalMs) {}

  static void setMappedInputManager(const MappedInputManager& mappedInputManager) { mappedInput = &mappedInputManager; }

  void onNext(const Callback& callback);
  void onPrevious(const Callback& callback);
  void onPressAndContinuous(const Buttons& buttons, const Callback& callback);

  void onNextPress(const Callback& callback);
  void onPreviousPress(const Callback& callback);
  void onPress(const Buttons& buttons, const Callback& callback);

  void onNextRelease(const Callback& callback);
  void onPreviousRelease(const Callback& callback);
  void onRelease(const Buttons& buttons, const Callback& callback);

  void onNextContinuous(const Callback& callback);
  void onPreviousContinuous(const Callback& callback);
  void onContinuous(const Buttons& buttons, const Callback& callback);

  // The SIDE pair: one whole screenful per press, repeating while held at the
  // same interval as the front pair. Wire these on every scrollable list screen
  // alongside the onNext/onPrevious step handlers above — the two pairs are
  // different actions now, not aliases (docs/ui-conventions.md, "Side buttons
  // should page, not repeat the front buttons").
  void onPageNext(const Callback& callback);
  void onPagePrevious(const Callback& callback);

  [[nodiscard]] static int nextIndex(int currentIndex, int totalItems);
  [[nodiscard]] static int previousIndex(int currentIndex, int totalItems);

  [[nodiscard]] static int nextPageIndex(int currentIndex, int totalItems, int itemsPerPage);
  [[nodiscard]] static int previousPageIndex(int currentIndex, int totalItems, int itemsPerPage);

  // Side-button paging arithmetic. Distinct from nextPageIndex/previousPageIndex
  // above, which wrap and snap to the page boundary and stay wired to the front
  // pair's hold-to-repeat:
  //
  //  * Moves by exactly itemsPerPage, so the drawn page advances by exactly one
  //    ((i + p) / p == i / p + 1 for any i) and the highlight keeps its position
  //    within the page — nothing is skipped and nothing is shown twice.
  //  * CLAMPS at both ends (ruling, 2026-08-14). The last page stays the last
  //    page; the press slides the selection onto the final/first row so the end
  //    of the list is perceptible instead of teleporting.
  //  * DEAD when the whole list fits one screen (ruling, 2026-08-14): a side
  //    button means "page", and where there is no page there is no action. It
  //    does NOT fall back to stepping one row.
  //
  // Returns false when nothing moved, so a held button on a short list costs
  // no redraw. Callers must only requestUpdate() on true.
  [[nodiscard]] static bool pageDown(int& index, int totalItems, int itemsPerPage);
  [[nodiscard]] static bool pageUp(int& index, int totalItems, int itemsPerPage);

  // Navigation uses the logical NavNext / NavPrevious buttons; MappedInputManager::mapButton resolves
  // them to physical buttons and applies any orientation-based direction swap, so this stays settings-free.
  [[nodiscard]] static Buttons getNextButtons() { return {MappedInputManager::Button::NavNext}; }
  [[nodiscard]] static Buttons getPreviousButtons() { return {MappedInputManager::Button::NavPrevious}; }
  [[nodiscard]] static Buttons getPageNextButtons() { return {MappedInputManager::Button::PageNext}; }
  [[nodiscard]] static Buttons getPagePreviousButtons() { return {MappedInputManager::Button::PagePrevious}; }
};
