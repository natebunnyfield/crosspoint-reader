// Host keyboard text entry — the firmware half.
//
// WHAT THIS COVERS AND WHAT IT CANNOT
//
// The channel has two halves. The HOST half (a real key event becoming a
// character, and the keyboard being taken off the button map while a field is
// open) lives in the simulator's HalGPIO and needs real SDL input, so it is
// exercised by running the simulator, not here. The FIRMWARE half is this: a
// drained chunk of typed bytes becoming edits to the field, in the order the
// bytes arrived, with commit and cancel finishing the activity.
//
// The activity under test is the REAL DaisyEntryActivity, driven through the
// real MappedInputManager and the real frame boundary, with the typist scripted
// on the stub HAL (simType). The grid keyboard shares the same drain helper
// (util/TypedTextInput.h) and the same two call sites; it is not linked here
// because it pulls the SDK keyboard component in with it, and it is covered
// end-to-end by the simulator instead (tests/test_text_entry.sh in the
// simulator repo drives it through Settings > Device Owner).
//
// The parent double exists for one reason: an activity's result is only
// observable by whoever launched it.

#include <HalGPIO.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "HostHarness.h"
#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "activities/util/DaisyEntryActivity.h"

namespace {

struct EntryObservations {
  int results = 0;
  bool cancelled = false;
  std::string text;
};
EntryObservations g_obs;

// Launches one text-entry activity and records what came back. Outside the
// activity (static storage) for the same reason ActivityInputTest's parent
// observations are: the harness deletes the parent on some paths, and the
// trailing EXPECTs must not read freed memory.
class TextEntryParent final : public Activity {
 public:
  TextEntryParent(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initial, size_t maxLength)
      : Activity("TestTextEntryParent", renderer, mappedInput),
        initial(std::move(initial)),
        maxLength(maxLength) {}

  void loop() override {
    if (launched) return;
    launched = true;
    startActivityForResult(
        std::make_unique<DaisyEntryActivity>(renderer, mappedInput, "Title", initial, maxLength, InputType::Text),
        [](const ActivityResult& result) {
          g_obs.results++;
          g_obs.cancelled = result.isCancelled;
          if (const auto* kb = std::get_if<KeyboardResult>(&result.data)) g_obs.text = kb->text;
        });
  }

  void render(RenderLock&&) override {}

 private:
  std::string initial;
  size_t maxLength;
  bool launched = false;
};

class TypedTextEntry : public ::testing::Test {
 protected:
  void SetUp() override {
    host::reset();
    g_obs = {};
  }
  void TearDown() override { host::reset(); }

  // Puts a text-entry activity on screen with `initial` already in the field.
  void openField(const std::string& initial = "", size_t maxLength = 0) {
    host::setRootActivity(std::make_unique<TextEntryParent>(host::renderer(), host::input(), initial, maxLength));
    host::frame();  // parent's loop() launches the child
    ASSERT_EQ(host::currentActivityName(), "DaisyEntry") << "text entry never opened";
  }

