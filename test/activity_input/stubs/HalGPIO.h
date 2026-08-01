// Scriptable host HalGPIO — the seam the whole suite hangs off.
//
// Shadows lib/hal/HalGPIO.h (which pulls in the freeink-sdk InputManager and
// real GPIO). The production surface below is a method-for-method copy of the
// real class for everything reachable from activity code; the `sim*` methods at
// the bottom are test-only.
//
// EDGE SEMANTICS ARE THE POINT OF THIS FILE. The SDK documents wasPressed() as
// "press edge since the previous update()" and wasReleased() as "release edge
// since the previous update()" (freeink-sdk/libs/hardware/InputManager/include/
// InputManager.h:26-36), and src/main.cpp:505 calls gpio.update() exactly once
// per loop iteration, immediately before activityManager.loop(). So ONE physical
// button tap delivers its two edges in TWO DIFFERENT FRAMES:
//
//   frame N   : wasPressed(btn)  == true,  isPressed(btn) == true
//   frame N+1 : wasReleased(btn) == true,  isPressed(btn) == false
//
// That gap is the hazard the suite exists to pin: an activity that finishes on
// the press edge is gone by frame N+1, and whoever is current then receives the
// release. On the real device the gap measured ~67 ms (FontSelectionActivity.cpp
// :123-126).
#pragma once

#include <cstdint>

class HalGPIO {
 public:
  enum class DeviceType : uint8_t { X4, X3 };

  HalGPIO() = default;

  // ---- production surface (mirrors lib/hal/HalGPIO.h) ----

  bool deviceIsX3() const { return deviceType_ == DeviceType::X3; }
  bool deviceIsX4() const { return deviceType_ == DeviceType::X4; }

  void begin() {}

  // Commits the raw levels set by simSetDown() and derives this frame's edges,
  // exactly as InputManager::update() does against its debounced state.
  void update() {
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
      pressEdge_[i] = raw_[i] && !level_[i];
      releaseEdge_[i] = !raw_[i] && level_[i];
      level_[i] = raw_[i];
    }
  }

  bool isPressed(uint8_t buttonIndex) const { return valid(buttonIndex) && level_[buttonIndex]; }
  bool wasPressed(uint8_t buttonIndex) const { return valid(buttonIndex) && pressEdge_[buttonIndex]; }
  bool wasReleased(uint8_t buttonIndex) const { return valid(buttonIndex) && releaseEdge_[buttonIndex]; }

  bool wasAnyPressed() const {
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
      if (pressEdge_[i]) return true;
    }
    return false;
  }

  bool wasAnyReleased() const {
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
      if (releaseEdge_[i]) return true;
    }
    return false;
  }

  unsigned long getHeldTime() const { return heldTimeMs_; }
  unsigned long getPowerButtonHeldTime() const { return heldTimeMs_; }

  bool isUsbConnected() const { return false; }
  bool wasUsbStateChanged() const { return false; }

  // ---- test-only scripting ----

  // Sets the RAW level. It becomes visible (and produces an edge) on the next
  // update(), i.e. at the next frame boundary — never mid-frame.
  void simSetDown(uint8_t buttonIndex, bool down) {
    if (valid(buttonIndex)) raw_[buttonIndex] = down;
  }

  // Held time drives ButtonNavigator's continuous (auto-repeat) navigation,
  // which only arms above ButtonNavigator's continuousStartMs (500 ms default).
  void simSetHeldTime(unsigned long ms) { heldTimeMs_ = ms; }

  void simSetDeviceType(DeviceType type) { deviceType_ = type; }

  void simReset() {
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
      raw_[i] = false;
      level_[i] = false;
      pressEdge_[i] = false;
      releaseEdge_[i] = false;
    }
    heldTimeMs_ = 0;
    deviceType_ = DeviceType::X4;
  }

  // Button indices — same values as the real HAL and the SDK.
  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;
  static constexpr uint8_t BTN_COUNT = 7;

 private:
  static bool valid(uint8_t buttonIndex) { return buttonIndex < BTN_COUNT; }

  bool raw_[BTN_COUNT] = {};
  bool level_[BTN_COUNT] = {};
  bool pressEdge_[BTN_COUNT] = {};
  bool releaseEdge_[BTN_COUNT] = {};
  unsigned long heldTimeMs_ = 0;
  DeviceType deviceType_ = DeviceType::X4;
};

extern HalGPIO gpio;
