#include <gtest/gtest.h>

#include "src/util/LowBatteryGuard.h"

namespace {

constexpr unsigned long STEP = LowBatteryGuard::CHECK_INTERVAL_MS;

// Feed `percent` every interval from `start` until `end` (inclusive); returns
// the time sleep was requested, or 0 if it never was.
unsigned long run(LowBatteryGuard& g, unsigned long start, unsigned long end, uint16_t percent, bool usb = false) {
  for (unsigned long t = start; t <= end; t += STEP) {
    if (g.due(t) && g.sample(t, percent, usb)) return t;
  }
  return 0;
}

}  // namespace

TEST(LowBatteryGuard, NeverSleepsWhenItHasNeverSeenAReading) {
  // An X3 gauge that never answers leaves the cache at 0 forever.
  LowBatteryGuard g;
  EXPECT_EQ(run(g, 0, 60 * 60 * 1000UL, 0), 0UL);
}

TEST(LowBatteryGuard, SleepsAfterSustainedEmptyOnceArmed) {
  LowBatteryGuard g;
  EXPECT_EQ(run(g, 0, 50000, 5), 0UL);
  const unsigned long firstEmpty = 60000;
  const unsigned long slept = run(g, firstEmpty, firstEmpty + 10 * 60 * 1000UL, 0);
  EXPECT_EQ(slept, firstEmpty + LowBatteryGuard::SUSTAIN_MS);
}

TEST(LowBatteryGuard, ABriefSagDoesNotSleep) {
  LowBatteryGuard g;
  run(g, 0, 50000, 3);
  EXPECT_EQ(run(g, 60000, 60000 + LowBatteryGuard::SUSTAIN_MS - STEP, 0), 0UL);
  EXPECT_EQ(run(g, 60000 + LowBatteryGuard::SUSTAIN_MS, 60000 + LowBatteryGuard::SUSTAIN_MS, 1), 0UL);
  // The empty clock restarted: another almost-full window still does not sleep.
  const unsigned long t0 = 60000 + LowBatteryGuard::SUSTAIN_MS + STEP;
  EXPECT_EQ(run(g, t0, t0 + LowBatteryGuard::SUSTAIN_MS - STEP, 0), 0UL);
}

TEST(LowBatteryGuard, ExternalPowerNeverSleeps) {
  LowBatteryGuard g;
  run(g, 0, 50000, 4);
  EXPECT_EQ(run(g, 60000, 60 * 60 * 1000UL, 0, /*usb=*/true), 0UL);
}

TEST(LowBatteryGuard, SamplesAtMostOncePerInterval) {
  LowBatteryGuard g;
  EXPECT_TRUE(g.due(0));
  g.sample(0, 50, false);
  EXPECT_FALSE(g.due(STEP - 1));
  EXPECT_TRUE(g.due(STEP));
}
