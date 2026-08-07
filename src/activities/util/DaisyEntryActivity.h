#pragma once
#include <GfxRenderer.h>

#include <string>
#include <utility>

#include "KeyboardEntryActivity.h"  // InputType
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Daisywheel text entry per docs/daisywheel.md (ruled 2026-08-04): two rings
// of 3-char petals around an empty hub; the underlined text field above the
// wheel is the one text preview. Front Left/Right rotate the focus
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
  // When the ACTIVE pick button went down. MappedInputManager::getHeldTime()
  // takes no button and reports the longest-held button on the device, so
  // holding Left/Right to rotate -- which auto-repeats, i.e. holding is how the
  // wheel is meant to be driven -- already exceeded LONG_PRESS_MS before the
  // pick was even pressed, and every pick fired as uppercase.
  unsigned long pickPressedAt = 0;

  int petalCount() const;
  bool isUtilityPetal() const { return petalIdx == petalCount() - 1; }
  char slotChar(int petal, int slot) const;
  void rotate(int delta);
  void tapPick(int slot);
  void longPick(int slot);
  void swapRing();
  void insertChar(char c);
  void backspace();
  // Vertical distance between adjacent slot centers: the font's line advance,
  // clamped so the column fits the current ring's wedge at every angle.
  int slotSpacing() const;
  // Largest label font whose three-line column fits a wedge at the current
  // petal count; see the note on labelFontId() in the .cpp.
  int labelFontId() const;
  bool columnFits(int lineH) const;
  void slotCenter(int petal, int slot, int& outX, int& outY) const;

  void onComplete(std::string text);
  void onCancel();

  std::string displayText() const;

  static constexpr uint16_t LONG_PRESS_MS = 500;

  // Wheel geometry (portrait 480x800; header + field above, hints below).
  static constexpr int WHEEL_CX = 240;
  static constexpr int WHEEL_CY = 440;
  // Enlarged 2026-08-06 (owner ruling) so the 16-petal 123 ring can hold three
  // upright lines per wedge. The label radius rm = (HUB + OUTER)/2 sets the
  // tangential room a petal has, and at 16 petals the old 78/180 gave 25 px
  // against a 25 px line — the three slots drew straight through each other.
  // 150/230 lifts rm to 190 and the wedge to 37 px. OUTER is bounded by the
  // 480 px screen at WHEEL_CX 240, so this is close to the maximum.
  static constexpr int RADIUS_OUTER = 230;
  static constexpr int RADIUS_HUB = 150;
};
