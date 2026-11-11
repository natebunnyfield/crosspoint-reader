#pragma once

#include <GfxRenderer.h>

#include <string>

#include "activities/UiListActivity.h"

// Render a biased quarter-hour offset as "UTC+5:45" / "UTC-12:00".
// Upstream keeps this next to the menu row that displays it, in
// StatusBarSettingsActivity.cpp; this fork has no status bar screen, so it lives
// with the picker that owns the encoding and the two cannot drift apart.
std::string formatUtcOffset(uint8_t biasedQ);

// Time zone picker for the clock. Presents a short flash-resident list of named
// zones, each bound to a fixed UTC offset in the same biased quarter-hour
// encoding SETTINGS.clockUtcOffsetQ has always used (48 = UTC+0). Confirm
// stores the highlighted zone's offset; Back leaves the stored value untouched.
//
// A stored offset that matches no list entry (written by older firmware or the
// web settings API, e.g. Nepal +5:45) is surfaced as an extra "Custom offset"
// row rather than discarded — arbitrary offsets remain settable via the web
// settings control, which edits the raw byte.
// Upstream's ClockOffsetActivity (9e3cea2e5) is a slider; this fork's is the
// named time zone list, so it ports as a plain UiListActivity.
class ClockOffsetActivity final : public UiListActivity {
 public:
  explicit ClockOffsetActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("ClockOffset", renderer, mappedInput) {}

  void onEnter() override;

 private:
  int itemCount = 0;
  bool hasCustomRow = false;
  uint8_t customOffsetQ = 48;

  int listCount() const override { return itemCount; }
  void describeRow(int index, ListRow& row) const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override { return tr(STR_CLOCK_UTC_OFFSET); }
  // The live clock preview under the list, then the hints.
  void drawFooter() override;
  // Height of the preview line under the list (0 without a clock).
  int previewHeight() const;
};
