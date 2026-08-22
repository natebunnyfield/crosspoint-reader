#pragma once

#include <HalGPIO.h>

#include <functional>

class GfxRenderer;

class MappedInputManager {
 public:
  enum class Button {
    Back,
    Confirm,
    Left,
    Right,
    Up,
    Down,
    Power,
    PageBack,
    PageForward,
    NavNext,
    NavPrevious,
    PageNext,
    PagePrevious,
    ScreenLeft,
    ScreenRight,
    ScreenUp,
    ScreenDown
  };
  enum class SwipeDir { None, Left, Right, Up, Down };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  MappedInputManager(HalGPIO& gpio, const GfxRenderer& renderer) : gpio(gpio), renderer(renderer) {}

  // The once-per-frame input poll (top of src/main.cpp's loop). Beyond
  // gpio.update() it settles the post-swap swallow's per-frame state ONCE, at
  // the frame boundary, instead of lazily in whichever gated read runs first —
  // see swallowUntilIdle().
  void update() const;
  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool hasTouch() const;
  bool wasScreenTapped(int& x, int& y) const;
  bool wasScreenTouchDown(int& x, int& y) const;
  bool isScreenTouchHeld(int& x, int& y) const;
  bool wasTapInRect(int x, int y, int width, int height) const;
  bool wasListItemTapped(int& index, int itemCount, int selectedIndex, int listTop, int listHeight,
                         bool hasSubtitle) const;
  bool wasListItemTouchedDown(int& index, int itemCount, int selectedIndex, int listTop, int listHeight,
                              bool hasSubtitle) const;

  // Combined touch interaction for a band of equal rows with caller-supplied
  // geometry — the shared hit-test for lists the theme helpers above do not
  // cover (custom row heights, option prompts, menus). Down = a held
  // tap-candidate is on a row (update the selection highlight); Tap = a tap
  // released on one (activate). rowHeight limits the hit to the top rowHeight
  // px of each step (0 = the full step, no gap band).
  enum class RowTouch : uint8_t { None, Down, Tap };
  RowTouch rowTouch(int& row, int top, int rowStep, int rowCount, int xStart = 0, int xEnd = INT32_MAX,
                    int rowHeight = 0) const;
  // Horizontal variant for side-by-side button pairs (confirmation prompts).
  RowTouch colTouch(int& col, int left, int colStep, int colCount, int yStart, int yEnd, int colWidth = 0) const;

  // Host keyboard text entry — see HalGPIO. Straight passthroughs: nothing
  // here is directional, so there is no button mapping to do, and typed text
  // is the one input that does not arrive as a Button. No-ops on device.
  void setTextEntryActive(const bool active,
                          const HalGPIO::TextEntryLines lines = HalGPIO::TextEntryLines::Single) const {
    gpio.setTextEntryActive(active, lines);
  }
  bool consumeTypedText(std::string& out) const { return gpio.consumeTypedText(out); }
  // Is a host's own software keyboard covering the screen? Always false on
  // device -- see HalGPIO::isHostKeyboardVisible for why it is asked in one
  // form rather than behind an #if.
  bool isHostKeyboardVisible() const { return gpio.isHostKeyboardVisible(); }

  SwipeDir wasSwipe() const;
  bool wasHomeGesture() const;
  bool wasMenuGesture() const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  const GfxRenderer& getRenderer() const { return renderer; }
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  // Maps four screen-direction labels onto the two physical front-button roles
  // using the same live-orientation transform as ScreenLeft/Right/Up/Down.
  Labels mapDirectionalLabels(const char* back, const char* confirm, const char* left, const char* right,
                              const char* up, const char* down) const;
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;

  // True when the control axis is flipped relative to the physical buttons. Always false now that
  // everything renders portrait; kept so the nav call sites stay readable as direction questions.
  [[nodiscard]] bool isNavDirectionSwapped() const;

  // After an activity swap (pop, push, or replace) the incoming activity must
  // not see stale press/release edges — or the stale LEVEL and HOLD TIME — from
  // the button(s) the outgoing activity consumed. Call this once at the
  // transition. The swallow is DIRECTIONAL and per-button, not a blanket gate
  // (input-edge audit 2026-08-21, finding 6):
  //   - In the ARMING frame everything reads idle: the press edge that caused
  //     the swap may still be latched, and same-frame consumers (result
  //     handlers, onEnter, an immediate-launch replaceActivity) must see none
  //     of it.
  //   - From the next frame on, the buttons held at swap time read unpressed
  //     (level, hold time, and any boot-style from-zero press edge), and their
  //     release edge — the stale release — is eaten for its whole frame.
  //   - Everything ELSE is delivered: a PRESS edge after the arming frame is
  //     always fresh input, because a held button cannot emit a second press
  //     edge without first releasing (which retires it from the swallow). The
  //     old whole-frame latch ate a genuine second press that landed in the
  //     same idle-poll window as the stale release.
  void swallowUntilIdle();

  // Pair for test tear-down: clears the swallow so a between-test reset does
  // not leak state into the next case.
  void resetSwallow() {
    swallowActive_ = false;
    swallowArmingFrame_ = false;
    swallowMask_ = 0;
    staleReleaseMask_ = 0;
  }

 private:
  HalGPIO& gpio;
  // Logical-to-physical button mapping depends on what the user is actually looking at: when the
  // screen is rendered rotated, the directional buttons must flip to match. The renderer is the only
  // authority on the *live* orientation (the reader rotates it and restores portrait on exit), so we
  // read it here instead of CrossPointSettings.orientation, which is just the persisted reader
  // preference and stays "rotated" even while portrait UI like home/settings is on screen.
  const GfxRenderer& renderer;

  Button mapScreenDirection(Button button) const;
  Labels mapFrontLabels(const char* back, const char* confirm, const char* left, const char* right) const;
  // The logical→physical switch, over an arbitrary per-physical-button
  // predicate; the member-pointer overload is the common gpio case and the
  // mask overload asks "does this logical button touch any masked physical?".
  bool mapButton(Button button, const std::function<bool(uint8_t)>& fn) const;
  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
  bool mapButtonInMask(Button button, uint8_t mask) const;
  bool wasBackGesture() const;
  // Fetch the pending swipe (if any) and map both endpoints to logical screen coords
  bool decodeSwipe(int& sx, int& sy, int& ex, int& ey) const;
  bool listItemFromPoint(int x, int y, int& index, int itemCount, int selectedIndex, int listTop, int listHeight,
                         bool hasSubtitle) const;
  void rememberTouchHeldTime() const;

  mutable bool touchHeldOverrideValid = false;
  mutable unsigned long touchHeldOverrideMs = 0;
  mutable unsigned long touchHeldOverrideAt = 0;

  // Swallow state — see swallowUntilIdle(). All mutable because the gated
  // reads are const and update() is called through a const reference.
  // swallowActive_ is the master switch; swallowArmingFrame_ marks the frame
  // the swallow armed in (everything reads idle until the next update());
  // swallowMask_ is the physical buttons held at swap time, retired by
  // update() as each goes idle; staleReleaseMask_ is this frame's release
  // edges on swap-held buttons, recomputed by update() each frame so the stale
  // release stays eaten for EVERY query in its frame.
  mutable bool swallowActive_ = false;
  mutable bool swallowArmingFrame_ = false;
  mutable uint8_t swallowMask_ = 0;
  mutable uint8_t staleReleaseMask_ = 0;
  bool isAnyPhysicalButtonHeld() const;
};
