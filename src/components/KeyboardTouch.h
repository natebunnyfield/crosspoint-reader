// Touch for the on-screen keyboards, BOOX Picco only.
//
// Owner, 2026-11-05: "make a touch version just for the keyboard on pico". That
// narrows docs/daisywheel.md's 2026-08-05 ruling (this fork never supports
// touch) to exactly one exception: on the Picco, a tap on an on-screen key does
// what selecting that key with the buttons and confirming it does, and a hold
// does what holding Confirm on it does. Nothing else on any board takes touch;
// HalGPIO::setKeyboardTouch is the gate.
//
// Everything here is the screen-point-to-key mapping, kept pure so it is
// host-tested (test/keyboard_touch): a point in, a key out, in the renderer's
// LOGICAL portrait space. That is the space MappedInputManager::wasScreenTapped
// and friends already deliver taps in (GfxRenderer::tapToLogical), and the one
// every keyboard is drawn in; GfxRenderer's orientation is a compile-time
// Portrait (GfxRenderer.h:74), so there is no other orientation to map.
//
// Nothing here draws, and nothing here may change what a keyboard looks like:
// the grids' hit rects come from running fui::keyboard() itself against a
// target that draws nothing, so they are the drawn keys by construction.
//
// Header-only on purpose: a new translation unit breaks the simulator's iOS
// source list (CLAUDE.md). Included only from FREEINK_DEVICE_PICCO code and the
// host test, so no other board compiles any of it.
#pragma once

#include <components/keyboard/keyboard.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace kbtouch {

namespace fui = freeink::ui;

// Hit slots for the largest layout a keyboard shows: the 13-Grid is 5 rows of
// 13 (notes/Grid13Layout.h SL_LAYOUT), 65 keys. KeyboardEntryActivity's
// InteractionBuffer<64> holds 64, so on the Sticky its 'z' registers no hit
// rect (addInteraction refuses the 65th, FreeInkUICore.h:1017-1020).
static constexpr size_t KEY_SLOTS = 72;

// Hold thresholds, the full-screen keyboard's touch ones (KeyboardEntryActivity.h
// TOUCH_LONG_PRESS_MS / TOUCH_DEL_LONG_PRESS_MS, upstream's values): a hold types
// the alt or uppercase, and a hold on Del clears the field.
static constexpr uint16_t HOLD_MS = 350;
static constexpr uint16_t DEL_HOLD_MS = 900;

// A DrawTarget that draws nothing, so fui::keyboard() can be run for the hit
// rects it registers alone. Text metrics are zero: they only place labels and
// icons inside a key, never the key itself (keyboard.h:827-853).
class HitOnlyTarget final : public fui::DrawTarget {
 public:
  fui::Size measureText(fui::FontId, const char*, fui::TextStyle) const override { return {}; }
  int16_t lineHeight(fui::FontId) const override { return 0; }
  void fill(fui::Rect, fui::Paint, uint8_t, uint8_t) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect, const char*, fui::TextStyle) override {}
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}
};

// Rebuilds `hits` with exactly the key hit rects fui::keyboard() registers when
// it lays `props` out in `rect` on a screenW x screenH screen: key rects, gaps,
// the minimum touch size, bottomHitOverflow and the 12 px screen-edge snap
// (FreeInkUI.cpp ensureMinTouchRect) all come from keyboard() itself. The
// buffer's own routing (InteractionBuffer::route, or fui::TouchHoldRouter)
// then answers which key a point is on. `props` must carry a keyAction and a
// touch inputMask, or nothing registers.
template <size_t N>
void buildKeyHits(fui::InteractionBuffer<N>& hits, const fui::Rect rect, const fui::KeyboardProps& props,
                  const int16_t screenW, const int16_t screenH) {
  HitOnlyTarget target;
  fui::DeviceContext device;
  device.width = screenW;
  device.height = screenH;
  const fui::InputSnapshot noInput{};
  fui::Frame<N> frame(target, device, noInput, hits);
  fui::keyboard(frame, rect, props);
}

