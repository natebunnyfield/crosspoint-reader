#pragma once
#include <GfxRenderer.h>

#include <string>
#include <utility>

#include "KeyboardEntryActivity.h"  // InputType
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Daisywheel text entry per docs/daisywheel.md (ruled 2026-08-04): two rings
// of 3-char petals around a hub preview. Front Left/Right rotate the focus
// (wrapping, auto-repeat); side Up / Confirm / side Down pick the top / middle
// / bottom char of the focused petal; long-press on a pick = uppercase and
// nothing else. The last petal of each ring is the utility petal (backspace /
// ring swap / OK, in that order -- backspace and OK deliberately non-adjacent).
// Back cancels the entry. Returns the same KeyboardResult contract as
// KeyboardEntryActivity so call sites can swap freely via the factory.
// Buttons only -- no touch handling (ruling 2026-08-05: this fork never
// supports touch or the X4 Pro).
class DaisyEntryActivity : public Activity {
 public:
  explicit DaisyEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title = "Enter Text",
                              std::string initialText = "", const size_t maxLength = 0,
                              InputType inputType = InputType::Text)
      : Activity("DaisyEntry", renderer, mappedInput),
        title(std::move(title)),
        text(std::move(initialText)),
        maxLength(maxLength),
        inputType(inputType) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string title;
  std::string text;
  size_t maxLength;
  InputType inputType;

  ButtonNavigator buttonNavigator;

  int ringIdx = 0;   // 0 = ABC ring, 1 = 123 ring
  int petalIdx = 0;  // focused petal, clockwise from 12 o'clock

  // One pick button (0 = top/Up, 1 = middle/Confirm, 2 = bottom/Down) is
  // tracked at a time: long-press fires uppercase at the threshold while held,
  // release before it commits the plain char.
  int activePick = -1;
  bool pickHandled = false;

  int petalCount() const;
  bool isUtilityPetal() const { return petalIdx == petalCount() - 1; }
  char slotChar(int petal, int slot) const;
  void rotate(int delta);
  void tapPick(int slot);
  void longPick(int slot);
  void swapRing();
  void insertChar(char c);
  void backspace();
  void slotCenter(int petal, int slot, int& outX, int& outY) const;

  void onComplete(std::string text);
  void onCancel();

  std::string displayText() const;

  static constexpr uint16_t LONG_PRESS_MS = 500;

  // Wheel geometry (portrait 480x800; header + field above, hints below).
  static constexpr int WHEEL_CX = 240;
  static constexpr int WHEEL_CY = 440;
  static constexpr int RADIUS_OUTER = 180;
  static constexpr int RADIUS_HUB = 78;
};
