#pragma once

#include <cstdint>

// Sleep cleanly at a sustained empty battery instead of running into the
// brownout reset.
//
// Nothing did this before: the device ran until the 3.3 V rail sagged under an
// e-ink refresh and the brownout detector reset the chip -- mid-write as often
// as not, and every boot that followed repeated its state writes on the same
// failing cell. On this target a reset between SdFat's FAT write and its
// directory write (remove, truncate, rename all have that gap) leaves directory
// entries on freed clusters, which the next allocation hands out again as
// cross-links. One deliberate deep sleep is a single clean shutdown.
//
// Pure, so the decision is host-tested (test/low_battery_guard). Two traps it
// is built around:
//   * An unread gauge reads as 0. HalPowerManager's cache starts at 0 and the
//     X3 gauge path returns that cache when a read fails, so "0%" from boot can
//     mean "unknown". The guard only ARMS after it has seen the battery above
//     0% during this boot, so a gauge that never answers never sleeps the
//     device -- the cost is no protection for a boot that starts at 0%.
//   * Sag. 0% is already 3.45 V at rest (BatteryMonitor's curve), and a refresh
//     pulls it lower for a moment; the reading must stay at 0 for SUSTAIN_MS.
class LowBatteryGuard {
 public:
  static constexpr unsigned long CHECK_INTERVAL_MS = 10000;
  static constexpr unsigned long SUSTAIN_MS = 120000;

  // Whether a sample is due. Cheap; gate the battery read on it.
  bool due(unsigned long nowMs) const { return !checkedOnce || nowMs - lastCheckMs >= CHECK_INTERVAL_MS; }

  // Feed one reading. True = sleep now.
  bool sample(unsigned long nowMs, uint16_t percent, bool externalPower) {
    checkedOnce = true;
    lastCheckMs = nowMs;

    if (externalPower || percent > 0) {
      if (percent > 0) armed = true;
      emptyTracking = false;
      return false;
    }
    if (!armed) return false;
    if (!emptyTracking) {
      emptyTracking = true;
      emptySinceMs = nowMs;
      return false;
    }
    return nowMs - emptySinceMs >= SUSTAIN_MS;
  }

 private:
  unsigned long lastCheckMs = 0;
  unsigned long emptySinceMs = 0;
  bool checkedOnce = false;
  bool armed = false;
  bool emptyTracking = false;
};
