// Activity / input-edge suite.
//
// THE BUG THIS EXISTS FOR
//
// A single Back tap inside Text Settings threw the user out of their book to
// Home. FontSelectionActivity finished on the Back PRESS, so it was already
// gone when the RELEASE edge arrived one frame later; EpubReaderActivity — by
// then current again — acts on the Back release (short press -> onGoHome) and
// took it as its own. Measured on device: Back at t=13000 -> FontSelect exited
// t+13ms (the press), EpubReader exited t+80ms (the release).
// See FontSelectionActivity.cpp:116-137.
//
// One physical press therefore produced TWO activity transitions. That is the
// invariant under test here, and it is a property of the whole
// launcher/child contract, not of one screen:
//
//   A child activity launched from a parent that acts on the Back RELEASE must
//   consume the RELEASE itself. Consuming the press leaves the release
//   unconsumed for the parent.
//
// WHY IT IS MODELLED RATHER THAN DRIVEN END-TO-END
//
// EpubReaderActivity cannot be linked host-side: nm -u on its simulator object
// lists 124 project-level undefined symbols (the EPUB engine, the section
// cache, KOReader sync, the whole reader UI). So the parent is a double that
// implements only the contract that matters — "acts on the Back release" — and
// the CHILD is the real shipped activity, unmodified. The hazard is structural,
// so a structural model catches it: what makes the test valid is that the
// button edges are delivered on the real frame boundary (HalGPIO edges are
// computed once per gpio.update(), stubs/HalGPIO.h) through the real
// MappedInputManager mapping.

#include <HalGPIO.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdio>
#include <memory>
#include <ostream>
#include <string>

#include "CrossPointSettings.h"
#include "HostHarness.h"
#include "MappedInputManager.h"
#include "ReaderFontSizes.h"
#include "activities/Activity.h"
#include "activities/RenderLock.h"
#include "activities/settings/FontSelectionActivity.h"
#include "activities/util/IntervalSelectionActivity.h"