// --- Daisy strip (notes::KeyboardPanel::render, daisy branch) ----------------
//
// The strip draws min(n, 7) petal cells of width/visible px side by side from
// x, each split top to bottom into three slots of height/3 px, the selected
// petal in the middle cell. Pixels right of the last cell and below the third
// slot are drawn into by nothing, so they hit nothing.
inline int stripVisibleCells(const int petalCount) { return petalCount < 7 ? petalCount : 7; }

inline bool stripHit(const int x, const int y, const int width, const int height, const int visible, const int px,
                     const int py, int& cell, int& slot) {
  if (visible <= 0) return false;
  const int cellW = width / visible;
  const int slotH = height / 3;
  if (cellW <= 0 || slotH <= 0 || px < x || py < y) return false;
  const int c = (px - x) / cellW;
  const int s = (py - y) / slotH;
  if (c >= visible || s >= 3) return false;
  cell = c;
  slot = s;
  return true;
}

// The petal a strip cell shows when `centerPetal` is the selected one.
inline int stripPetal(const int centerPetal, const int visible, const int cell, const int petalCount) {
  if (petalCount <= 0) return 0;
  return ((centerPetal - visible / 2 + cell) % petalCount + petalCount) % petalCount;
}

// --- Daisy wheel (DaisyEntryActivity) ----------------------------------------
//
// Petal i is the wedge between the spokes at (i - 0.5) and (i + 0.5) petal
// steps, theta = 0 at 12 o'clock and increasing clockwise, between the hub and
// rim circles. Its three slots are an UPRIGHT column at the label radius,
// slotSpacing apart (DaisyEntryActivity::slotCenter), so the slot is chosen by
// the point's height against that column, halfway between slot centers.
inline float wheelStep(const int petalCount) {
  return 2.0f * static_cast<float>(M_PI) / static_cast<float>(petalCount);
}

// DaisyEntryActivity::slotCenter, the same arithmetic (truncating casts
// included), so a slot's own center is guaranteed to hit it.
inline void wheelSlotCenter(const int cx, const int cy, const int hubR, const int outerR, const int petalCount,
                            const int slotSpacing, const int petal, const int slot, int& outX, int& outY) {
  const float theta = static_cast<float>(petal) * wheelStep(petalCount);
  const float rm = static_cast<float>(hubR + outerR) / 2.0f;
  outX = cx + static_cast<int>(rm * sinf(theta));
  outY = cy - static_cast<int>(rm * cosf(theta)) + (slot - 1) * slotSpacing;
}

inline bool wheelHit(const int cx, const int cy, const int hubR, const int outerR, const int petalCount,
                     const int slotSpacing, const int px, const int py, int& petal, int& slot) {
  if (petalCount <= 0 || slotSpacing <= 0) return false;
  const int dx = px - cx;
  const int dy = py - cy;
  const int r2 = dx * dx + dy * dy;
  if (r2 < hubR * hubR || r2 > outerR * outerR) return false;  // the empty hub, or off the rim
  const float step = wheelStep(petalCount);
  float theta = atan2f(static_cast<float>(dx), static_cast<float>(-dy));
  if (theta < 0.0f) theta += 2.0f * static_cast<float>(M_PI);
  const int p = static_cast<int>(floorf(theta / step + 0.5f)) % petalCount;
  int midX = 0;
  int midY = 0;
  wheelSlotCenter(cx, cy, hubR, outerR, petalCount, slotSpacing, p, 1, midX, midY);
  int s = 1 + static_cast<int>(floorf(static_cast<float>(py - midY) / static_cast<float>(slotSpacing) + 0.5f));
  if (s < 0) s = 0;
  if (s > 2) s = 2;
  petal = p;
  slot = s;
  return true;
}

