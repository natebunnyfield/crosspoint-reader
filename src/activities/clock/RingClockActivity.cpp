#include "RingClockActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "RingClockFace.h"
#include "components/UITheme.h"
#include "fontIds.h"

bool RingClockActivity::readDisplayState(int& hour12, int& minuteRing, int& month, int& day) const {
  uint8_t h = 0, mi = 0, mo = 0, d = 0;
  uint16_t y = 0;
  if (!halClock.getTime(h, mi)) return false;
  if (!halClock.getDate(y, mo, d)) return false;

  ringclock::DateTime dt{y, mo, d, h, mi};
  ringclock::applyUtcOffsetQ(dt, SETTINGS.clockUtcOffsetQ);

  hour12 = dt.hour % 12;
  if (hour12 == 0) hour12 = 12;
  minuteRing = dt.minute / 5 - 1;
  month = dt.month;
  day = dt.day;
  return true;
}

void RingClockActivity::onEnter() {
  Activity::onEnter();
  rtcAvailable = readDisplayState(shownHour12, shownMinuteRing, shownMonth, shownDay);
  if (!rtcAvailable) LOG_INF("RCLK", "RTC unavailable, showing fallback message");
  requestUpdate();
}

void RingClockActivity::onExit() {
  // The clock leaves a mostly-black frame behind; flush a clean white FULL
  // refresh so the next activity doesn't render on top of heavy ghosting.
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  Activity::onExit();
}

void RingClockActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  int hour12, minuteRing, month, day;
  if (!readDisplayState(hour12, minuteRing, month, day)) return;
  if (rtcAvailable && hour12 == shownHour12 && minuteRing == shownMinuteRing && month == shownMonth &&
      day == shownDay) {
    return;
  }

  // A read after a transient onEnter failure un-latches the fallback screen.
  {
    RenderLock lock;
    rtcAvailable = true;
    shownHour12 = hour12;
    shownMinuteRing = minuteRing;
    shownMonth = month;
    shownDay = day;
  }
  requestUpdate();
}

void RingClockActivity::render(RenderLock&&) {
  if (!rtcAvailable) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_RING_CLOCK_NO_RTC));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  renderer.clearScreen(0x00);  // black background, white ink (inverted design)

  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int viewW = w - left - right;
  const int viewH = h - top - bottom;
  const int cx = left + viewW / 2;
  const int cy = top + viewH / 2;
  int faceRadius = (viewW < viewH ? viewW : viewH) / 2 - 4;
  if (faceRadius < ringclock::NUM_RINGS * 2) faceRadius = ringclock::NUM_RINGS * 2;

  const ringclock::Geometry geometry = ringclock::computeGeometry(faceRadius);
  const ringclock::FaceState state{ringclock::sumCombinationMask(shownHour12), shownMinuteRing,
                                   ringclock::sumCombinationMask(shownMonth), ringclock::sumCombinationMask(shownDay)};

  for (int py = cy - faceRadius; py <= cy + faceRadius; py++) {
    const int dy = cy - py;
    for (int px = cx - faceRadius; px <= cx + faceRadius; px++) {
      if (ringclock::pixelIsWhite(px - cx, dy, geometry, state)) {
        renderer.drawPixel(px, py, false);  // white
      }
    }
  }

  // Periodic full refresh clears e-ink ghosting from the fast updates.
  const bool full = rendersSinceFullRefresh == 0 || rendersSinceFullRefresh >= 6;
  renderer.displayBuffer(full ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  rendersSinceFullRefresh = full ? 1 : rendersSinceFullRefresh + 1;
}
