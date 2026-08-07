// Host HalClock stub.
//
// Shadows lib/hal/HalClock.h, which includes the freeink-sdk <Rtc.h> and from
// there BoardConfig.h -> ESP-IDF <driver/gpio.h>. That include chain is the
// only reason the real theme stack was previously unlinkable host-side:
// BaseTheme.cpp includes <HalClock.h> even though nothing in it reads the
// clock (grep for `halClock` in src/components/themes — no hits).
//
// The surface below therefore exists to satisfy the compiler, not to model a
// clock: every method reports "no RTC", which is what a device without one
// does. If a future host test wants a fake time, make it settable here rather
// than linking the real HAL.
#pragma once

#include <cstddef>
#include <cstdint>

class HalClock;
extern HalClock halClock;  // Singleton, defined in HostThemeStack.cpp

class HalClock {
 public:
  void begin() {}
  bool isAvailable() const { return false; }
  bool getTime(uint8_t&, uint8_t&) const { return false; }
  bool getDateTime(uint16_t&, uint8_t&, uint8_t&, uint8_t&, uint8_t&) const { return false; }
  bool formatTime(char* buf, size_t bufSize, uint8_t = 48, bool = false) const {
    if (buf && bufSize) buf[0] = '\0';
    return false;
  }
  bool syncFromNTP() { return false; }
};
