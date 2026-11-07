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

// Owner ruling 2026-11-06, "Match iOS exactly" (docs/picco-port-2026-11-05.md):
// the iOS zen thresholds, not the SDK's. A tap is one contact that never
// travels more than 28 px from where it landed and lifts within 400 ms
// (crosspoint-simulator ios/ZenVerbs.h:56-57 and :131-132, both inclusive); a
// hold is a still contact at 750 ms (ios/ZenHoldRouting.h:38). Anything longer
// than a tap, shorter than a hold and not a swipe does NOTHING, and so does a
// contact that drifts past the tap slop without becoming a swipe.
//
// These apply to the gesture decoder alone. The keyboards keep the SDK's own
// tap and hold queries and their own timings (KeyboardEntryActivity.h:167-170),
// which the ruling leaves alone; a keyboard tap and a tap read as Right are no
// longer the same test, and do not need to be: inside a keyboard this decoder
// fires nothing but the left-edge Back.
constexpr int TAP_SLOP_PX = 28;               // a tap or a hold never moves past this
constexpr unsigned long TAP_MAX_MS = 400;     // a tap lifts within this
constexpr unsigned long LONG_PRESS_MS = 750;  // a still contact this long is a hold
// Swipes are the SDK's (InputManager.h:327-329), unchanged by the ruling.
constexpr int SWIPE_MIN_PX = 60;             // a swipe travels at least this on one axis
constexpr unsigned long SWIPE_MAX_MS = 700;  // a swipe is over within this
// A hold needs the TMA525C to keep reporting the contact. Its lift-off is a
// report like any other (freeink-sdk InputManager.cpp:1231), and when one is
// lost the SDK keeps the finger "down" for 3 s (:1116, :1275) -- long
// enough for a tap to sit there past LONG_PRESS_MS and fire Confirm on a
// finger that left long ago (review minor, 2026-11-06). So a gap longer than
// this between two reports, at any point in the hold window, disqualifies the
// contact from holding, and the last report must be this fresh when the hold
// fires. 200 ms is twenty polls at the 100 Hz contact poll; it is a guess at
// "the controller went quiet", not a measured report rate (UNCONFIRMED on
// device: if the controller pauses on a resting finger for longer, real holds
// stop firing, and this number is the one to move).
constexpr unsigned long HOLD_REPORT_GAP_MS = 200;

// One frame's view of the glass, in the renderer's LOGICAL portrait pixels.
//   contact:  a finger is on the glass (InputManager::isTouchPressed).
//   live:     the contact is still being delivered. False while contact is true
//             means it was ended (InputManager::suppressTouchContact: a keyboard
//             typed with it, or the touch gate moved), and it must not end as a
//             gesture.
//   reportMs: millis() of the controller's latest contact report
//             (InputManager::getTouchPoint().timestamp). Unchanged from one
//             frame to the next means no new report arrived in between.
struct Sample {
  bool contact = false;
  bool live = false;
  int x = 0;
  int y = 0;
  unsigned long reportMs = 0;
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
// the glass, and whether it ever travelled past the 28 px tap slop. A swipe
// is the SDK's rule (InputManager::wasSwipe, InputManager.cpp:681-693); a tap
// is iOS's (above). The two cannot both be true of one contact: a swipe has
// travelled 60 px, a tap never more than 28.
inline Result classifyRelease(const int sx, const int sy, const int ex, const int ey, const unsigned long durationMs,
                              const bool movedBeyondTapSlop, const bool keyboard, const int screenW) {
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
  if (keyboard || movedBeyondTapSlop || durationMs > TAP_MAX_MS) return Result{};
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
        reportGap_ = false;
        keyboardAtDown_ = keyboard;
        downX_ = s.x;
        downY_ = s.y;
        downAt_ = nowMs;
        lastReportMs_ = s.reportMs;
      }
      // A keyboard opening or closing under the finger ends the contact (the
      // HAL suppresses it too, HalGPIO::setKeyboardTouch).
      if (keyboard != keyboardAtDown_) consumed_ = true;
      if (consumed_) return Result{};
      if (s.reportMs != lastReportMs_) {
        if (s.reportMs - lastReportMs_ > HOLD_REPORT_GAP_MS) reportGap_ = true;
        lastReportMs_ = s.reportMs;
      }
      lastX_ = s.x;
      lastY_ = s.y;
      const int adx = std::abs(lastX_ - downX_);
      const int ady = std::abs(lastY_ - downY_);
      if (adx > TAP_SLOP_PX || ady > TAP_SLOP_PX) movedTap_ = true;
      // The gap still open counts too: a lost lift-off leaves no later report
      // to measure it against.
      const bool reported = !reportGap_ && nowMs - lastReportMs_ <= HOLD_REPORT_GAP_MS;
      if (!keyboard && !movedTap_ && reported && nowMs - downAt_ >= LONG_PRESS_MS) {
        consumed_ = true;
        return fire(Button::Confirm, true);
      }
      return Result{};
    }
    if (!tracking_) return Result{};
    tracking_ = false;
    // Ended early (s.contact still true) or already spent on a hold.
    if (consumed_ || s.contact || keyboard != keyboardAtDown_) return Result{};
    return classifyRelease(downX_, downY_, lastX_, lastY_, nowMs - downAt_, movedTap_, keyboard, screenW);
  }

 private:
  bool tracking_ = false;
  bool consumed_ = false;
  bool movedTap_ = false;
  bool reportGap_ = false;
  bool keyboardAtDown_ = false;
  int downX_ = 0;
  int downY_ = 0;
  int lastX_ = 0;
  int lastY_ = 0;
  unsigned long downAt_ = 0;
  unsigned long lastReportMs_ = 0;
};

// --- A gesture on a button that is already held ----------------------------

// A synthesized press on a physical button that is physically down would
// release it a frame later (HalGPIO::update turns the synthesized level into a
// release edge), ending the real press early: a gesture made while the rocker
// end it maps to is held read as that end being let go (review minor,
// 2026-11-06). Such a gesture is dropped. `physicallyHeld` is a bit per
// physical button index (HalGPIO::BTN_*), as MappedInputManager reads it.
constexpr bool canSynthesizeTap(const uint8_t physicallyHeld, const uint8_t index) {
  return index < 8 && ((physicallyHeld >> index) & 1u) == 0;
}

// --- The idle poll ----------------------------------------------------------

// After 1 s idle the main loop light-sleeps in 50 ms slices and samples the
// glass once per slice (main.cpp loop, HalPowerManager::lightSleep), so a tap
// arriving there is seen late and a short one can be missed. It polls at
// 10 ms instead while a contact is down, and for this long after the
// controller's last contact report, so the next tap of a run is sampled at
// 100 Hz from its first report. Same length as the post-activity 100 Hz window
// (HalPowerManager::IDLE_LIGHT_SLEEP_MS), which a recognized gesture already
// gets because its synthesized press is activity.
constexpr unsigned long CONTACT_FAST_POLL_MS = 1000;

// lastReportMs is InputManager::getTouchPoint().timestamp: 0 until the first
// report, then the time of the latest one (a lift-off clears `valid`, not the
// timestamp: freeink-sdk InputManager.cpp:1238-1246).
constexpr bool pollFast(const bool contactDown, const unsigned long nowMs, const unsigned long lastReportMs) {
  return contactDown || (lastReportMs != 0 && nowMs - lastReportMs < CONTACT_FAST_POLL_MS);
}

}  // namespace piccoinput
