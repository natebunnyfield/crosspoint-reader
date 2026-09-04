// KeyboardEntryActivity cursor mode: the caret auto-repeats while Left or
// Right is held (docs/ux-navigation-audit-2026-09-02.md, F14).
//
// Before 2026-09-04 the caret stepped once per RELEASE, so crossing a
// sixty-character field was sixty presses while the same button auto-repeated
// across the key grid. The pin is the update count: a press is one step, and a
// press still held past FIRST_REPEAT_MS is another. The pre-fix tree records
// zero updates on the press frame (it was waiting for the release), which is
// what makes the first EXPECT discriminate.
//
// A PASSWORD field keeps Right release-driven, because a held Right there is
// the reveal-position gesture; the last case pins that it did not grow a
// repeat by accident.
#include <HalGPIO.h>
#include <gtest/gtest.h>
#include <unistd.h>  // usleep

#include <memory>
#include <string>

#include "HostHarness.h"
#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "activities/util/KeyboardEntryActivity.h"

namespace {

class KeyboardParent final : public Activity {
 public:
  KeyboardParent(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initial, InputType type)
      : Activity("TestKeyboardParent", renderer, mappedInput), initial(std::move(initial)), type(type) {}

  void loop() override {
    if (launched) return;
    launched = true;
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Title", initial, 0, type),
                           [](const ActivityResult&) {});
  }

  void render(RenderLock&&) override {}

 private:
  std::string initial;
  InputType type;
  bool launched = false;
};

class KeyboardCursorRepeat : public ::testing::Test {
 protected:
  void SetUp() override { host::reset(); }
  void TearDown() override { host::reset(); }

  void openField(const std::string& initial, InputType type = InputType::Text) {
    host::setRootActivity(std::make_unique<KeyboardParent>(host::renderer(), host::input(), initial, type));
    host::frame();
    ASSERT_EQ(host::currentActivityName(), "KeyboardEntry") << "keyboard never opened";
    // Cursor mode is a held Up (LONG_PRESS_MS = 500). Its own hold fires
    // requestUpdate once; the counters are reset after so the caret's steps
    // are the only updates counted below.
    host::pressFrame(HalGPIO::BTN_UP);
    usleep(550 * 1000);
    host::frame();
    host::releaseFrame(HalGPIO::BTN_UP);
    host::resetCounters();
  }
};

TEST_F(KeyboardCursorRepeat, LeftStepsOnPressAndRepeatsWhileHeld) {
  openField("abcdef");
  // The caret opens at the END of the field, so Left has room to step.
  host::pressFrame(HalGPIO::BTN_LEFT);
  EXPECT_EQ(host::counters().updates, 1) << "Left did not step the caret on the press (release-driven, F14)";

  host::frame();
  EXPECT_EQ(host::counters().updates, 1) << "repeated before FIRST_REPEAT_MS";

  usleep(650 * 1000);
  host::frame();
  EXPECT_EQ(host::counters().updates, 2) << "no repeat after FIRST_REPEAT_MS";

  usleep(320 * 1000);
  host::frame();
  EXPECT_EQ(host::counters().updates, 3) << "no second repeat after NEXT_REPEAT_MS";

  host::releaseFrame(HalGPIO::BTN_LEFT);
  EXPECT_EQ(host::counters().updates, 3) << "the release must not add a step of its own";

  usleep(650 * 1000);
  host::frame();
  EXPECT_EQ(host::counters().updates, 3) << "a released button kept repeating";
}

TEST_F(KeyboardCursorRepeat, LeftStopsAtTheStartOfTheField) {
  openField("ab");
  host::pressFrame(HalGPIO::BTN_LEFT);  // -> 1
  usleep(650 * 1000);
  host::frame();  // -> 0
  EXPECT_EQ(host::counters().updates, 2);
  usleep(320 * 1000);
  host::frame();  // nothing left to step
  EXPECT_EQ(host::counters().updates, 2) << "a caret at 0 must not keep reporting steps";
}

TEST_F(KeyboardCursorRepeat, RightRepeatsInATextField) {
  openField("abcdef");
  host::pressFrame(HalGPIO::BTN_LEFT);
  usleep(650 * 1000);
  host::frame();
  host::releaseFrame(HalGPIO::BTN_LEFT);  // caret at 4
  host::resetCounters();

  host::pressFrame(HalGPIO::BTN_RIGHT);
  EXPECT_EQ(host::counters().updates, 1) << "Right did not step on the press";
  usleep(650 * 1000);
  host::frame();
  EXPECT_EQ(host::counters().updates, 2) << "Right did not repeat";
  host::releaseFrame(HalGPIO::BTN_RIGHT);
  EXPECT_EQ(host::counters().updates, 2);
}

TEST_F(KeyboardCursorRepeat, RightStaysReleaseDrivenInAPasswordField) {
  openField("abcdef", InputType::Password);
  host::pressFrame(HalGPIO::BTN_LEFT);
  host::releaseFrame(HalGPIO::BTN_LEFT);  // caret at 5
  host::resetCounters();

  host::pressFrame(HalGPIO::BTN_RIGHT);
  EXPECT_EQ(host::counters().updates, 0) << "a password field's Right must wait for the release (reveal-position hold)";
  host::releaseFrame(HalGPIO::BTN_RIGHT);
  EXPECT_EQ(host::counters().updates, 1) << "the short Right release stopped stepping the caret";
}

}  // namespace
