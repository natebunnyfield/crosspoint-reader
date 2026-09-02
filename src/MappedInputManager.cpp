#include "MappedInputManager.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdlib>

#include "CrossPointSettings.h"
#include "components/UITheme.h"

bool MappedInputManager::isNavDirectionSwapped() const {
  // Everything renders portrait, so the nav axis never flips. Kept as a named
  // predicate because the call sites read as "which way is next" questions.
  return false;
}

MappedInputManager::Button MappedInputManager::mapScreenDirection(const Button button) const {
  // Portrait only: screen directions map straight through.
  static constexpr Button directions[4] = {Button::Left, Button::Right, Button::Up, Button::Down};

  uint8_t direction = 0;
  switch (button) {
    case Button::ScreenLeft:
      direction = 0;
      break;
    case Button::ScreenRight:
      direction = 1;
      break;
    case Button::ScreenUp:
      direction = 2;
      break;
    case Button::ScreenDown:
      direction = 3;
      break;
    default:
      return button;
  }

  return directions[direction];
}

bool MappedInputManager::mapButton(const Button button, bool (HalGPIO::*fn)(uint8_t) const) const {
  return mapButton(button, [this, fn](const uint8_t i) { return (gpio.*fn)(i); });
}

bool MappedInputManager::mapButtonInMask(const Button button, const uint8_t mask) const {
  if (mask == 0) return false;
  return mapButton(button, [mask](const uint8_t i) { return ((mask >> i) & 1u) != 0; });
}

bool MappedInputManager::mapButton(const Button button, const std::function<bool(uint8_t)>& fn) const {
  const auto sideLayout = SETTINGS.sideButtonLayout;

  switch (button) {
    case Button::Back:
      // Logical Back maps to user-configured front button.
      return fn(SETTINGS.frontButtonBack);
    case Button::Confirm:
      // Logical Confirm maps to user-configured front button.
      return fn(SETTINGS.frontButtonConfirm);
    case Button::Left:
      // Logical Left maps to user-configured front button.
      return fn(SETTINGS.frontButtonLeft);
    case Button::Right:
      // Logical Right maps to user-configured front button.
      return fn(SETTINGS.frontButtonRight);
    case Button::Up:
      // Side buttons remain fixed for Up/Down.
      return fn(HalGPIO::BTN_UP);
    case Button::Down:
      // Side buttons remain fixed for Up/Down.
      return fn(HalGPIO::BTN_DOWN);
    case Button::Power:
      // Power button bypasses remapping.
      return fn(HalGPIO::BTN_POWER);
    case Button::PageBack:
      // Reader page navigation uses side buttons and can be swapped via settings.
      switch (sideLayout) {
        case CrossPointSettings::PREV_NEXT:
          return fn(HalGPIO::BTN_UP);
        case CrossPointSettings::NEXT_PREV:
          return fn(HalGPIO::BTN_DOWN);
        case CrossPointSettings::SIDE_BUTTONS_DISABLED:
        default:
          return false;
      }
    case Button::PageForward:
      // Reader page navigation uses side buttons and can be swapped via settings.
      switch (sideLayout) {
        case CrossPointSettings::PREV_NEXT:
          return fn(HalGPIO::BTN_DOWN);
        case CrossPointSettings::NEXT_PREV:
          return fn(HalGPIO::BTN_UP);
        case CrossPointSettings::SIDE_BUTTONS_DISABLED:
        default:
          return false;
      }
    case Button::NavNext:
      // Logical "next item" navigation: the FRONT pair only.
      //
      // This used to be "side Down OR front Right", which made the two pairs
      // literally the same action on every list screen in the firmware — the
      // owner's 2026-08-14 ask ("make side buttons be page up and page down,
      // when they are identical functionality to front buttons, like on the
      // home screen"). Narrowing it here rather than per-screen is what keeps
      // every list consistent; see docs/ui-conventions.md.
      return isNavDirectionSwapped() ? mapButton(Button::Left, fn) : mapButton(Button::Right, fn);
    case Button::NavPrevious:
      // Logical "previous item" navigation: the FRONT pair only.
      return isNavDirectionSwapped() ? mapButton(Button::Right, fn) : mapButton(Button::Left, fn);
    case Button::PageNext:
      // Logical "next screenful": the SIDE pair, honoring the user's side-button
      // swap so paging a list runs the same way round as paging the book.
      //
      // SIDE_BUTTONS_DISABLED deliberately falls through to the natural order
      // rather than going inert, for the reason ReaderUtils.h:112-125 records
      // about the reader's side font controls: that setting exists to stop the
      // side buttons turning BOOK pages, and honoring it here would leave every
      // list with no page control at all — a silent loss for anyone who set it.
      return fn(sideLayout == CrossPointSettings::NEXT_PREV ? HalGPIO::BTN_UP : HalGPIO::BTN_DOWN);
    case Button::PagePrevious:
      // Logical "previous screenful": the SIDE pair. See PageNext.
      return fn(sideLayout == CrossPointSettings::NEXT_PREV ? HalGPIO::BTN_DOWN : HalGPIO::BTN_UP);
    case Button::ScreenLeft:
    case Button::ScreenRight:
    case Button::ScreenUp:
    case Button::ScreenDown:
      return mapButton(mapScreenDirection(button), fn);
  }

  return false;
}

