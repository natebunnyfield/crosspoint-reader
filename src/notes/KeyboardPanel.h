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
// half-height strip its petals are laid out flat — same letters, same grouping,
// same order. Selection is DIRECT, as in DaisyEntryActivity: a petal exposes
// its three characters as three slots and you pick one. It is not a multi-tap
// cycle; an earlier version of this panel invented that and was wrong.
#pragma once

#include <components/keyboard/keyboard.h>

#include <cstdint>
#if FREEINK_DEVICE_PICCO
#include <atomic>

#include "components/KeyboardTouch.h"
#endif

class GfxRenderer;

namespace notes {

class KeyboardPanel {
 public:
  // What a key press means to the host activity.
  enum class Event : uint8_t { None, Character, Backspace, ClearAll, Enter, Done };

  struct Result {
    Event event = Event::None;
    char ch = 0;  // valid when event == Character
  };

  // okIsDone: what the OK key means. Claude asks; the note editor inserts a
  // newline. Without this the grids always meant newline, and Claude's only
  // route to send() was the daisy strip's OK — so on the DEFAULT keyboard
  // Claude could not be asked anything at all.
  void begin(bool okIsDone = false);

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

#if FREEINK_DEVICE_PICCO
  // --- Touch: BOOX Picco only (components/KeyboardTouch.h) -----------------
  // A tap on a key is the buttons' select-then-Confirm, a hold is a held
  // Confirm. The host feeds routeTouch() one pass of touch per loop with the
  // rect it hands render(); a dispatched key comes back without the selection
  // having moved. The host then moves it with selectTouched() under its
  // RenderLock and passes the returned pick slot to its handlePanelKey(), the
  // very path its buttons take, so typing, refresh and debounce are theirs.
  struct TouchKey {
    bool fired = false;
    int16_t id = 0;  // a grid key's value, or daisy petal * 3 + slot
    bool longPress = false;
  };
  TouchKey routeTouch(const kbtouch::TouchInput& in, uint32_t nowMs, int x, int y, int width, int height, int screenW,
                      int screenH);
  // Selects the key routeTouch() returned, exactly where the buttons would put
  // the selection, and returns the slot handlePanelKey() takes: 1 (Confirm)
  // on the grids, the touched slot on daisy; -1 if `id` is no key of the
  // current layer (type nothing). Writes what render() draws.
  int selectTouched(int16_t id);
  // Forget any contact in progress (the host stopped taking touch).
  void resetTouch();
#endif

 private:
  const freeink::ui::KeyboardLayout& layout() const;
  int rowCount() const;
  int colsInRow(int row) const;
  int selectedLogicalIndex() const;
  int keyIndexAtUnit(int row, int unit) const;
  void keyUnitSpan(int row, int keyIdx, int& start, int& end) const;
  int petalCount() const;
  char slotChar(int petal, int slot) const;

  static constexpr int DAISY_PETALS = 5;

  bool shift_ = false;
  bool symbols_ = false;
  int row_ = 0;
  int col_ = 0;
  int anchorUnit_ = 0;  // 13-Grid column anchor, in width units
  bool grid13_ = false;
  bool okIsDone_ = false;
  bool daisy_ = false;
  int ringIdx_ = 0;  // 0 = abc ring, 1 = numbers/symbols
  int petal_ = 0;

#if FREEINK_DEVICE_PICCO
  freeink::ui::KeyboardProps touchKeyProps() const;
  // Grid keys: rebuilt on the loop task from the current layer, never shared
  // with render(), which registers nothing (so no key ever draws pressed).
  freeink::ui::InteractionBuffer<kbtouch::KEY_SLOTS> touchHits_;
  freeink::ui::TouchHoldRouter gridRouter_;
  kbtouch::HoldRouter stripRouter_;
  // The daisy strip re-centers on the selected petal, so every tap off the
  // middle cell moves every petal on screen. A tap must land on what the
  // owner SEES, which until the panel refreshes is the last drawn strip, not
  // the state the previous tap just changed: render() publishes the ring and
  // center petal it drew here (ring << 8 | petal; -1 before the first draw).
  std::atomic<int> stripDrawn_{-1};
#endif
};

}  // namespace notes
