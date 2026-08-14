# Ring clock — rescued, parked, and memorialised

**Status: NOT in main, not on any branch. This file is the only copy.**

An unfinished ring-clock face for the home screen. It was found on 2026-08-03 as
451 lines of uncommitted work in the `~/src/xteink/crosspoint-reader` clone — a
checkout that no longer exists — and committed to `rescue/ring-clock` purely so
it would survive. It was never reviewed, never built against this fork, and
never seen running. On 2026-08-14 the branch was retired and its diff moved
here, because a branch 1015 commits divergent reads as live work when it is not,
and every branch audit had to re-litigate it.

Original commit: `2e4d4ac6a` "wip: ring clock activity (rescued from an uncommitted
working tree)", parented on `bfa1d706b` (upstream's "Hidden wifi ssid support",
#2360). 13 files, +463/-18.

## What it does

`RingClockFace.h` draws an analogue clock as concentric rings rather than hands —
the bulk of the work and the only part with any design in it. `RingClockActivity`
wraps it in the activity lifecycle, `HomeActivity` gains an entry point, and a
`clock.h` icon plus a `UIIcon::Clock` enum member wire it into the menu.

## What has changed under it since

Read this before assuming the diff applies. It does not.

* **`src/components/icons/clock.h` does not exist in main**, and `UIIcon`
  (`BaseTheme.h:114-130`) has no `Clock` member. The enum has moved on
  considerably — `ManageFiles`, `CreateNote` and `ClaudeMark` all postdate this
  work. Adding a member is easy; just do not expect the hunk to apply.
* **The HAL half is already in main, under a different name.** The WIP added
  `getDate()` and `writeDateTimeToRTC()` to `HalClock`; main has
  `getDateTime(year, month, day, hour, minute)` (`HalClock.h:39`) plus the
  caching and 10-second poll the WIP was also adding. So roughly a third of this
  diff is redundant — take the face, not the HAL hunks.
* **Four of the five themes are gone** (2026-08-04). The `LyraTheme.cpp` hunk
  targets a class that still exists, but only as a layer of the
  `BaseTheme <- LyraTheme <- Lyra3CoversTheme <- LyraSixTheme` chain; Lyra Six is
  the only theme instantiated. Anything the face assumes about theme metrics
  needs re-reading against `LyraSixMetrics`.
* **`HomeActivity` has been reworked** — the selector clamps rather than wraps on
  the two-page Lyra Six home (`HomeActivity.cpp:210-217`), which did not exist
  when this was written.

## If you pick this up

It is a **port, not a rebase**. Cherry-picking will conflict in every integration
file and silently reintroduce a stale HAL API. The order that works:

1. Take `RingClockFace.h` as-is and get it compiling standalone. It is the only
   file with irreplaceable content.
2. Render it through `tools/calendar_preview` (the host harness links the real
   `GfxRenderer`) before wiring any UI. If the face does not look good, stop —
   everything else is plumbing worth nothing on its own.
3. Only then add the `UIIcon` member, the icon header and the HomeActivity entry,
   written fresh against current main rather than from these hunks.
4. Drop the `HalClock` hunks entirely; use `getDateTime`.

Nobody has ever seen this render. Treat "it works" as unproven in both
directions.

## The diff, in full