namespace {
constexpr float LEFT_EDGE_BACK_GESTURE_FRAC_X = 0.25f;
constexpr float BOTTOM_EDGE_BACK_GESTURE_FRAC_Y = 0.14f;
constexpr float TOP_EDGE_MENU_GESTURE_FRAC_Y = 0.14f;
constexpr unsigned long TOUCH_DOWN_SELECT_DELAY_MS = 90;
constexpr unsigned long TOUCH_HELD_OVERRIDE_WINDOW_MS = 250;
}  // namespace

bool MappedInputManager::hasTouch() const { return gpio.hasTouch(); }

void MappedInputManager::rememberTouchHeldTime() const {
  touchHeldOverrideValid = true;
  touchHeldOverrideMs = gpio.lastTouchHeldMs();
  touchHeldOverrideAt = millis();
}

bool MappedInputManager::wasScreenTapped(int& x, int& y) const {
  float nx = 0.0f;
  float ny = 0.0f;
  if (!gpio.wasTouchTap(nx, ny)) return false;
  renderer.tapToLogical(nx, ny, x, y);
  rememberTouchHeldTime();
  return true;
}

bool MappedInputManager::wasScreenTouchDown(int& x, int& y) const {
  float nx = 0.0f;
  float ny = 0.0f;
  unsigned long heldMs = 0;
  if (!gpio.isTouchTapCandidate(nx, ny, heldMs)) return false;
  if (heldMs < TOUCH_DOWN_SELECT_DELAY_MS) return false;
  renderer.tapToLogical(nx, ny, x, y);
  return true;
}

bool MappedInputManager::isScreenTouchHeld(int& x, int& y) const {
  // Live contact position while the finger is down (no tap-slop gate) — drag tracking.
  float nx = 0.0f;
  float ny = 0.0f;
  if (!gpio.isTouchHeldAt(nx, ny)) return false;
  renderer.tapToLogical(nx, ny, x, y);
  return true;
}

bool MappedInputManager::wasTapInRect(const int x, const int y, const int width, const int height) const {
  int tx = 0;
  int ty = 0;
  return wasScreenTapped(tx, ty) && tx >= x && tx < x + width && ty >= y && ty < y + height;
}

bool MappedInputManager::listItemFromPoint(const int x, const int y, int& index, const int itemCount,
                                           const int selectedIndex, const int listTop, const int listHeight,
                                           const bool hasSubtitle) const {
  (void)x;
  if (itemCount <= 0) return false;
  if (y < listTop || y >= listTop + listHeight) return false;

  const auto& theme = UITheme::getInstance().getTheme();
  const int rowStep = theme.getListRowStep(hasSubtitle);
  if (rowStep <= 0) return false;

  const int pageItems = theme.getListPageItems(listHeight, hasSubtitle);
  if (pageItems <= 0) return false;
  const int pageStart = std::max(0, selectedIndex / pageItems) * pageItems;
  const int row = (y - listTop) / rowStep;
  const int tapped = pageStart + row;
  if (row < 0 || row >= pageItems || tapped >= itemCount) return false;
  index = tapped;
  return true;
}

