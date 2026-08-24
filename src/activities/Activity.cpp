#include "Activity.h"

#include <HalGPIO.h>

#include "ActivityManager.h"

namespace {
// FNV-1a, 32-bit. The simulator's sheetid::screenKey() is the same function,
// and tests/sheet_identity_test.cpp over there reads this file as text to prove
// the two have not drifted. It cannot include that header: the simulator is not
// linked on device.
uint32_t screenKeyOf(const std::string& name) {
  uint32_t h = 2166136261u;
  for (const unsigned char c : name) {
    h ^= static_cast<uint32_t>(c);
    h *= 16777619u;
  }
  return h;
}
}  // namespace

void Activity::onEnter() {
  LOG_DBG("ACT", "Entering activity: %s", name.c_str());
  // WHICH SHEET THIS SCREEN IS PRINTED ON, for hosts that generate paper (an
  // inline no-op on device -- lib/hal/HalGPIO.h). Every screen in the firmware
  // passes through here, which is why the call is in the base rather than in
  // the 34 overrides.
  //
  // READERS ARE SKIPPED. They publish a finer identity -- the actual page --
  // from their render, which runs after this; publishing here too would put one
  // present carrying the OUTGOING screen's pixels on a third sheet between the
  // two, on every book open.
  if (!isReaderActivity()) gpio.publishScreenIdentity(screenKeyOf(name));
}

void Activity::onExit() { LOG_DBG("ACT", "Exiting activity: %s", name.c_str()); }

void Activity::requestUpdate(bool immediate) { activityManager.requestUpdate(immediate); }

void Activity::requestUpdateAndWait() { activityManager.requestUpdateAndWait(); }

void Activity::onGoHome(HomeMenuItem item) { activityManager.goHome(item); }

void Activity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void Activity::startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler) {
  this->resultHandler = std::move(resultHandler);
  activityManager.pushActivity(std::move(activity));
}

void Activity::setResult(ActivityResult&& result) { this->result = std::move(result); }

void Activity::finish() { activityManager.popActivity(); }

Activity::ListTouchResult Activity::handleListTouch(int& selectedIndex, const int itemCount, const int listTop,
                                                    const int listHeight, const bool hasSubtitle) {
  int touched = -1;
  if (mappedInput.wasListItemTouchedDown(touched, itemCount, selectedIndex, listTop, listHeight, hasSubtitle)) {
    if (selectedIndex != touched) {
      selectedIndex = touched;
      requestUpdate();
    }
    return ListTouchResult::Consumed;
  }
  if (mappedInput.wasListItemTapped(touched, itemCount, selectedIndex, listTop, listHeight, hasSubtitle)) {
    selectedIndex = touched;
    return ListTouchResult::Activated;
  }
  return ListTouchResult::None;
}