namespace {

// The launcher's half of the contract. EpubReaderActivity's Back handling is
// release-edge based (short press -> onGoHome), and that single fact is all a
// child needs to be tested against — so this double carries exactly that and
// nothing else.
// Observations live OUTSIDE the activity, at static storage duration.
//
// They used to be members of ReaderParentDouble, read through a raw pointer after
// the taps. That is a use-after-free on exactly the path the test exists to catch:
// when the child consumes the wrong edge, the parent's own loop() calls
// onGoHome(), the harness replaces the activity and DELETES the parent, and the
// trailing EXPECTs then read freed memory. gtest's EXPECT_* are non-fatal, so
// nothing stopped before them — the failure output was partly garbage, and under
// ASan the binary aborts instead of printing the assertions.
struct ParentObservations {
  int backReleasesSeen = 0;
  int childResults = 0;
};
ParentObservations g_parentObs;

class ReaderParentDouble final : public Activity {
 public:
  ReaderParentDouble(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TestReaderParent", renderer, mappedInput) {}

  void loop() override {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      g_parentObs.backReleasesSeen++;
      onGoHome();  // what the real reader does: leaves the book
    }
  }
};

// ---------------------------------------------------------------------------
// The activities under test: every one that (a) links host-side and (b) is
// opened by a launcher that acts on the Back release. FontSelectionActivity is
// the one that regressed; the other three are the ones its fix was aligned
// with, and they are here so the next activity someone adds has a net to fall
// into.
// ---------------------------------------------------------------------------

using ActivityFactory = std::unique_ptr<Activity> (*)();

// Why every FontSelection case below is skipped rather than run.
//
// This suite's UITheme double leaves `currentTheme` null on purpose — linking
// the real one needs BaseTheme.cpp, which reaches HalClock/HalPowerManager/
// BoardConfig and from there ESP-IDF's <driver/gpio.h>. HostHarness.cpp states
// the resulting invariant: GUI (== getTheme()) is a null dereference, so only
// render() may touch it, and nothing here calls render().
//
// cb98d4fa ("prose specimen, 3-row list, overlaid badge") broke that invariant
// by adding `GUI.getListRowStep(...)` to FontSelectionActivity::onEnter
// (FontSelectionActivity.cpp:105). onEnter DOES run here, so all seven cases
// segfaulted before their bodies ran — reporting as failures with no assertion
// output, which is how they stayed unnoticed. Verified pre-existing: the same
// seven crash at d3a96f4d, before any of the keyboard work.
//
// Skipping is not a fix, it is honesty: they were already not executing. The
// fix is to move them to test/renderer_bounds, which links the real theme
// stack's collaborators and can construct a theme.
constexpr bool kThemeBackedActivitiesRunnable = false;

#define SKIP_WITHOUT_THEME()                                                             \
  do {                                                                                   \
    if (!kThemeBackedActivitiesRunnable)                                                 \
      GTEST_SKIP() << "FontSelectionActivity::onEnter calls a GUI virtual and this "     \
                      "suite's UITheme double has a null currentTheme; see the comment " \
                      "above makeFontSelection().";                                      \
  } while (0)

std::unique_ptr<Activity> makeFontSelection() {
  // Null registry: no SD fonts installed, so the list is the two built-in Noto
  // faces (FontSelectionActivity.cpp:96-99) and nothing touches the SD card.
  return std::make_unique<FontSelectionActivity>(host::renderer(), host::input(), nullptr);
}

std::unique_ptr<Activity> makeIntervalSelection() {
  return std::make_unique<IntervalSelectionActivity>(host::renderer(), host::input(), "TestInterval",
                                                     StrId::STR_NONE_OPT, 30, 10, 120, 10, 30);
}

struct BackEdgeCase {
  const char* label;
  ActivityFactory factory;
};

// Without this gtest prints the param as "16-byte object <11-60 97-02 ...>",
// which buries which activity failed.
void PrintTo(const BackEdgeCase& testCase, std::ostream* os) { *os << testCase.label; }

const BackEdgeCase kBackEdgeCases[] = {
    {"FontSelection", makeFontSelection},
    {"IntervalSelection", makeIntervalSelection},
};

class ActivityInput : public ::testing::Test {
 protected:
  void SetUp() override { host::reset(); }
  void TearDown() override { host::reset(); }
};

// ===========================================================================
// (a) The exact regression: FontSelectionActivity must not finish on the press.
// ===========================================================================

TEST_F(ActivityInput, FontSelectionSurvivesTheBackPressAndLeavesOnTheRelease) {
  SKIP_WITHOUT_THEME();
  host::setRootActivity(makeFontSelection());
  ASSERT_EQ(host::currentActivityName(), "FontSelect");
  host::resetCounters();

  // Frame 1: the press edge, and only the press edge.
  host::pressFrame(HalGPIO::BTN_BACK);
  EXPECT_EQ(host::currentActivityName(), "FontSelect")
      << "finished on the Back PRESS: the release edge is now orphaned and lands on whoever is current next";
  EXPECT_EQ(host::counters().pops, 0);

  // Frame 2: the release edge — now it may leave.
  host::releaseFrame(HalGPIO::BTN_BACK);
  EXPECT_EQ(host::counters().pops, 1) << "did not finish on the Back RELEASE either";
  EXPECT_NE(host::currentActivityName(), "FontSelect");
}

// ===========================================================================
// (b) + (c) The general guard, table-driven over every linkable child of a
// release-edge launcher: one physical press, one transition.
// ===========================================================================

class BackEdgeConvention : public ActivityInput, public ::testing::WithParamInterface<BackEdgeCase> {};

TEST_P(BackEdgeConvention, OnePhysicalBackPressCausesExactlyOneTransition) {
  // Only the FontSelection param constructs a theme-touching onEnter; the
  // IntervalSelection param runs normally and still covers the convention.
  if (std::string(GetParam().label) == "FontSelection") SKIP_WITHOUT_THEME();
  g_parentObs = ParentObservations{};
  auto parentOwned = std::make_unique<ReaderParentDouble>(host::renderer(), host::input());
  auto* parent = parentOwned.get();
  host::setRootActivity(std::move(parentOwned));

  // The real launch path: startActivityForResult pushes and installs a result
  // handler, applied at the next loop() boundary.
  // Lambda captures nothing owned by the activity: it may run after the parent is
  // gone on the defective path.
  parent->startActivityForResult(GetParam().factory(), [](const ActivityResult&) { g_parentObs.childResults++; });
  host::frame();
  ASSERT_NE(host::currentActivityName(), "TestReaderParent") << "child was never pushed";

  const std::string childName = host::currentActivityName();
  host::resetCounters();

  // One tap: press frame, then release frame.
  host::tap(HalGPIO::BTN_BACK);
  // A couple of idle frames — the orphaned-release bug surfaced one frame late,
  // so give it room to show up.
  host::frames(2);

  EXPECT_EQ(host::counters().transitions, 1)
      << childName << ": one Back press produced " << host::counters().transitions
      << " activity transitions. The child consumed the wrong edge, so the parent got the other one.";
  EXPECT_EQ(host::counters().goHome, 0) << childName << ": the user was thrown out to Home by a single Back tap";
  EXPECT_EQ(g_parentObs.backReleasesSeen, 0)
      << childName << ": the parent saw the Back release the child should have eaten";
  EXPECT_EQ(host::currentActivityName(), "TestReaderParent") << childName << ": did not return to its launcher";
  EXPECT_EQ(g_parentObs.childResults, 1) << childName << ": result was not delivered to the launcher";
}

TEST_P(BackEdgeConvention, DoesNotFinishOnTheBackPressEdge) {
  // Only the FontSelection param constructs a theme-touching onEnter; the
  // IntervalSelection param runs normally and still covers the convention.
  if (std::string(GetParam().label) == "FontSelection") SKIP_WITHOUT_THEME();
  host::setRootActivity(GetParam().factory());
  const std::string childName = host::currentActivityName();
  host::resetCounters();

  host::pressFrame(HalGPIO::BTN_BACK);

  EXPECT_EQ(host::counters().pops, 0)
      << childName << " finished on the Back PRESS. Its launcher (the reader) acts on the RELEASE, "
      << "so that release will be delivered to the launcher as a second Back — see FontSelectionActivity.cpp:116.";
  EXPECT_EQ(host::currentActivityName(), childName);
}

INSTANTIATE_TEST_SUITE_P(ReleaseEdgeChildren, BackEdgeConvention, ::testing::ValuesIn(kBackEdgeCases),
                         [](const ::testing::TestParamInfo<BackEdgeCase>& info) {
                           return std::string(info.param.label);
                         });

// ===========================================================================
// The seam itself: MappedInputManager's logical -> physical mapping. If Back
// stopped following the user's remap, every test above would still pass while
// the device was broken.
// ===========================================================================

TEST_F(ActivityInput, LogicalBackFollowsTheUserRemap) {
  // Default mapping.
  host::buttons().simSetDown(HalGPIO::BTN_BACK, true);
  host::buttons().update();
  EXPECT_TRUE(host::input().wasPressed(MappedInputManager::Button::Back));
  EXPECT_FALSE(host::input().wasPressed(MappedInputManager::Button::Confirm));

  host::reset();

  // Remapped: Back now lives on the physical Right button, and logical Back
  // must follow it (MappedInputManager.cpp:20-22).
  SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_RIGHT;
  SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_BACK;

  host::buttons().simSetDown(HalGPIO::BTN_RIGHT, true);
  host::buttons().update();
  EXPECT_TRUE(host::input().wasPressed(MappedInputManager::Button::Back));

  host::buttons().simSetDown(HalGPIO::BTN_RIGHT, false);
  host::buttons().update();
  EXPECT_TRUE(host::input().wasReleased(MappedInputManager::Button::Back));
  EXPECT_FALSE(host::input().isPressed(MappedInputManager::Button::Back));
}

TEST_F(ActivityInput, FontSelectionLeavesViaTheRemappedBackButton) {
  SKIP_WITHOUT_THEME();
  SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_RIGHT;
  SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_BACK;

  host::setRootActivity(makeFontSelection());
  ASSERT_EQ(host::currentActivityName(), "FontSelect");
  host::resetCounters();

  host::pressFrame(HalGPIO::BTN_RIGHT);
  EXPECT_EQ(host::currentActivityName(), "FontSelect");
  host::releaseFrame(HalGPIO::BTN_RIGHT);
  EXPECT_EQ(host::counters().pops, 1);
}

// ===========================================================================
// One physical edge, one action. Holding Back must not exit on its own: only
// the release edge may. This is the other half of the double-consume family
// that OpdsBookBrowserActivity's `consumeBack` flag
// (OpdsBookBrowserActivity.h:40) also guards against.
// ===========================================================================

TEST_F(ActivityInput, HoldingBackDoesNotRepeatTheExit) {
  SKIP_WITHOUT_THEME();
  host::setRootActivity(makeFontSelection());
  host::resetCounters();

  host::pressFrame(HalGPIO::BTN_BACK);
  host::frames(5);  // still held: no new edges
  EXPECT_EQ(host::counters().pops, 0) << "a held Back produced an exit without a release";

  host::releaseFrame(HalGPIO::BTN_BACK);
  EXPECT_EQ(host::counters().pops, 1);
}

// ===========================================================================
// The render lock around the two calls that free fonts under the render task.
// changeFontSize() and applySelectedFont() both call
// SdCardFontSystem::ensureLoaded(), which unloads the resident SdCardFont;
// ActivityManager::loop() holds no lock, so the activity must take one itself
// (FontSelectionActivity.cpp:200-220, 229-234).
// ===========================================================================

// Guards the two tests below from being vacuous: if peek() answered "held" (or
// "not held") unconditionally, they would pass no matter what the activity did.
TEST_F(ActivityInput, HarnessReportsRenderLockStateHonestly) {
  ASSERT_FALSE(RenderLock::peek());
  {
    RenderLock lock;
    EXPECT_TRUE(RenderLock::peek());
    lock.unlock();
    EXPECT_FALSE(RenderLock::peek());
  }
  EXPECT_FALSE(RenderLock::peek());
}

TEST_F(ActivityInput, ConfirmAppliesTheFontUnderTheRenderLock) {
  SKIP_WITHOUT_THEME();
  host::setRootActivity(makeFontSelection());
  host::resetCounters();

  // applySelectedFont is bound to the Confirm PRESS (FontSelectionActivity.cpp:139).
  host::pressFrame(HalGPIO::BTN_CONFIRM);

  ASSERT_EQ(host::fontSystemCalls().ensureLoaded, 1) << "Confirm did not apply the selected font";
  EXPECT_EQ(host::fontSystemCalls().ensureLoadedWithoutRenderLock, 0)
      << "ensureLoaded() ran with no RenderLock held: it unloads the resident SdCardFont while the render "
         "task may hold a reference into the font map";
  EXPECT_EQ(host::currentActivityName(), "FontSelect") << "Confirm must apply and stay, not leave";
}

TEST_F(ActivityInput, SideButtonChangesFontSizeUnderTheRenderLock) {
  SKIP_WITHOUT_THEME();
  host::setRootActivity(makeFontSelection());
  ASSERT_EQ(SETTINGS.fontSizeSlot, CrossPointSettings::DEFAULT_FONT_SIZE_SLOT);
  host::resetCounters();

  // PageForward with the default PREV_NEXT side layout is BTN_DOWN
  // (MappedInputManager.cpp:52-56); the size step runs on the RELEASE.
  // Sizes are slots now (S/M/L/XL = 0..READER_FONT_SLOT_COUNT-1), not the old
  // MEDIUM/LARGE enums; default is slot 1 (M), the top of the ramp is the last
  // slot.
  host::tap(HalGPIO::BTN_DOWN);

  EXPECT_EQ(SETTINGS.fontSizeSlot, CrossPointSettings::DEFAULT_FONT_SIZE_SLOT + 1)
      << "side button did not step the reader font size slot";
  ASSERT_EQ(host::fontSystemCalls().ensureLoaded, 1);
  EXPECT_EQ(host::fontSystemCalls().ensureLoadedWithoutRenderLock, 0)
      << "changeFontSize() called ensureLoaded() with no RenderLock held";

  // And it clamps rather than wrapping at the top of the ramp.
  constexpr uint8_t kTopSlot = READER_FONT_SLOT_COUNT - 1;
  host::tap(HalGPIO::BTN_DOWN);
  EXPECT_EQ(SETTINGS.fontSizeSlot, kTopSlot);
  host::tap(HalGPIO::BTN_DOWN);
  EXPECT_EQ(SETTINGS.fontSizeSlot, kTopSlot) << "font size slot wrapped instead of clamping";
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  const int rc = RUN_ALL_TESTS();
  // ActivityManager.h:75 is `~ActivityManager() { assert(false); }` — the
  // manager is a singleton that must never be destroyed — so a normal return
  // would abort in global teardown and report a pass as a crash. The simulator
  // solves it the same way (simulator_main.cpp:42).
  std::fflush(nullptr);
  _exit(rc);
}