bool MappedInputManager::wasListItemTapped(int& index, const int itemCount, const int selectedIndex, const int listTop,
                                           const int listHeight, const bool hasSubtitle) const {
  int tx = 0;
  int ty = 0;
  return wasScreenTapped(tx, ty) &&
         listItemFromPoint(tx, ty, index, itemCount, selectedIndex, listTop, listHeight, hasSubtitle);
}

bool MappedInputManager::wasListItemTouchedDown(int& index, const int itemCount, const int selectedIndex,
                                                const int listTop, const int listHeight, const bool hasSubtitle) const {
  int tx = 0;
  int ty = 0;
  return wasScreenTouchDown(tx, ty) &&
         listItemFromPoint(tx, ty, index, itemCount, selectedIndex, listTop, listHeight, hasSubtitle);
}

MappedInputManager::RowTouch MappedInputManager::rowTouch(int& row, const int top, const int rowStep,
                                                          const int rowCount, const int xStart, const int xEnd,
                                                          const int rowHeight) const {
  if (rowStep <= 0 || rowCount <= 0) return RowTouch::None;
  const auto hit = [&](const int x, const int y) {
    if (x < xStart || x >= xEnd || y < top) return false;
    const int r = (y - top) / rowStep;
    if (r >= rowCount) return false;
    if (rowHeight > 0 && (y - top) % rowStep >= rowHeight) return false;
    row = r;
    return true;
  };
  int x = 0;
  int y = 0;
  if (wasScreenTouchDown(x, y) && hit(x, y)) return RowTouch::Down;
  if (wasScreenTapped(x, y) && hit(x, y)) return RowTouch::Tap;
  return RowTouch::None;
}

MappedInputManager::RowTouch MappedInputManager::colTouch(int& col, const int left, const int colStep,
                                                          const int colCount, const int yStart, const int yEnd,
                                                          const int colWidth) const {
  if (colStep <= 0 || colCount <= 0) return RowTouch::None;
  const auto hit = [&](const int x, const int y) {
    if (y < yStart || y >= yEnd || x < left) return false;
    const int c = (x - left) / colStep;
    if (c >= colCount) return false;
    if (colWidth > 0 && (x - left) % colStep >= colWidth) return false;
    col = c;
    return true;
  };
  int x = 0;
  int y = 0;
  if (wasScreenTouchDown(x, y) && hit(x, y)) return RowTouch::Down;
  if (wasScreenTapped(x, y) && hit(x, y)) return RowTouch::Tap;
  return RowTouch::None;
}

bool MappedInputManager::decodeSwipe(int& sx, int& sy, int& ex, int& ey) const {
  float nxs = 0.0f;
  float nys = 0.0f;
  float nxe = 0.0f;
  float nye = 0.0f;
  if (!gpio.wasSwipe(nxs, nys, nxe, nye)) return false;
  renderer.tapToLogical(nxs, nys, sx, sy);
  renderer.tapToLogical(nxe, nye, ex, ey);
  return true;
}

MappedInputManager::SwipeDir MappedInputManager::wasSwipe() const {
  int sx = 0;
  int sy = 0;
  int ex = 0;
  int ey = 0;
  if (!decodeSwipe(sx, sy, ex, ey)) return SwipeDir::None;
  const int dx = ex - sx;
  const int dy = ey - sy;
  if (std::abs(dx) >= std::abs(dy)) {
    return dx < 0 ? SwipeDir::Left : SwipeDir::Right;
  }
  return dy < 0 ? SwipeDir::Up : SwipeDir::Down;
}

bool MappedInputManager::wasBackGesture() const {
  // Back = left-to-right swipe starting near the left edge. Edge-anchored so that
  // mid-screen horizontal swipes stay available to activities that consume
  // SwipeDir::Left/Right (e.g. percent selection, image viewer).
  int sx = 0;
  int sy = 0;
  int ex = 0;
  int ey = 0;
  if (!decodeSwipe(sx, sy, ex, ey)) return false;
  const bool hit = sx <= renderer.getScreenWidth() * LEFT_EDGE_BACK_GESTURE_FRAC_X && ex > sx &&
                   std::abs(ex - sx) > std::abs(ey - sy);
  if (hit) rememberTouchHeldTime();
  return hit;
}