```diff
diff --git a/lib/I18n/translations/english.yaml b/lib/I18n/translations/english.yaml
index aac1ea0c3..9af948a8c 100644
--- a/lib/I18n/translations/english.yaml
+++ b/lib/I18n/translations/english.yaml
@@ -261,6 +261,8 @@ STR_CLOCK_SYNC_FAIL: "Sync failed"
 STR_CLOCK_SYNC_NO_WIFI: "Wi-Fi not connected"
 STR_CLOCK_SYNC_NO_WIFI_HINT: "Connect to Wi-Fi first, then try again."
 STR_CLOCK_SYNCED: "Clock Synced"
+STR_RING_CLOCK: "Ring Clock"
+STR_RING_CLOCK_NO_RTC: "The ring clock needs the X3 real-time clock"
 STR_UI_THEME: "UI Theme"
 STR_THEME_CLASSIC: "Classic"
 STR_THEME_LYRA: "Lyra"
diff --git a/lib/hal/HalClock.cpp b/lib/hal/HalClock.cpp
index 6e3c3a90b..314ff4f06 100644
--- a/lib/hal/HalClock.cpp
+++ b/lib/hal/HalClock.cpp
@@ -57,7 +57,8 @@ bool HalClock::getTime(uint8_t& hour, uint8_t& minute) const {
     return true;
   }
 
-  // Read 3 bytes starting at register 0x00: seconds, minutes, hours
+  // Read 7 bytes starting at register 0x00:
+  // seconds, minutes, hours, weekday, date, month/century, year
   Wire.beginTransmission(I2C_ADDR_DS3231);
   Wire.write(DS3231_SEC_REG);
   if (Wire.endTransmission(false) != 0) {
@@ -67,8 +68,8 @@ bool HalClock::getTime(uint8_t& hour, uint8_t& minute) const {
     minute = _cachedMinute;
     return true;
   }
-  Wire.requestFrom(I2C_ADDR_DS3231, (uint8_t)3);
-  if (Wire.available() < 3) {
+  Wire.requestFrom(I2C_ADDR_DS3231, (uint8_t)7);
+  if (Wire.available() < 7) {
     if (!_hasCachedTime) return false;
     _lastPollMs = now;
     hour = _cachedHour;
@@ -79,8 +80,17 @@ bool HalClock::getTime(uint8_t& hour, uint8_t& minute) const {
   Wire.read();  // seconds — not needed
   const uint8_t rawMin = Wire.read();
   const uint8_t rawHour = Wire.read();
+  Wire.read();  // weekday — not needed
+  const uint8_t rawDay = Wire.read();
+  const uint8_t rawMonth = Wire.read();
+  const uint8_t rawYear = Wire.read();
 
   _cachedMinute = bcdToDec(rawMin & 0x7F);
+  _cachedDay = bcdToDec(rawDay & 0x3F);
+  _cachedMonth = bcdToDec(rawMonth & 0x1F);  // bit 7 is the century flag
+  _cachedYear = 2000 + bcdToDec(rawYear);
+  if (_cachedDay < 1 || _cachedDay > 31) _cachedDay = 1;
+  if (_cachedMonth < 1 || _cachedMonth > 12) _cachedMonth = 1;
   // Handle 12/24h mode: bit 6 high = 12h mode
   if (rawHour & 0x40) {
     // 12h mode: bit 5 = PM, bits 4-0 = hours (1-12)
@@ -100,6 +110,17 @@ bool HalClock::getTime(uint8_t& hour, uint8_t& minute) const {
   return true;
 }
 
+bool HalClock::getDate(uint16_t& year, uint8_t& month, uint8_t& day) const {
+  if (!_available) return false;
+  // getTime() owns the poll/caching; it refreshes the date cache as a side effect.
+  uint8_t h, m;
+  if (!getTime(h, m)) return false;
+  year = _cachedYear;
+  month = _cachedMonth;
+  day = _cachedDay;
+  return true;
+}
+
 bool HalClock::formatTime(char* buf, size_t bufSize, uint8_t utcOffsetQuarterHoursBiased, bool use12Hour) const {
   if (bufSize < (use12Hour ? 9u : 6u)) return false;
   uint8_t h, m;
@@ -127,17 +148,26 @@ bool HalClock::formatTime(char* buf, size_t bufSize, uint8_t utcOffsetQuarterHou
   return true;
 }
 
-bool HalClock::writeTimeToRTC(uint8_t hour, uint8_t minute, uint8_t second) {
+bool HalClock::writeDateTimeToRTC(uint8_t hour, uint8_t minute, uint8_t second, uint8_t weekday, uint8_t day,
+                                  uint8_t month, uint16_t year) {
   assert(hour < 24);
   assert(minute < 60);
   assert(second < 60);
+  assert(weekday >= 1 && weekday <= 7);
+  assert(day >= 1 && day <= 31);
+  assert(month >= 1 && month <= 12);
+  const uint8_t yy = static_cast<uint8_t>(year >= 2000 ? (year - 2000) % 100 : 0);
   Wire.beginTransmission(I2C_ADDR_DS3231);
-  Wire.write(DS3231_SEC_REG);    // Start at register 0x00
-  Wire.write(decToBcd(second));  // 0x00: Seconds
-  Wire.write(decToBcd(minute));  // 0x01: Minutes
-  Wire.write(decToBcd(hour));    // 0x02: Hours (24h mode, bit 6 = 0)
+  Wire.write(DS3231_SEC_REG);     // Start at register 0x00
+  Wire.write(decToBcd(second));   // 0x00: Seconds
+  Wire.write(decToBcd(minute));   // 0x01: Minutes
+  Wire.write(decToBcd(hour));     // 0x02: Hours (24h mode, bit 6 = 0)
+  Wire.write(decToBcd(weekday));  // 0x03: Day of week (1-7)
+  Wire.write(decToBcd(day));      // 0x04: Date (1-31)
+  Wire.write(decToBcd(month));    // 0x05: Month (1-12, century bit 0)
+  Wire.write(decToBcd(yy));       // 0x06: Year (00-99)
   if (Wire.endTransmission() != 0) {
-    LOG_ERR("CLK", "Failed to write time to DS3231");
+    LOG_ERR("CLK", "Failed to write date-time to DS3231");
     return false;
   }
 
@@ -145,6 +175,9 @@ bool HalClock::writeTimeToRTC(uint8_t hour, uint8_t minute, uint8_t second) {
   _lastPollMs = 0;
   _cachedHour = hour;
   _cachedMinute = minute;
+  _cachedDay = day;
+  _cachedMonth = month;
+  _cachedYear = 2000 + yy;
   _hasCachedTime = true;
   return true;
 }
@@ -168,8 +201,10 @@ bool HalClock::syncFromNTP() {
       struct tm timeinfo;
       gmtime_r(&now, &timeinfo);
 
-      if (writeTimeToRTC(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec)) {
-        LOG_INF("CLK", "RTC set to %02d:%02d:%02d UTC", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
+      if (writeDateTimeToRTC(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, timeinfo.tm_wday + 1,
+                             timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900)) {
+        LOG_INF("CLK", "RTC set to %04d-%02d-%02d %02d:%02d:%02d UTC", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
+                timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
         return true;
       }
       return false;
diff --git a/lib/hal/HalClock.h b/lib/hal/HalClock.h
index a0cc48251..f240eb631 100644
--- a/lib/hal/HalClock.h
+++ b/lib/hal/HalClock.h
@@ -12,6 +12,9 @@ class HalClock {
   bool _available = false;
   mutable uint8_t _cachedHour = 0;
   mutable uint8_t _cachedMinute = 0;
+  mutable uint8_t _cachedDay = 1;
+  mutable uint8_t _cachedMonth = 1;
+  mutable uint16_t _cachedYear = 2000;
   mutable bool _hasCachedTime = false;
   mutable unsigned long _lastPollMs = 0;
 
@@ -28,6 +31,11 @@ class HalClock {
   // Returns false if RTC is not available.
   bool getTime(uint8_t& hour, uint8_t& minute) const;
 
+  // Get current date (UTC, like getTime). year is the full year (e.g. 2026).
+  // The date is only meaningful after an NTP sync has written it; a factory
+  // DS3231 reports 2000-01-01. Returns false if RTC is not available.
+  bool getDate(uint16_t& year, uint8_t& month, uint8_t& day) const;
+
   // Format time into a caller-provided buffer.
   // 24h mode produces "HH:MM" (needs >=6 bytes); 12h mode produces "H:MM AM"/"HH:MM PM" (needs >=9 bytes).
   // utcOffsetQuarterHoursBiased: biased quarter-hour offset (48 = UTC+0, 0 = UTC-12, 104 = UTC+14).
@@ -44,5 +52,8 @@ class HalClock {
   bool syncFromNTP();
 
  private:
-  bool writeTimeToRTC(uint8_t hour, uint8_t minute, uint8_t second);
+  // Write the full date-time (UTC) including the DS3231 date registers.
+  // weekday: 1-7 (any consistent convention), day: 1-31, month: 1-12.
+  bool writeDateTimeToRTC(uint8_t hour, uint8_t minute, uint8_t second, uint8_t weekday, uint8_t day, uint8_t month,
+                          uint16_t year);
 };
diff --git a/src/activities/ActivityManager.cpp b/src/activities/ActivityManager.cpp
index 7757d050b..cf1d75023 100644
--- a/src/activities/ActivityManager.cpp
+++ b/src/activities/ActivityManager.cpp
@@ -221,6 +221,8 @@ void ActivityManager::goHome(HomeMenuItem initialMenuItem) {
       initialMenuItem = HomeMenuItem::OPDS_BROWSER;
     } else if (activityName == "CrossPointWebServer") {
       initialMenuItem = HomeMenuItem::FILE_TRANSFER;
+    } else if (activityName == "RingClock") {
+      initialMenuItem = HomeMenuItem::RING_CLOCK;
     } else if (activityName == "Settings") {
       initialMenuItem = HomeMenuItem::SETTINGS_MENU;
     }
diff --git a/src/activities/ActivityManager.h b/src/activities/ActivityManager.h
index 5712ec712..101bc321c 100644
--- a/src/activities/ActivityManager.h
+++ b/src/activities/ActivityManager.h
@@ -17,7 +17,7 @@
 class Activity;    // forward declaration
 class RenderLock;  // forward declaration
 
-enum class HomeMenuItem { NONE, FILE_BROWSER, RECENTS, OPDS_BROWSER, FILE_TRANSFER, SETTINGS_MENU };
+enum class HomeMenuItem { NONE, FILE_BROWSER, RECENTS, OPDS_BROWSER, FILE_TRANSFER, SETTINGS_MENU, RING_CLOCK };
 
 /**
  * ActivityManager
diff --git a/src/activities/clock/RingClockActivity.cpp b/src/activities/clock/RingClockActivity.cpp
new file mode 100644
index 000000000..cd273fc6a
--- /dev/null
+++ b/src/activities/clock/RingClockActivity.cpp
@@ -0,0 +1,111 @@
+#include "RingClockActivity.h"
+
+#include <GfxRenderer.h>
+#include <HalClock.h>
+#include <I18n.h>
+#include <Logging.h>
+
+#include "CrossPointSettings.h"
+#include "MappedInputManager.h"
+#include "RingClockFace.h"
+#include "components/UITheme.h"
+#include "fontIds.h"
+
+bool RingClockActivity::readDisplayState(int& hour12, int& minuteRing, int& month, int& day) const {
+  uint8_t h = 0, mi = 0, mo = 0, d = 0;
+  uint16_t y = 0;
+  if (!halClock.getTime(h, mi)) return false;
+  if (!halClock.getDate(y, mo, d)) return false;
+
+  ringclock::DateTime dt{y, mo, d, h, mi};
+  ringclock::applyUtcOffsetQ(dt, SETTINGS.clockUtcOffsetQ);
+
+  hour12 = dt.hour % 12;
+  if (hour12 == 0) hour12 = 12;
+  minuteRing = dt.minute / 5 - 1;
+  month = dt.month;
+  day = dt.day;
+  return true;
+}
+
+void RingClockActivity::onEnter() {
+  Activity::onEnter();
+  rtcAvailable = readDisplayState(shownHour12, shownMinuteRing, shownMonth, shownDay);
+  if (!rtcAvailable) LOG_INF("RCLK", "RTC unavailable, showing fallback message");
+  requestUpdate();
+}
+
+void RingClockActivity::onExit() {
+  // The clock leaves a mostly-black frame behind; flush a clean white FULL
+  // refresh so the next activity doesn't render on top of heavy ghosting.
+  renderer.clearScreen();
+  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
+  Activity::onExit();
+}
+
+void RingClockActivity::loop() {
+  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
+    finish();
+    return;
+  }
+
+  int hour12, minuteRing, month, day;
+  if (!readDisplayState(hour12, minuteRing, month, day)) return;
+  if (rtcAvailable && hour12 == shownHour12 && minuteRing == shownMinuteRing && month == shownMonth &&
+      day == shownDay) {
+    return;
+  }
+
+  // A read after a transient onEnter failure un-latches the fallback screen.
+  {
+    RenderLock lock;
+    rtcAvailable = true;
+    shownHour12 = hour12;
+    shownMinuteRing = minuteRing;
+    shownMonth = month;
+    shownDay = day;
+  }
+  requestUpdate();
+}
+
+void RingClockActivity::render(RenderLock&&) {
+  if (!rtcAvailable) {
+    renderer.clearScreen();
+    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_RING_CLOCK_NO_RTC));
+    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
+    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
+    renderer.displayBuffer();
+    return;
+  }
+
+  renderer.clearScreen(0x00);  // black background, white ink (inverted design)
+
+  const int w = renderer.getScreenWidth();
+  const int h = renderer.getScreenHeight();
+  int top, right, bottom, left;
+  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
+  const int viewW = w - left - right;
+  const int viewH = h - top - bottom;
+  const int cx = left + viewW / 2;
+  const int cy = top + viewH / 2;
+  int faceRadius = (viewW < viewH ? viewW : viewH) / 2 - 4;
+  if (faceRadius < ringclock::NUM_RINGS * 2) faceRadius = ringclock::NUM_RINGS * 2;
+
+  const ringclock::Geometry geometry = ringclock::computeGeometry(faceRadius);
+  const ringclock::FaceState state{ringclock::sumCombinationMask(shownHour12), shownMinuteRing,
+                                   ringclock::sumCombinationMask(shownMonth), ringclock::sumCombinationMask(shownDay)};
+
+  for (int py = cy - faceRadius; py <= cy + faceRadius; py++) {
+    const int dy = cy - py;
+    for (int px = cx - faceRadius; px <= cx + faceRadius; px++) {
+      if (ringclock::pixelIsWhite(px - cx, dy, geometry, state)) {
+        renderer.drawPixel(px, py, false);  // white
+      }
+    }
+  }
+
+  // Periodic full refresh clears e-ink ghosting from the fast updates.
+  const bool full = rendersSinceFullRefresh == 0 || rendersSinceFullRefresh >= 6;
+  renderer.displayBuffer(full ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
+  rendersSinceFullRefresh = full ? 1 : rendersSinceFullRefresh + 1;
+}
diff --git a/src/activities/clock/RingClockActivity.h b/src/activities/clock/RingClockActivity.h
new file mode 100644
index 000000000..a361342cd
--- /dev/null
+++ b/src/activities/clock/RingClockActivity.h
@@ -0,0 +1,28 @@
+#pragma once
+#include "activities/Activity.h"
+
+// Full-screen segmented ring clock (port of the CoreInk "simple ring clock,
+// inverted"). White-on-black; updates when the displayed value changes
+// (5-minute minute-ring granularity). Requires the X3's DS3231 RTC.
+class RingClockActivity final : public Activity {
+  bool rtcAvailable = false;
+  // Last displayed tuple; render() draws exactly this state.
+  int shownHour12 = -1;
+  int shownMinuteRing = -2;
+  int shownMonth = -1;
+  int shownDay = -1;
+  uint8_t rendersSinceFullRefresh = 0;
+
+  bool readDisplayState(int& hour12, int& minuteRing, int& month, int& day) const;
+
+ public:
+  explicit RingClockActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
+      : Activity("RingClock", renderer, mappedInput) {}
+  void onEnter() override;
+  void onExit() override;
+  void loop() override;
+  void render(RenderLock&&) override;
+  // An always-on clock face is the point; the user exits with Back. Only
+  // while actually showing the clock — the no-RTC fallback may sleep.
+  bool preventAutoSleep() override { return rtcAvailable; }
+};
diff --git a/src/activities/clock/RingClockFace.h b/src/activities/clock/RingClockFace.h
new file mode 100644
index 000000000..143b2e94f
--- /dev/null
+++ b/src/activities/clock/RingClockFace.h
@@ -0,0 +1,197 @@
+#pragma once
+
+#include <stdint.h>
+
+// Pure integer rasterizer for the segmented ring clock face.
+// Port of ~/src/simple-ring-clock-inverted/main.py (M5Stack CoreInk, 200x200)
+// generalized to an arbitrary face radius. White-on-black ("inverted") design:
+// 12 concentric rings, four quadrants:
+//   top-right    (0-90)    hours   - summed rings (greedy largest-first)
+//   top-left     (90-180)  minutes - single ring, ring N = N*5 minutes
+//   bottom-left  (180-270) month   - summed rings
+//   bottom-right (270-360) day     - summed rings
+// Active arcs are zebra-striped into ring-value segments, white first.
+//
+// Header is freestanding (no Arduino/FreeRTOS includes) so the exact same
+// code can be compiled and rendered on the host for verification.
+
+namespace ringclock {
+
+constexpr int NUM_RINGS = 12;
+
+struct Geometry {
+  int innerR[NUM_RINGS];
+  int outerR[NUM_RINGS];
+};
+
+// Port of get_ring_radii(). Original proportions on a 100-unit face radius:
+// rings start at r=10 and occupy 88 units, 1-unit gaps between rings.
+inline Geometry computeGeometry(int faceRadius) {
+  Geometry g;
+  const int startR = faceRadius * 10 / 100;
+  int gap = faceRadius / 100;
+  if (gap < 1) gap = 1;
+  const int totalSpace = faceRadius * 88 / 100;
+  const int ringWidth = (totalSpace - NUM_RINGS * gap) / NUM_RINGS;
+  int inner = startR;
+  for (int i = 0; i < NUM_RINGS; i++) {
+    g.innerR[i] = inner;
+    g.outerR[i] = inner + ringWidth;
+    inner = g.outerR[i] + gap;
+  }
+  return g;
+}
+
+// Integer angle approximation, 0..360 (can return exactly 360 near the +x
+// axis from below, which no quadrant claims - same as the original).
+// 0 = +x axis, counter-clockwise; dy positive = up (dy = centerY - screenY).
+// Exact port of the octant classification in main.py fill_arc_segment();
+// C++ '/' truncation equals Python '//' floor here because every division
+// has non-negative operands.
+inline int angleOf(int dx, int dy) {
+  if (dx >= 0 && dy >= 0) {
+    int angle = dx > dy ? 0 : 90;
+    if (dx > 0 && dy > 0) angle = dx >= dy ? (45 * dy) / dx : 90 - (45 * dx) / dy;
+    return angle;
+  }
+  if (dx < 0 && dy >= 0) {
+    int angle = dy > -dx ? 90 : 180;
+    if (dy > 0) angle = dy >= -dx ? 90 + (45 * -dx) / dy : 180 - (45 * dy) / -dx;
+    return angle;
+  }
+  if (dx < 0 && dy < 0) {
+    int angle = -dx > -dy ? 180 : 270;
+    angle = -dx >= -dy ? 180 + (45 * -dy) / -dx : 270 - (45 * -dx) / -dy;
+    return angle;
+  }
+  // dx >= 0, dy < 0
+  int angle = -dy > dx ? 270 : 360;
+  angle = -dy >= dx ? 270 + (45 * dx) / -dy : 360 - (45 * -dy) / dx;
+  return angle;
+}
+
+// Port of find_sum_combination(): greedy largest-first ring sum.
+// Returns a bitmask of active ring indices (bit i = ring value i+1), or 0
+// when target is out of the representable range.
+inline uint16_t sumCombinationMask(int target) {
+  if (target <= 0) return 0;
+  uint16_t mask = 0;
+  int remaining = target;
+  for (int i = NUM_RINGS - 1; i >= 0; i--) {
+    const int val = i + 1;
+    if (val <= remaining) {
+      mask |= static_cast<uint16_t>(1u << i);
+      remaining -= val;
+      if (remaining == 0) break;
+    }
+  }
+  return remaining == 0 ? mask : 0;
+}
+
+struct FaceState {
+  uint16_t hourMask;   // quadrant 0 (top-right), summed rings
+  int minuteRing;      // quadrant 1 (top-left), single ring index, -1 = none
+  uint16_t monthMask;  // quadrant 2 (bottom-left), summed rings
+  uint16_t dayMask;    // quadrant 3 (bottom-right), summed rings
+};
+
+// hour12 in 1..12, minute in 0..59, month in 1..12, day in 1..31.
+inline FaceState computeState(int hour12, int minute, int month, int day) {
+  return {sumCombinationMask(hour12), minute / 5 - 1, sumCombinationMask(month), sumCombinationMask(day)};
+}
+
+// Classify one pixel relative to the face center; dy positive = up.
+// Returns true when the pixel is WHITE (everything else stays background black).
+inline bool pixelIsWhite(int dx, int dy, const Geometry& g, const FaceState& st) {
+  const int d2 = dx * dx + dy * dy;
+  const int minR = g.innerR[0];
+  const int maxR = g.outerR[NUM_RINGS - 1];
+  if (d2 > maxR * maxR + maxR) return false;  // outside the face
+
+  // Quadrant divider lines: 1px along both axes, spanning minR..maxR
+  // (crosses ring gaps, like the drawLine calls in main.py).
+  if ((dx == 0 || dy == 0) && d2 >= minR * minR) return true;
+
+  for (int i = 0; i < NUM_RINGS; i++) {
+    const int ri = g.innerR[i];
+    const int ro = g.outerR[i];
+    // Boundary bands |d - r| <= ~0.5  <=>  r^2 - r + 1 <= d^2 <= r^2 + r.
+    // Replaces main.py's midpoint-ellipse outlines (visually equivalent,
+    // guaranteed gap-free). Ring 0's inner band is the inner-circle outline.
+    if (d2 < ri * ri - ri + 1) return false;  // gap below ring i (or inner disc): black
+    if (d2 <= ri * ri + ri) return true;      // inner boundary outline
+    if (d2 > ro * ro + ro) continue;          // beyond ring i, try next
+    if (d2 >= ro * ro - ro + 1) return true;  // outer boundary outline
+
+    // Interior of ring i: segmented arc fill.
+    const int angle = angleOf(dx, dy);
+    const int quad = angle / 90;
+    if (quad > 3) return false;  // angle == 360: unclaimed, stays black (as in main.py)
+
+    bool active = false;
+    switch (quad) {
+      case 0: active = (st.hourMask >> i) & 1; break;
+      case 1: active = i == st.minuteRing; break;
+      case 2: active = (st.monthMask >> i) & 1; break;
+      case 3: active = (st.dayMask >> i) & 1; break;
+    }
+    if (!active) return false;  // inactive arc: black
+
+    // fill_segmented_arc(): ring value = segment count, zebra white-first,
+    // integer-division remainder wedge stays background black.
+    const int segs = i + 1;
+    const int segAngle = 90 / segs;
+    const int rel = angle - quad * 90;
+    if (rel >= segs * segAngle) return false;
+    return (rel / segAngle) % 2 == 0;
+  }
+  return false;
+}
+
+// Minimal date/time carrier for RTC (UTC) -> local conversion.
+struct DateTime {
+  int year;    // e.g. 2026
+  int month;   // 1..12
+  int day;     // 1..31
+  int hour;    // 0..23
+  int minute;  // 0..59
+};
+
+inline bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
+
+inline int daysInMonth(int year, int month) {
+  static constexpr uint8_t DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
+  return (month == 2 && isLeapYear(year)) ? 29 : DAYS[month - 1];
+}
+
+// Apply the CrossPoint clock offset (biased quarter-hours: 48 = UTC+0,
+// 0 = UTC-12:00, 104 = UTC+14:00) to a UTC date-time, rolling the date
+// across midnight. Mirrors the clamp in HalClock::formatTime().
+inline void applyUtcOffsetQ(DateTime& dt, uint8_t offsetQBiased) {
+  if (offsetQBiased > 104) offsetQBiased = 104;
+  int total = dt.hour * 60 + dt.minute + (static_cast<int>(offsetQBiased) - 48) * 15;
+  while (total < 0) {
+    total += 1440;
+    if (--dt.day < 1) {
+      if (--dt.month < 1) {
+        dt.month = 12;
+        dt.year--;
+      }
+      dt.day = daysInMonth(dt.year, dt.month);
+    }
+  }
+  while (total >= 1440) {
+    total -= 1440;
+    if (++dt.day > daysInMonth(dt.year, dt.month)) {
+      dt.day = 1;
+      if (++dt.month > 12) {
+        dt.month = 1;
+        dt.year++;
+      }
+    }
+  }
+  dt.hour = total / 60;
+  dt.minute = total % 60;
+}
+
+}  // namespace ringclock
diff --git a/src/activities/home/HomeActivity.cpp b/src/activities/home/HomeActivity.cpp
index 287432643..b3c566bc9 100644
--- a/src/activities/home/HomeActivity.cpp
+++ b/src/activities/home/HomeActivity.cpp
@@ -15,8 +15,11 @@
 #include "CrossPointSettings.h"
 #include "CrossPointState.h"
 #include "MappedInputManager.h"
+#include <HalClock.h>
+
 #include "OpdsServerStore.h"
 #include "RecentBooksStore.h"
+#include "activities/clock/RingClockActivity.h"
 #include "components/UITheme.h"
 #include "fontIds.h"
 
@@ -28,6 +31,9 @@ int HomeActivity::getMenuItemCount() const {
   if (hasOpdsServers) {
     count++;
   }
+  if (hasClock) {
+    count++;
+  }
   return count;
 }
 
@@ -112,12 +118,14 @@ void HomeActivity::onEnter() {
   Activity::onEnter();
 
   hasOpdsServers = OPDS_STORE.hasServers();
+  hasClock = halClock.isAvailable();
 
   const auto& metrics = UITheme::getInstance().getMetrics();
   loadRecentBooks(metrics.homeRecentBooksCount);
 
   const auto base = static_cast<int>(recentBooks.size());
-  selectorIndex = initialMenuItem == HomeMenuItem::NONE ? 0 : base + menuItemToIndex(initialMenuItem, hasOpdsServers);
+  selectorIndex =
+      initialMenuItem == HomeMenuItem::NONE ? 0 : base + menuItemToIndex(initialMenuItem, hasOpdsServers, hasClock);
 
   // Trigger first update
   requestUpdate();
@@ -184,7 +192,7 @@ void HomeActivity::loop() {
       onSelectBook(recentBooks[selectorIndex].path);
     } else {
       const int menuIndex = selectorIndex - static_cast<int>(recentBooks.size());
-      switch (indexToMenuItem(menuIndex, hasOpdsServers)) {
+      switch (indexToMenuItem(menuIndex, hasOpdsServers, hasClock)) {
         case HomeMenuItem::FILE_BROWSER:
           onFileBrowserOpen();
           break;
@@ -197,6 +205,9 @@ void HomeActivity::loop() {
         case HomeMenuItem::FILE_TRANSFER:
           onFileTransferOpen();
           break;
+        case HomeMenuItem::RING_CLOCK:
+          onRingClockOpen();
+          break;
         case HomeMenuItem::SETTINGS_MENU:
           onSettingsOpen();
           break;
@@ -235,6 +246,12 @@ void HomeActivity::render(RenderLock&&) {
                                         tr(STR_SETTINGS_TITLE)};
   std::vector<UIIcon> menuIcons = {Folder, Recent, Transfer, Settings};
 
+  if (hasClock) {
+    // Ring Clock sits between File Transfer and Settings (X3 with RTC only)
+    menuItems.insert(menuItems.end() - 1, tr(STR_RING_CLOCK));
+    menuIcons.insert(menuIcons.end() - 1, Clock);
+  }
+
   if (hasOpdsServers) {
     menuItems.insert(menuItems.begin() + 2, tr(STR_OPDS_BROWSER));
     menuIcons.insert(menuIcons.begin() + 2, Library);
@@ -281,3 +298,10 @@ void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }
 void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }
 
 void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
+
+void HomeActivity::onRingClockOpen() {
+  // Replace (like the sibling menu entries) so HomeActivity and its cover
+  // buffer are freed during a potentially very long clock session; Back from
+  // the clock lands on goHome() via popActivity's empty-stack fallback.
+  activityManager.replaceActivity(std::make_unique<RingClockActivity>(renderer, mappedInput));
+}
diff --git a/src/activities/home/HomeActivity.h b/src/activities/home/HomeActivity.h
index 6d170cddc..8dcf021f0 100644
--- a/src/activities/home/HomeActivity.h
+++ b/src/activities/home/HomeActivity.h
@@ -16,6 +16,7 @@ class HomeActivity final : public Activity {
   bool recentsLoaded = false;
   bool firstRenderDone = false;
   bool hasOpdsServers = false;
+  bool hasClock = false;
   bool coverRendered = false;      // Track if cover has been rendered once
   bool coverBufferStored = false;  // Track if cover buffer is stored
   uint8_t* coverBuffer = nullptr;  // HomeActivity's own buffer for cover image
@@ -31,7 +32,7 @@ class HomeActivity final : public Activity {
   const HomeMenuItem initialMenuItem;
 
   // Convert HomeMenuItem to menu index (used in onEnter)
-  static int menuItemToIndex(HomeMenuItem item, bool hasOpdsUrl) {
+  static int menuItemToIndex(HomeMenuItem item, bool hasOpdsUrl, bool hasClock) {
     int i = 0;
     if (item == HomeMenuItem::FILE_BROWSER) return i;
     ++i;
@@ -41,17 +42,20 @@ class HomeActivity final : public Activity {
     if (hasOpdsUrl) ++i;
     if (item == HomeMenuItem::FILE_TRANSFER) return i;
     ++i;
+    if (item == HomeMenuItem::RING_CLOCK) return hasClock ? i : 0;
+    if (hasClock) ++i;
     if (item == HomeMenuItem::SETTINGS_MENU) return i;
     return 0;
   }
 
   // Convert menu index to HomeMenuItem (used in loop)
-  static HomeMenuItem indexToMenuItem(int idx, bool hasOpdsUrl) {
+  static HomeMenuItem indexToMenuItem(int idx, bool hasOpdsUrl, bool hasClock) {
     int i = 0;
     if (idx == i++) return HomeMenuItem::FILE_BROWSER;
     if (idx == i++) return HomeMenuItem::RECENTS;
     if (hasOpdsUrl && idx == i++) return HomeMenuItem::OPDS_BROWSER;
     if (idx == i++) return HomeMenuItem::FILE_TRANSFER;
+    if (hasClock && idx == i++) return HomeMenuItem::RING_CLOCK;
     if (idx == i) return HomeMenuItem::SETTINGS_MENU;
     return HomeMenuItem::NONE;
   }
@@ -61,6 +65,7 @@ class HomeActivity final : public Activity {
   void onSettingsOpen();
   void onFileTransferOpen();
   void onOpdsBrowserOpen();
+  void onRingClockOpen();
 
   int getMenuItemCount() const;
   bool storeCoverBuffer();    // Store frame buffer for cover image
diff --git a/src/components/icons/clock.h b/src/components/icons/clock.h
new file mode 100644
index 000000000..a742f77e9
--- /dev/null
+++ b/src/components/icons/clock.h
@@ -0,0 +1,12 @@
+#pragma once
+#include <cstdint>
+
+// size: 32x32
+static const uint8_t ClockIcon[] = {
+    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x07, 0xFF, 0xFF, 0xC0, 0x01, 0xFF, 0xFF, 0x03, 0xE0,
+    0x7F, 0xFE, 0x0F, 0xF8, 0x3F, 0xFC, 0x3F, 0xFE, 0x1F, 0xF8, 0x7F, 0xFF, 0x0F, 0xF8, 0xFF, 0xFF, 0x8F, 0xF1, 0xFF,
+    0xFF, 0xC7, 0xF1, 0xFF, 0xCF, 0xC7, 0xE3, 0xFF, 0x8F, 0xE3, 0xE3, 0xFF, 0x8F, 0xE3, 0xE7, 0xFF, 0x1F, 0xF3, 0xE7,
+    0x00, 0x1F, 0xF3, 0xE7, 0x00, 0x3F, 0xF3, 0xE7, 0xFF, 0xFF, 0xF3, 0xE7, 0xFF, 0xFF, 0xF3, 0xE3, 0xFF, 0xFF, 0xE3,
+    0xE3, 0xFF, 0xFF, 0xE3, 0xF1, 0xFF, 0xFF, 0xC7, 0xF1, 0xFF, 0xFF, 0xC7, 0xF8, 0xFF, 0xFF, 0x8F, 0xF8, 0x7F, 0xFF,
+    0x0F, 0xFC, 0x3F, 0xFE, 0x1F, 0xFE, 0x0F, 0xF8, 0x3F, 0xFF, 0x03, 0xE0, 0x7F, 0xFF, 0xC0, 0x01, 0xFF, 0xFF, 0xF0,
+    0x07, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
diff --git a/src/components/themes/BaseTheme.h b/src/components/themes/BaseTheme.h
index b50bfc6d2..6f7edb307 100644
--- a/src/components/themes/BaseTheme.h
+++ b/src/components/themes/BaseTheme.h
@@ -113,7 +113,22 @@ struct ThemeMetrics {
   int textFieldLineEndOffset;
 };
 
-enum UIIcon { None = 0, Folder, Text, Image, Book, File, Recent, Settings, Transfer, Library, Wifi, Hotspot, Bookmark };
+enum UIIcon {
+  None = 0,
+  Folder,
+  Text,
+  Image,
+  Book,
+  File,
+  Recent,
+  Settings,
+  Transfer,
+  Library,
+  Wifi,
+  Hotspot,
+  Bookmark,
+  Clock
+};
 
 enum class KeyboardKeyType { Normal, Shift, Mode, Space, Del, Ok, Disabled };
 
diff --git a/src/components/themes/lyra/LyraTheme.cpp b/src/components/themes/lyra/LyraTheme.cpp
index c5af0a395..4fa086afd 100644
--- a/src/components/themes/lyra/LyraTheme.cpp
+++ b/src/components/themes/lyra/LyraTheme.cpp
@@ -15,6 +15,7 @@
 #include "components/icons/book.h"
 #include "components/icons/book24.h"
 #include "components/icons/bookmark.h"
+#include "components/icons/clock.h"
 #include "components/icons/cover.h"
 #include "components/icons/file24.h"
 #include "components/icons/folder.h"
@@ -76,6 +77,8 @@ const uint8_t* iconForName(UIIcon icon, int size) {
         return HotspotIcon;
       case UIIcon::Bookmark:
         return BookmarkIcon;
+      case UIIcon::Clock:
+        return ClockIcon;
       default:
         return nullptr;
     }
```
