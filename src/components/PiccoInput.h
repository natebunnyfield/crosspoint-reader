// BOOX Picco input, pure so it is host-tested (test/picco_input): the rocker
// remap, and the decoder that turns one touch contact into a synthesized
// button. Compiled into Picco builds only (MappedInputManager.cpp includes it
// under FREEINK_DEVICE_PICCO), so X4/X3/Sticky compile exactly what they did.
//
// Owner ruling 2026-11-06, "Picco input = the full iOS norm, keyboards type"
// (docs/picco-port-2026-11-05.md in the primary checkout):
//   * The rocker sends Right (upper end, GPIO21, the SDK's BTN_UP) and Left
//     (lower end, GPIO9, BTN_DOWN) instead of page up/down, on every screen.
//     Up, Down and the four Page buttons no longer come from it.
//   * Outside keyboards, a single-finger gesture is a SYNTHESIZED BUTTON, with
//     the iOS zen defaults (crosspoint-simulator ios/GestureBindings.h:638-689):
//     tap = Right, swipe left = Right, swipe right = Left, swipe up = Back,
//     swipe down = Confirm, hold = Confirm. A left-edge swipe
//     (components/EdgeSwipe.h) is Back everywhere and wins over swipe right.
//   * Inside keyboards, the keyboard types the key a tap lands on (its own
//     path, HalGPIO::setKeyboardTouch), the left-edge swipe is Back, and every
//     other gesture is off.
//
// Header-only on purpose: a new translation unit breaks the simulator's iOS
// source list (CLAUDE.md).
#pragma once

#include <cstdint>
#include <cstdlib>

#include "MappedInputManager.h"
#include "components/EdgeSwipe.h"