bool MappedInputManager::wasMenuGesture() const {
  // Downward swipe starting at the top edge (mirror of the bottom-edge home gesture).
  int sx = 0;
  int sy = 0;
  int ex = 0;
  int ey = 0;
  if (!decodeSwipe(sx, sy, ex, ey)) return false;
  const int topEdgeBottom = static_cast<int>(renderer.getScreenHeight() * TOP_EDGE_MENU_GESTURE_FRAC_Y);
  const bool hit = sy <= topEdgeBottom && ey > sy && std::abs(ey - sy) > std::abs(ex - sx);
  if (hit) rememberTouchHeldTime();
  return hit;
}

bool MappedInputManager::wasHomeGesture() const {
  int sx = 0;
  int sy = 0;
  int ex = 0;
  int ey = 0;
  if (decodeSwipe(sx, sy, ex, ey)) {
    const int bottomEdgeTop =
        renderer.getScreenHeight() - static_cast<int>(renderer.getScreenHeight() * BOTTOM_EDGE_BACK_GESTURE_FRAC_Y);
    if (sy >= bottomEdgeTop && ey < sy && std::abs(ey - sy) > std::abs(ex - sx)) {
      rememberTouchHeldTime();
      return true;
    }
  }
  return false;
}

bool MappedInputManager::isAnyPhysicalButtonHeld() const {
  // Check all seven physical button indices.
  for (uint8_t i = 0; i <= HalGPIO::BTN_POWER; ++i) {
    if (gpio.isPressed(i)) return true;
  }
  return false;
}

void MappedInputManager::update() const {
  gpio.update();
  {
    // Per-button press stamps, from the RAW edges: a press is a physical fact
    // whatever the swallow thinks of it. The swallow is applied at read time,
    // in getHeldTime(Button), the same way isPressed() applies it.
    static_assert(HalGPIO::BTN_POWER + 1 == buttonhold::kButtons, "one hold slot per physical button");
    uint8_t presses = 0;
    uint8_t releases = 0;
    uint8_t levels = 0;
    for (uint8_t i = 0; i <= HalGPIO::BTN_POWER; ++i) {
      const uint8_t bit = static_cast<uint8_t>(1u << i);
      if (gpio.wasPressed(i)) presses |= bit;
      if (gpio.wasReleased(i)) releases |= bit;
      if (gpio.isPressed(i)) levels |= bit;
    }
    holdTimer_.frame(millis(), presses, releases, levels);
  }
  if (!swallowActive_) return;
  // A new frame began, so the arming frame is over and this frame's swallow
  // state can be settled ONCE, here, rather than lazily in whichever gated
  // read runs first. (Lazy clearing is what dfeb1232e had to patch: the
  // frame's first query decided for the whole frame, and a later query in the
  // same frame could still read a latched edge.)
  swallowArmingFrame_ = false;
  uint8_t releases = 0;
  uint8_t held = 0;
  for (uint8_t i = 0; i <= HalGPIO::BTN_POWER; ++i) {
    const uint8_t bit = static_cast<uint8_t>(1u << i);
    if (gpio.wasReleased(i)) releases |= bit;
    if (gpio.isPressed(i)) held |= bit;
  }
  // The stale release: this frame's release edge on a button that was held at
  // swap time belongs to the press the DEPARTED activity consumed.
  staleReleaseMask_ = swallowMask_ & releases;
  // A button no longer held has finished its stale story and cannot produce
  // another stale edge: re-pressing it makes a PRESS edge, which is fresh
  // input by definition after the arming frame.
  swallowMask_ &= held;
  if (swallowMask_ == 0 && staleReleaseMask_ == 0) swallowActive_ = false;
}

