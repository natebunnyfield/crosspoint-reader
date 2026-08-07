// An on-screen keyboard that lives in part of a screen rather than owning it.
//
// KeyboardEntryActivity is a full-screen activity that returns one string;
// Create Note and Claude need the text visible ABOVE the keys while typing, so
// they host this instead. It renders the same FreeInkUI keyboard component into
// a caller-supplied Rect and reports keys back, holding no text of its own.
//
// Honours all three SETTINGS.keyboardLayout choices. QWERTY and 13-Grid render
// through the shared FreeInkUI keyboard component (the 13-grid table lives in
// notes/Grid13Layout.h so the full-screen activity and this panel use ONE
// definition). Daisy's wheel geometry assumes a full screen, so in a
// half-height strip its character groups are laid out as flat rings and typed
// with the same multi-tap idiom — same letters, same grouping, same order.
#pragma once

#include <components/keyboard/keyboard.h>

#include <cstdint>

class GfxRenderer;

namespace notes {

class KeyboardPanel {
 public:
  // What a key press means to the host activity.
  enum class Event : uint8_t { None, Character, Backspace, Enter, Done };

  struct Result {
    Event event = Event::None;
    char ch = 0;  // valid when event == Character
  };

  void begin();  // pick the layout from settings; reset selection

  // Navigation from the host's buttons.
  void moveRow(int delta);
  void moveCol(int delta);

  // Activate the selected key. longPress gives the alt output — which for a
  // letter is its uppercase, matching the full-screen keyboard.
  Result activate(bool longPress = false);

  // Daisy only: the three pick buttons choose top/middle/bottom of the current
  // petal directly. This is NOT a multi-tap cycle; see notes/DaisyRings.h.
  Result activateSlot(int slot, bool longPress = false);
  bool isDaisy() const { return daisy_; }

  // Height this panel wants for `rows` of keys at the theme's key spacing.
  int preferredHeight(const GfxRenderer& renderer) const;

  void render(GfxRenderer& renderer, int x, int y, int width, int height);

  bool shifted() const { return shift_; }
  bool symbols() const { return symbols_; }

 private:
  const freeink::ui::KeyboardLayout& layout() const;
  int rowCount() const;
  int colsInRow(int row) const;
  int selectedLogicalIndex() const;
  int petalCount() const;
  char slotChar(int petal, int slot) const;

  static constexpr int DAISY_PETALS = 5;

  bool shift_ = false;
  bool symbols_ = false;
  int row_ = 0;
  int col_ = 0;
  bool grid13_ = false;
  bool daisy_ = false;
  int ringIdx_ = 0;  // 0 = abc ring, 1 = numbers/symbols
  int petal_ = 0;
  // The multi-tap group currently being cycled. Cells come from one static
  // table, so identity is the pointer; no cell text repeats.
  const char* lastGroup_ = nullptr;
  int groupIndex_ = 0;
};

}  // namespace notes