  // One host keystroke burst, delivered on the next frame boundary.
  void type(const std::string& utf8) {
    host::buttons().simType(utf8);
    host::frame();
  }
};

TEST_F(TypedTextEntry, AnnouncesTheOpenFieldToTheHostAndStandsDownAfterwards) {
  EXPECT_FALSE(host::buttons().simTextEntryActive()) << "flag set before any field was open";
  openField();
  EXPECT_TRUE(host::buttons().simTextEntryActive()) << "the host was never told a field is open";

  type(std::string(1, HalGPIO::TYPED_COMMIT));
  EXPECT_FALSE(host::buttons().simTextEntryActive())
      << "the flag outlived the field -- the host would keep the keyboard off the button map";
}

TEST_F(TypedTextEntry, TypedRunLandsInTheFieldAndCommitReturnsIt) {
  openField();
  type("Ada Lovelace");
  EXPECT_EQ(g_obs.results, 0) << "typing alone finished the activity";

  type(std::string(1, HalGPIO::TYPED_COMMIT));
  ASSERT_EQ(g_obs.results, 1) << "the commit key did not finish the entry";
  EXPECT_FALSE(g_obs.cancelled);
  EXPECT_EQ(g_obs.text, "Ada Lovelace");
}

TEST_F(TypedTextEntry, TypingAppendsToWhateverSeededTheField) {
  openField("Ada ");
  type("Lovelace\n");
  ASSERT_EQ(g_obs.results, 1);
  EXPECT_EQ(g_obs.text, "Ada Lovelace");
}

// The ordering property: one chunk can hold several edits, and they have to be
// applied left to right. Applying all the text then all the backspaces (the
// obvious wrong implementation) yields "Ad" here instead of "Ada".
TEST_F(TypedTextEntry, BackspacesApplyInThePositionTheyArrived) {
  openField();
  type("AdaXX\b\b\n");
  ASSERT_EQ(g_obs.results, 1);
  EXPECT_EQ(g_obs.text, "Ada");
}

TEST_F(TypedTextEntry, BackspaceOnAnEmptyFieldIsHarmless) {
  openField();
  type("\b\b\b");
  type("Ada\n");
  ASSERT_EQ(g_obs.results, 1);
  EXPECT_EQ(g_obs.text, "Ada");
}

TEST_F(TypedTextEntry, CancelAbandonsTheEntryAndKeepsWhatWasTypedOutOfTheResult) {
  openField("Ada");
  type("Byron\x1b");
  ASSERT_EQ(g_obs.results, 1) << "the cancel key did not finish the entry";
  EXPECT_TRUE(g_obs.cancelled);
  EXPECT_TRUE(g_obs.text.empty()) << "a cancelled entry still returned text";
}

// Commit and cancel end the walk: bytes behind them belong to nothing, and an
// activity that has called finish() must not keep editing.
TEST_F(TypedTextEntry, NothingAfterTheCommitByteIsApplied) {
  openField();
  type("Ada\nByron");
  ASSERT_EQ(g_obs.results, 1);
  EXPECT_EQ(g_obs.text, "Ada");
}

// A phone keyboard emits emoji and accented characters as multi-byte UTF-8 in
// one event. The length check has to see the whole run, or the field is cut
// mid-code-point and stores an invalid string.
TEST_F(TypedTextEntry, MultiByteRunIsInsertedWholeOrNotAtAll) {
  openField("", /*maxLength=*/5);
  type("ab");        // 2 bytes of 5
  type("\xc3\xa9");  // 'é', 2 more -> fits
  type("\n");
  ASSERT_EQ(g_obs.results, 1);
  EXPECT_EQ(g_obs.text, "ab\xc3\xa9");

  g_obs = {};
  host::reset();
  openField("", /*maxLength=*/5);
  type("abcd");      // 4 bytes of 5
  type("\xc3\xa9");  // would need 2 -> must be refused whole
  type("\n");
  ASSERT_EQ(g_obs.results, 1);
  EXPECT_EQ(g_obs.text, "abcd") << "a 2-byte character was cut in half at the length limit";
}

// The host is told when a field opens and closes; anything typed outside that
// window is dropped at the HAL rather than queued for whatever opens next.
TEST_F(TypedTextEntry, TextTypedBeforeAFieldOpensIsNotDeliveredToIt) {
  host::buttons().simType("ghost");
  openField();
  type("\n");
  ASSERT_EQ(g_obs.results, 1);
  EXPECT_EQ(g_obs.text, "") << "text typed before the field existed leaked into it";
}

// The wheel still works while a keyboard is attached: the two edit the same
// field, and neither consumes the other's input.
TEST_F(TypedTextEntry, ButtonsStillDriveTheWheelWhileTypingIsAvailable) {
  openField();
  type("Ada");
  host::tap(HalGPIO::BTN_BACK);  // the wheel's cancel
  host::frames(2);
  ASSERT_EQ(g_obs.results, 1);
  EXPECT_TRUE(g_obs.cancelled) << "Back no longer cancels the entry once typing is in play";
}

}  // namespace
