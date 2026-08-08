#include "ClockOffsetActivity.h"
#include <HalClock.h>

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// The stored byte is quarter-hour steps biased by 48 (48 = UTC+0).
constexpr uint8_t BIAS_QUARTER_HOURS = 48;

constexpr uint8_t quarters(const int hours, const int minutes = 0) {
  return static_cast<uint8_t>(BIAS_QUARTER_HOURS + hours * 4 + minutes / 15);
}

struct TimezoneEntry {
  StrId nameId;
  uint8_t offsetQ;  // Same biased quarter-hour encoding as SETTINGS.clockUtcOffsetQ.
};

// Named zones with fixed UTC offsets, west to east. The firmware has no DST
// database, so a zone that observes DST appears once per offset it uses and the
// user flips entries seasonally. Zones whose offsets coincide share a row
// (US Eastern standard == US Central daylight, and so on down the ladder):
// every row's offset stays unique, so restoring the highlight from the stored
// byte is unambiguous, and both seasons of every US zone fit in seven rows.
// Flash-resident: constexpr table of PODs, no constructors, no DRAM cost.
constexpr TimezoneEntry TIMEZONES[] = {
    {StrId::STR_TZ_US_HAWAII, quarters(-10)},          // HST, no DST
    {StrId::STR_TZ_US_ALASKA_STD, quarters(-9)},       // AKST
    {StrId::STR_TZ_US_PACIFIC_STD, quarters(-8)},      // PST / AKDT
    {StrId::STR_TZ_US_MOUNTAIN_STD, quarters(-7)},     // MST (Arizona all year) / PDT
    {StrId::STR_TZ_US_CENTRAL_STD, quarters(-6)},      // CST / MDT
    {StrId::STR_TZ_US_CENTRAL_DST, quarters(-5)},      // CDT / EST -- the fresh-device default
    {StrId::STR_TZ_US_EASTERN_DST, quarters(-4)},      // EDT
    {StrId::STR_TZ_UTC_GMT, quarters(0)},              // UTC / UK winter
    {StrId::STR_TZ_UK_DST_CET, quarters(+1)},          // BST / CET
    {StrId::STR_TZ_CENTRAL_EUROPE_DST, quarters(+2)},  // CEST
    {StrId::STR_TZ_INDIA, quarters(+5, 30)},           // IST, no DST
    {StrId::STR_TZ_JAPAN, quarters(+9)},               // JST, no DST
    {StrId::STR_TZ_AUS_EAST_STD, quarters(+10)},       // AEST
    {StrId::STR_TZ_AUS_EAST_DST, quarters(+11)},       // AEDT
};
constexpr int TZ_COUNT = static_cast<int>(sizeof(TIMEZONES) / sizeof(TIMEZONES[0]));

// Index of the entry matching a stored offset, or -1. Offsets are unique by
// construction, so the first match is the only match.
int indexForOffset(const uint8_t offsetQ) {
  for (int i = 0; i < TZ_COUNT; i++) {
    if (TIMEZONES[i].offsetQ == offsetQ) return i;
  }
  return -1;
}
}  // namespace

// Verbatim from upstream StatusBarSettingsActivity.cpp, which this fork does not
// have. Rehomed rather than duplicated so the settings row and the picker cannot
// disagree about how an offset is spelled.
std::string formatUtcOffset(uint8_t biasedQ) {
  // biasedQ is in quarter-hour steps, biased by 48 (so 48 = UTC+0).
  if (biasedQ > 104) biasedQ = 48;
  int totalMinutes = (static_cast<int>(biasedQ) - 48) * 15;
  bool neg = totalMinutes < 0;
  int absMinutes = neg ? -totalMinutes : totalMinutes;
  int hours = absMinutes / 60;
  int mins = absMinutes % 60;
  char buf[16];
  snprintf(buf, sizeof(buf), "UTC%c%d:%02d", neg ? '-' : '+', hours, mins);
  return buf;
}

void ClockOffsetActivity::onEnter() {
  Activity::onEnter();

  const uint8_t stored = SETTINGS.clockUtcOffsetQ;
  const int match = indexForOffset(stored);
  // An offset no named zone uses (older firmware or the web API can write any
  // 15-minute step, e.g. Nepal +5:45) gets a trailing "Custom offset" row so
  // the stored value stays visible and selectable -- never silently discarded.
  hasCustomRow = (match < 0);
  customOffsetQ = stored;
  itemCount = TZ_COUNT + (hasCustomRow ? 1 : 0);
  selectedIndex = (match >= 0) ? match : TZ_COUNT;

  requestUpdate();
}

void ClockOffsetActivity::handleSelection() {
  if (selectedIndex < TZ_COUNT) {
    const uint8_t offsetQ = TIMEZONES[selectedIndex].offsetQ;
    if (offsetQ != SETTINGS.clockUtcOffsetQ) {
      SETTINGS.clockUtcOffsetQ = offsetQ;
      SETTINGS.saveToFile();
    }
  }
  // The custom row re-selects the already-stored offset: nothing to write.
  finish();
}

void ClockOffsetActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  switch (handleListTouch(selectedIndex, itemCount, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      handleSelection();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, itemCount, pageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, itemCount, pageItems);
    requestUpdate();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, pageItems] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, itemCount, pageItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, pageItems] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, itemCount, pageItems);
    requestUpdate();
  });
}

void ClockOffsetActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CLOCK_UTC_OFFSET));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  // Live preview of the wall-clock time the highlighted row would produce, so
  // the offset can be checked against a watch instead of worked out. Upstream
  // has this on its own offset screen; this fork's rewrite as a timezone list
  // dropped it, which also left SETTINGS.clockFormat with no reader at all --
  // a 24H/12H row that changed nothing (B-019). Reserve a line for it only when
  // there is a clock to read.
  const bool showPreview = halClock.isAvailable();
  const int previewHeight = showPreview ? metrics.listRowHeight + metrics.verticalSpacing : 0;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - previewHeight;
  const uint8_t stored = SETTINGS.clockUtcOffsetQ;
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, itemCount, selectedIndex,
      [this](const int index) -> std::string {
        if (index < TZ_COUNT) return I18N.get(TIMEZONES[index].nameId);
        // Custom row: show the raw offset in the title, because it appears in
        // no named row and is the one thing this row must communicate.
        char buf[48];
        snprintf(buf, sizeof(buf), "%s (%s)", tr(STR_TZ_CUSTOM), formatUtcOffset(customOffsetQ).c_str());
        return buf;
      },
      nullptr, nullptr,
      [this, stored](const int index) -> std::string {
        if (index >= TZ_COUNT) return tr(STR_SELECTED);  // Custom row only exists when it is current.
        if (TIMEZONES[index].offsetQ == stored) return tr(STR_SELECTED);
        return formatUtcOffset(TIMEZONES[index].offsetQ);
      },
      true);

  if (showPreview) {
    const uint8_t previewQ = selectedIndex < TZ_COUNT ? TIMEZONES[selectedIndex].offsetQ : customOffsetQ;
    char timeBuf[16];
    if (halClock.formatTime(timeBuf, sizeof(timeBuf), previewQ, SETTINGS.clockFormat == 1)) {
      char preview[64];
      snprintf(preview, sizeof(preview), "%s %s", tr(STR_CURRENT_TIME), timeBuf);
      renderer.drawCenteredText(UI_10_FONT_ID, contentTop + contentHeight + metrics.verticalSpacing, preview);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