// --- Tap / hold routing -------------------------------------------------------

// One loop pass of touch, read through MappedInputManager in logical space.
struct TouchInput {
  bool pressedDown = false;  // a tap-candidate contact is down (every pass while held)
  int16_t px = 0;
  int16_t py = 0;
  bool tapped = false;  // a tap was released this pass (at its touch-DOWN point)
  int16_t tx = 0;
  int16_t ty = 0;
  bool inContact = false;  // any contact at all, ungated (drag tracking)
};

template <class Input>
TouchInput readTouch(const Input& input) {
  TouchInput t;
  int x = 0;
  int y = 0;
  if (input.wasScreenTouchDown(x, y)) {
    t.pressedDown = true;
    t.px = static_cast<int16_t>(x);
    t.py = static_cast<int16_t>(y);
  }
  if (input.wasScreenTapped(x, y)) {
    t.tapped = true;
    t.tx = static_cast<int16_t>(x);
    t.ty = static_cast<int16_t>(y);
  }
  t.inContact = input.isScreenTouchHeld(x, y);
  return t;
}

// Forget a grid contact in progress: the router's own state AND the key the
// table holds as active. TouchHoldRouter::reset() clears only the former, and
// the router dispatches the table's active key for a tap that lands off every
// key (keyboard.h:351-356); a tap shorter than the 90 ms touch-down delay
// arrives with no press pass to replace it, so a stale active key would type.
// The empty release lands at (0,0), which no keyboard here puts a key on
// (each sits at the bottom of the screen): it clears the active key and
// dispatches nothing.
template <size_t N>
void resetGridTouch(fui::TouchHoldRouter& router, fui::InteractionBuffer<N>& hits) {
  router.reset();
  fui::InputSnapshot cancel{};
  cancel.touchReleased = true;
  hits.routePublished(cancel);
}

// fui::TouchHoldRouter's contract (keyboard.h:291-424), for keys that are not
// rects in an InteractionBuffer: the daisy wheel's polar slots and the daisy
// strip. hit(x, y) returns a key id >= 0, or -1 off every key. One update() per
// loop pass with that pass's TouchInput:
//   * a hold fires at holdMs WHILE the finger is still down, and the release
//     that ends it is swallowed;
//   * a tap whose point is on no key dispatches the key the press was on;
//   * contact that ends without a tap (a drag) resets, so the next press
//     times its hold from its own start.
class HoldRouter {
 public:
  uint16_t holdMs = HOLD_MS;

  struct Result {
    int16_t key = -1;
    bool longPress = false;
    explicit operator bool() const { return key >= 0; }
  };

  template <class Hit>
  Result update(Hit&& hit, const TouchInput& in, const uint32_t nowMs) {
    Result result;
    if (in.tapped) {
      const bool swallow = longFired_;
      const int16_t pressed = active_;
      held_ = false;
      longFired_ = false;
      active_ = -1;
      if (!swallow) {
        result.key = static_cast<int16_t>(hit(in.tx, in.ty));
        if (result.key < 0) result.key = pressed;
      }
      return result;
    }
    if (in.pressedDown) {
      if (longFired_) return result;
      const int16_t key = static_cast<int16_t>(hit(in.px, in.py));
      if (!held_ || key != active_) {
        held_ = true;
        startMs_ = nowMs;
      }
      active_ = key;
      if (active_ >= 0 && nowMs - startMs_ >= holdMs) {
        longFired_ = true;
        result.key = active_;
        result.longPress = true;
        active_ = -1;
      }
      return result;
    }
    if (held_ && !in.inContact) reset();
    return result;
  }

  void reset() {
    held_ = false;
    longFired_ = false;
    active_ = -1;
    startMs_ = 0;
  }

 private:
  bool held_ = false;
  bool longFired_ = false;
  int16_t active_ = -1;
  uint32_t startMs_ = 0;
};

}  // namespace kbtouch
