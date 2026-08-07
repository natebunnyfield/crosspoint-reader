// Host HalPowerManager stub.
//
// Shadows lib/hal/HalPowerManager.h, which includes the freeink-sdk
// <BatteryMonitor.h> / <InputManager.h> and reads BOARD_HAS_PSRAM from
// BoardConfig.h -> ESP-IDF. BaseTheme.cpp calls exactly one method on it,
// `powerManager.getBatteryPercentage()` (BaseTheme.cpp:81, 97), from the
// battery-icon draw paths.
//
// The percentage is settable so a future render-path test can assert on the
// icon; it defaults to a value that is neither empty nor full, so a test that
// forgets to set it still exercises the partial-fill branch.
#pragma once

#include <cstdint>

class HalGPIO;

class HalPowerManager;
extern HalPowerManager powerManager;  // Singleton, defined in HostThemeStack.cpp

class HalPowerManager {
 public:
  void begin() {}
  void setPowerSaving(bool) {}
  void startDeepSleep(HalGPIO&) const {}

  uint16_t getBatteryPercentage() const { return batteryPercent_; }

  // Test-only.
  void simSetBatteryPercentage(uint16_t percent) { batteryPercent_ = percent; }

  class Lock {
   public:
    explicit Lock() = default;
    ~Lock() = default;
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    Lock(Lock&&) = delete;
    Lock& operator=(Lock&&) = delete;
  };

 private:
  uint16_t batteryPercent_ = 65;
};