void MappedInputManager::swallowUntilIdle() {
  // Only arm for buttons actually held at the swap. If nothing is held there
  // are no stale edges or holds to protect against, and arming would swallow
  // the NEXT legitimate press (which may arrive immediately in tests or UI
  // combos).
  uint8_t held = 0;
  for (uint8_t i = 0; i <= HalGPIO::BTN_POWER; ++i) {
    if (gpio.isPressed(i)) held |= static_cast<uint8_t>(1u << i);
  }
  if (held == 0) return;
  swallowMask_ |= held;
  swallowActive_ = true;
  // Everything in the ARMING frame is stale: the press edge that triggered the
  // swap can still be latched (transitions are processed in the frame that
  // produced them), so same-frame consumers — result handlers, onEnter, an
  // immediate-launch replaceActivity — must see nothing at all. update()
  // clears this at the next frame boundary.
  swallowArmingFrame_ = true;
}

bool MappedInputManager::wasPressed(const Button button) const {
  if (swallowActive_) {
    if (swallowArmingFrame_) return false;
    // After the arming frame a press edge is fresh input UNLESS it is on a
    // button still held since the swap — only an edge detector starting from
    // zero state produces that (a button held across boot, finding 4's
    // recovery picker). A fresh press on any other button is DELIVERED: the
    // swallow exists to eat the stale release of the press that caused the
    // swap, not the user's next press (input-edge audit 2026-08-21, finding 6).
    if (mapButtonInMask(button, swallowMask_)) return false;
  }
  if (button == Button::Back && wasBackGesture()) return true;
  return mapButton(button, &HalGPIO::wasPressed);
}

bool MappedInputManager::wasReleased(const Button button) const {
  if (swallowActive_) {
    if (swallowArmingFrame_) return false;
    // The stale release itself, eaten for EVERY query in its frame — the
    // owner's "Back from Settings opens the book" was a later query in the
    // release frame reading the latched edge (iOS build 120 lineage,
    // dfeb1232e). A still-held swap button has no release edge to leak, and a
    // release on any other button is the end of a fresh press: delivered.
    if (mapButtonInMask(button, staleReleaseMask_)) return false;
  }
  if (button == Button::Back && wasBackGesture()) return true;
  return mapButton(button, &HalGPIO::wasReleased);
}

bool MappedInputManager::isPressed(const Button button) const {
  if (swallowActive_) {
    if (swallowArmingFrame_) return false;
    // Level reads are gated too, not just edges: a hold that crosses an
    // activity boundary belongs to the DEPARTED activity, and the hold timer
    // never resets on transition. When isPressed()/getHeldTime() bypassed the
    // swallow, holding Back out of the note editor satisfied Manage Files'
    // isPressed(Back) && getHeldTime() >= GO_HOME_MS branch on its first
    // frames and jumped to the SD root instead of the note's folder
    // (input-edge audit 2026-08-21, finding 1). That specific Back-hold
    // destination was retired 2026-09-01 (docs/hold-gestures.md), but the
    // same class of bug is exactly as live for the held-Confirm gestures
    // that replaced it (FileBrowserActivity's delete hold,
    // FileManagerActivity's action-menu hold) or any future hold elsewhere —
    // hence this gate staying general rather than naming one button. Only
    // the swap-held buttons are gated: a fresh press's level reads normally.
    if (mapButtonInMask(button, swallowMask_)) return false;
  }
  return mapButton(button, &HalGPIO::isPressed);
}

bool MappedInputManager::wasAnyPressed() const { return gpio.wasAnyPressed(); }

bool MappedInputManager::wasAnyReleased() const { return gpio.wasAnyReleased(); }

unsigned long MappedInputManager::getHeldTime() const {
  if (swallowActive_) {
    // The HAL's held time is global, not per-button, so while any swap-held
    // button is still settling the value measures the previous activity's
    // press (finding 1). 0 until the swallow resolves.
    return 0;
  }
  if (!gpio.wasAnyPressed() && !gpio.wasAnyReleased() && touchHeldOverrideValid &&
      millis() - touchHeldOverrideAt <= TOUCH_HELD_OVERRIDE_WINDOW_MS) {
    return touchHeldOverrideMs;
  }
  touchHeldOverrideValid = false;
  return gpio.getHeldTime();
}

