// Per-button hold timing — the timer the firmware's hold gestures should have
// been reading all along.
//
// The SDK's InputManager::getHeldTime() is ONE global stopwatch: it is stamped
// when a button goes down while NOTHING else is down
// (freeink-sdk/libs/hardware/InputManager/src/InputManager.cpp:252-254) and
// reports millis() minus that stamp for as long as anything stays down. So in
// a chord it measures the FIRST button, whichever one is asked about. "Hold
// Right to page through the keyboard, tap Confirm" made Confirm's hold fire on
// Confirm's first frame — clear-all on the keyboard, a font deactivation in the
// font picker, the delete prompt in the file browser
// (docs/ux-navigation-audit-2026-09-02.md, F4/F9/F10).
//
// Two activities had already worked around it by stamping their own press
// (EpubReaderActivity's chord, DaisyEntryActivity's pick button). This is that
// pattern, once, for every button, fed by MappedInputManager::update() and read
// through MappedInputManager::getHeldTime(Button).
//
// Pure — no HAL, no clock — so the chord can be truth-tabled on a host with an
// explicit `now` rather than judged with a stopwatch on a device.
#pragma once

#include <cstdint>

namespace buttonhold {

// One slot per physical button, HalGPIO::BTN_BACK .. BTN_POWER. The masks
// are uint8_t, so this cannot silently grow past 8; MappedInputManager
// static_asserts it against HalGPIO::BTN_COUNT where both are visible.
constexpr uint8_t kButtons = 7;
static_assert(kButtons <= 8, "hold masks are uint8_t");

struct Timer {
  unsigned long pressStartMs[kButtons] = {};
  unsigned long releasedHeldMs[kButtons] = {};
  bool down[kButtons] = {};
  bool releasedThisFrame[kButtons] = {};
  // A press edge has been seen for the current (or last) press. Without it
  // a button already down when the first frame arrives -- an edge consumed
  // by a gpio.update() outside the pump -- would read millis() minus 0:
  // uptime, past every threshold, an instant spurious long press.
  bool started[kButtons] = {};

  // Once per frame, after the HAL has derived this frame's edges. Bit i of
  // each mask is physical button i.
  void frame(const unsigned long nowMs, const uint8_t pressEdges, const uint8_t releaseEdges, const uint8_t levels) {
    for (uint8_t i = 0; i < kButtons; ++i) {
      const bool bit = ((pressEdges >> i) & 1u) != 0;
      const bool rel = ((releaseEdges >> i) & 1u) != 0;
      // Its own press's length, kept for exactly the frame the release edge is
      // visible: the release-driven hold checks (FileBrowserActivity's delete,
      // FileManagerActivity's action-menu stall catch) read it there, where
      // the global timer answered 0 on the simulator and whatever the chord's
      // first button measured on the device.
      releasedThisFrame[i] = rel;
      if (rel) {
        releasedHeldMs[i] = started[i] ? nowMs - pressStartMs[i] : 0;
        // The press is spent. Without this, `started` covered only a
        // button's FIRST unseen press: a later level with no press edge
        // (one consumed by a gpio.update() outside the pump -- the boot-time
        // waitForPowerRelease poll, which on iOS survives the longjmp reboot
        // together with this timer) read now minus the PREVIOUS press's
        // stamp. Adversarial review 2026-09-04, latent.
        started[i] = false;
      }
      // The press AFTER the release, so a release and a re-press of the same
      // button in one frame (a host can: a KEY_UP and KEY_DOWN inside one
      // ~10 ms pump, two queued QTAPs on one button) leaves the NEW press
      // armed rather than dead for its whole hold. The device cannot produce
      // that shape -- its edges are level diffs -- and its synthesized click
      // (press+release, level 0) still reads 0 below.
      if (bit) {
        pressStartMs[i] = nowMs;
        started[i] = true;
      }
      down[i] = ((levels >> i) & 1u) != 0;
    }
  }

  // How long THIS button's current press has lasted: live while it is down,
  // the finished length on its release frame, 0 when it is not in play. Never
  // another button's time.
  unsigned long heldMs(const uint8_t i, const unsigned long nowMs) const {
    if (i >= kButtons) return 0;
    // The release frame first: its press was spent by frame(), so `started`
    // is already false there and the finished length lives in releasedHeldMs.
    if (releasedThisFrame[i]) return releasedHeldMs[i];
    if (!started[i]) return 0;
    if (down[i]) return nowMs - pressStartMs[i];
    return 0;
  }
};

}  // namespace buttonhold