namespace piccoinput {

using Button = MappedInputManager::Button;

// --- The rocker -------------------------------------------------------------

// Where a logical button comes from on the Picco. Unchanged = the shared
// mapping in MappedInputManager::mapButton (Back, Confirm and Power: the
// settings-driven front indices and BTN_POWER, so GPIO5 stays Confirm).
enum class Route : uint8_t { Unchanged, RockerUpper, RockerLower, None };

// NavNext/NavPrevious and the four Screen* directions are resolved here rather
// than through mapButton's recursion. The two are the same today:
// isNavDirectionSwapped() is constant false and mapScreenDirection() is the
// identity in portrait (MappedInputManager.cpp:12-41), which is the only
// orientation the Picco's UI draws in.
constexpr Route rockerRoute(const Button button) {
  switch (button) {
    case Button::Right:
    case Button::NavNext:
    case Button::ScreenRight:
      return Route::RockerUpper;
    case Button::Left:
    case Button::NavPrevious:
    case Button::ScreenLeft:
      return Route::RockerLower;
    case Button::Up:
    case Button::Down:
    case Button::ScreenUp:
    case Button::ScreenDown:
    case Button::PageBack:
    case Button::PageForward:
    case Button::PageNext:
    case Button::PagePrevious:
      return Route::None;
    case Button::Back:
    case Button::Confirm:
    case Button::Power:
      return Route::Unchanged;
  }
  return Route::Unchanged;
}

// --- Gestures ---------------------------------------------------------------

// The SDK's touch thresholds, mirrored: they are private to InputManager
// (freeink-sdk InputManager.h:326-330), and this decoder must classify a
// contact exactly as the SDK's own tap/swipe/long-press queries do, so a tap
// the keyboard types and a tap this reads as Right are the same tap.
constexpr int TAP_SLOP_PX = 28;                        // a hold must stay within this
constexpr int SWIPE_MIN_PX = 60;                       // a swipe travels at least this on one axis
constexpr int TAP_RELEASE_SLOP_PX = SWIPE_MIN_PX - 1;  // a tap never moved past this
constexpr unsigned long SWIPE_MAX_MS = 700;            // a swipe is over within this
constexpr unsigned long LONG_PRESS_MS = 500;           // a still contact this long is a hold

// One frame's view of the glass, in the renderer's LOGICAL portrait pixels.
//   contact: a finger is on the glass (InputManager::isTouchPressed).
//   live:    the contact is still being delivered. False while contact is true
//            means it was ended (InputManager::suppressTouchContact: a keyboard
//            typed with it, or the touch gate moved), and it must not end as a
//            gesture.
struct Sample {
  bool contact = false;
  bool live = false;
  int x = 0;
  int y = 0;
};

struct Result {
  bool fired = false;
  Button button = Button::Confirm;  // meaningful only when fired
  // The hold fires while the finger is still down: the caller ends the contact
  // so its lift cannot also tap (owner's ruling; InputManager.h:117-123).
  bool endContact = false;
};

constexpr Result fire(const Button button, const bool endContact = false) { return Result{true, button, endContact}; }

// A finished contact: down at (sx, sy), last seen at (ex, ey), durationMs on
// the glass, and whether it ever travelled past the tap release slop. Same
// rules as InputManager::wasSwipe (InputManager.cpp:681-693) and wasTouchTap
// (:576-583): a swipe and a tap cannot both be true of one contact.
inline Result classifyRelease(const int sx, const int sy, const int ex, const int ey, const unsigned long durationMs,
                              const bool movedBeyondTapRelease, const bool keyboard, const int screenW) {
  const int dx = ex - sx;
  const int dy = ey - sy;
  const int adx = std::abs(dx);
  const int ady = std::abs(dy);
  const bool swipe = durationMs <= SWIPE_MAX_MS && (adx >= SWIPE_MIN_PX || ady >= SWIPE_MIN_PX);
  if (swipe) {
    // Left-edge Back first, so it wins over swipe right = Left.
    if (edgeswipe::isLeftEdgeBack(sx, sy, ex, ey, screenW)) return fire(Button::Back);
    if (keyboard) return Result{};
    // Dominant axis, as MappedInputManager::wasSwipe decides it.
    if (adx >= ady) return fire(dx < 0 ? Button::Right : Button::Left);
    return fire(dy < 0 ? Button::Back : Button::Confirm);
  }
  // Inside a keyboard the keyboard itself types the tap.
  if (keyboard || movedBeyondTapRelease) return Result{};
  return fire(Button::Right);
}

// Fed once per input frame (MappedInputManager::update, right after
// gpio.update()), so it sees every contact sample the SDK sees.
class GestureDecoder {
 public:
  Result feed(const Sample& s, const unsigned long nowMs, const bool keyboard, const int screenW) {
    if (s.live) {
      if (!tracking_) {
        tracking_ = true;
        consumed_ = false;
        movedTap_ = false;
        movedRelease_ = false;
        keyboardAtDown_ = keyboard;
        downX_ = s.x;
        downY_ = s.y;
        downAt_ = nowMs;
      }
      // A keyboard opening or closing under the finger ends the contact (the
      // HAL suppresses it too, HalGPIO::setKeyboardTouch).
      if (keyboard != keyboardAtDown_) consumed_ = true;
      if (consumed_) return Result{};
      lastX_ = s.x;
      lastY_ = s.y;
      const int adx = std::abs(lastX_ - downX_);
      const int ady = std::abs(lastY_ - downY_);
      if (adx > TAP_SLOP_PX || ady > TAP_SLOP_PX) movedTap_ = true;
      if (adx > TAP_RELEASE_SLOP_PX || ady > TAP_RELEASE_SLOP_PX) movedRelease_ = true;
      if (!keyboard && !movedTap_ && nowMs - downAt_ >= LONG_PRESS_MS) {
        consumed_ = true;
        return fire(Button::Confirm, true);
      }
      return Result{};
    }
    if (!tracking_) return Result{};
    tracking_ = false;
    // Ended early (s.contact still true) or already spent on a hold.
    if (consumed_ || s.contact || keyboard != keyboardAtDown_) return Result{};
    return classifyRelease(downX_, downY_, lastX_, lastY_, nowMs - downAt_, movedRelease_, keyboard, screenW);
  }

 private:
  bool tracking_ = false;
  bool consumed_ = false;
  bool movedTap_ = false;
  bool movedRelease_ = false;
  bool keyboardAtDown_ = false;
  int downX_ = 0;
  int downY_ = 0;
  int lastX_ = 0;
  int lastY_ = 0;
  unsigned long downAt_ = 0;
};

}  // namespace piccoinput