unsigned long MappedInputManager::getHeldTime(const Button button) const {
  if (swallowActive_) {
    // Same gate as isPressed()/wasReleased(): a hold that crossed an activity
    // boundary belongs to the departed activity, and its release is stale.
    if (swallowArmingFrame_) return 0;
    if (mapButtonInMask(button, swallowMask_) || mapButtonInMask(button, staleReleaseMask_)) return 0;
  }
  // The one physical button this logical one resolves to (SIDE_BUTTONS_DISABLED
  // resolves PageBack/PageForward to nothing, so they read 0 here as they do
  // everywhere else). NavNext and friends recurse to a single index too.
  uint8_t idx = buttonhold::kButtons;
  mapButton(button, [&idx](const uint8_t i) {
    idx = i;
    return true;
  });
  if (idx >= buttonhold::kButtons) return 0;
  if (gpio.isPressed(idx) || gpio.wasReleased(idx)) return holdTimer_.heldMs(idx, millis());
  // No press of this button in flight. A touch tap may be standing in for it
  // (the touch boards' long-tap-to-delete reaches the same release path), so
  // the tap's contact time is answered under the same window the global timer
  // used — read, not retired: getHeldTime() still owns the retirement.
  if (!gpio.wasAnyPressed() && !gpio.wasAnyReleased() && touchHeldOverrideValid &&
      millis() - touchHeldOverrideAt <= TOUCH_HELD_OVERRIDE_WINDOW_MS) {
    return touchHeldOverrideMs;
  }
  return 0;
}

MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  // Labels follow the nav direction; portrait never swaps them.
  const bool swapLabels = isNavDirectionSwapped();
  const char* leftLabel = swapLabels ? next : previous;
  const char* rightLabel = swapLabels ? previous : next;

  return mapFrontLabels(back, confirm, leftLabel, rightLabel);
}

MappedInputManager::Labels MappedInputManager::mapDirectionalLabels(const char* back, const char* confirm,
                                                                    const char* left, const char* right, const char* up,
                                                                    const char* down) const {
  const auto labelForButton = [&](const Button rawButton) {
    if (mapScreenDirection(Button::ScreenLeft) == rawButton) return left;
    if (mapScreenDirection(Button::ScreenRight) == rawButton) return right;
    if (mapScreenDirection(Button::ScreenUp) == rawButton) return up;
    if (mapScreenDirection(Button::ScreenDown) == rawButton) return down;
    return "";
  };
  return mapFrontLabels(back, confirm, labelForButton(Button::Left), labelForButton(Button::Right));
}

MappedInputManager::Labels MappedInputManager::mapFrontLabels(const char* back, const char* confirm, const char* left,
                                                              const char* right) const {
  // Build the label order based on the configured hardware mapping.
  auto labelForHardware = [&](uint8_t hw) -> const char* {
    // Compare against configured logical roles and return the matching label.
    if (hw == SETTINGS.frontButtonBack) {
      return back;
    }
    if (hw == SETTINGS.frontButtonConfirm) {
      return confirm;
    }
    if (hw == SETTINGS.frontButtonLeft) {
      return left;
    }
    if (hw == SETTINGS.frontButtonRight) {
      return right;
    }
    return "";
  };

  return {labelForHardware(HalGPIO::BTN_BACK), labelForHardware(HalGPIO::BTN_CONFIRM),
          labelForHardware(HalGPIO::BTN_LEFT), labelForHardware(HalGPIO::BTN_RIGHT)};
}

int MappedInputManager::getPressedFrontButton() const {
  // Scan the raw front buttons in hardware order.
  // This bypasses remapping so the remap activity can capture physical presses.
  if (gpio.wasPressed(HalGPIO::BTN_BACK)) {
    return HalGPIO::BTN_BACK;
  }
  if (gpio.wasPressed(HalGPIO::BTN_CONFIRM)) {
    return HalGPIO::BTN_CONFIRM;
  }
  if (gpio.wasPressed(HalGPIO::BTN_LEFT)) {
    return HalGPIO::BTN_LEFT;
  }
  if (gpio.wasPressed(HalGPIO::BTN_RIGHT)) {
    return HalGPIO::BTN_RIGHT;
  }
  return -1;
}
