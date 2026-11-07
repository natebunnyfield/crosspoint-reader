#pragma once

// Thin firmware HAL over the SDK frontlight manager. Ported from upstream
// (added bbca4886c; brightness-before-warmth ordering from 8f4d035cb) for the
// BOOX Picco, whose warm/cool LED driver sits on I2C.
//
// FORK NOTE: only the Picco env links FrontlightManager (platformio.ini
// [env:picco] lib_deps), because no other board this fork builds for drives a
// frontlight. Everything below therefore compiles only into that env; include
// this header from Picco-gated code only. The simulator ships its own
// HalFrontlight with the same API (crosspoint-simulator/src/HalFrontlight.h).
#if FREEINK_DEVICE_PICCO

#include <FrontlightManager.h>

class HalFrontlight {
 public:
  static HalFrontlight& getInstance() { return instance; }

  void begin(uint8_t brightness, uint8_t warmth, bool on);

  bool present() const { return manager.present(); }
  bool hasColorTemperature() const { return manager.hasColorTemperature(); }

  void setBrightness(uint8_t percent);
  void setWarmth(uint8_t warmPercent);
  void setOn(bool on);

  uint8_t brightness() const { return lastBrightness; }
  uint8_t warmth() const { return manager.colorTemperature(); }
  bool isOn() const { return lit; }

 private:
  HalFrontlight() = default;

  FrontlightManager manager;
  // The SDK represents off as brightness 0. Keep the selected brightness so
  // toggling back on restores it.
  uint8_t lastBrightness = 60;
  bool lit = false;

  static HalFrontlight instance;
};

#define Frontlight HalFrontlight::getInstance()

#endif  // FREEINK_DEVICE_PICCO
