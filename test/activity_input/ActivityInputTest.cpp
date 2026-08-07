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
#include "components/UITheme.h"
#include "components/themes/lyra/LyraTheme.h"

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

// A note on the FontSelection cases, because they spent a while not running.
//
// This suite used to double UITheme with a null `currentTheme`, so GUI (==
// getTheme()) was a null dereference and only render() — which the harness
// never calls — was allowed to touch it. cb98d4fa ("prose specimen, 3-row list,
// overlaid badge") broke that by adding `GUI.getListRowStep(...)` to
// FontSelectionActivity::onEnter (FontSelectionActivity.cpp:105), which DOES
// run here: all seven FontSelection cases segfaulted before their bodies ran,
// reporting as failures with no assertion output, and 536020e7 then skipped
// them to make that visible.
//
// They execute again because the suite links the REAL theme stack now
// (crosspoint_add_theme_stack, test/cmake/HostThemeStack.cmake), so
// getListRowStep is LyraTheme's genuine override. The activity was not changed
// to dodge the virtual — it legitimately needs it.
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
// The theme this suite links is the REAL one, and this is what says so.
//
// Every FontSelection case below runs an onEnter that calls GUI.getListRowStep
// — a virtual with two live implementations. Against the old null-currentTheme
// double that was a segfault; against a hypothetical future double that
// answered from BaseMetrics it would silently be the wrong number, and the list
// under test would be sized by a theme the device does not ship. So pin the
// value to LyraMetrics, which BaseMetrics does not match on any of the three
// fields involved (30/50/16 vs 40/60/18).
// ===========================================================================

TEST_F(ActivityInput, TheLinkedThemeIsTheRealLyraChain) {
  EXPECT_EQ(GUI.getListRowStep(false), LyraMetrics::values.listRowHeight);
  EXPECT_EQ(GUI.getListRowStep(true, 1), LyraMetrics::values.listWithSubtitleRowHeight);
  EXPECT_EQ(GUI.getListRowStep(true, 3),
            LyraMetrics::values.listWithSubtitleRowHeight + 2 * LyraMetrics::values.listSubtitleLineStep);

  // ...and it is not BaseTheme's, which is what a metrics-only double resolves
  // to and what the numbers above would degrade into unnoticed.
  EXPECT_NE(GUI.getListRowStep(false), BaseMetrics::values.listRowHeight);
}

// render() used to be out of bounds in this suite — the null-currentTheme double
// made GUI a null dereference, so the harness deliberately never rendered a
// frame. With the real theme linked it is just another call, and this is the
// case that keeps it that way: it drives the whole draw path (header, list,
// preview pane, button hints, and the grayscale store/restore the single-buffer
// mode needs) against the real GfxRenderer.
//
// It asserts nothing about pixels. Its job is to fail loudly if the theme is
// ever unwired again, at the strongest point rather than in an onEnter.
TEST_F(ActivityInput, RenderDrawsAFullFrameThroughTheRealTheme) {
  host::setRootActivity(makeFontSelection());
  host::currentActivity()->render(RenderLock{});
}

// ===========================================================================
// (a) The exact regression: FontSelectionActivity must not finish on the press.
// ===========================================================================

TEST_F(ActivityInput, FontSelectionSurvivesTheBackPressAndLeavesOnTheRelease) {
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

// Confirm has two meanings (FontSelectionActivity.cpp, cb98d4fa): on the row
// that is ALREADY applied it swaps the specimen, on any other row it applies.
// With Libre Franklin as the only built-in family, a null-registry picker has
// exactly ONE row, so the apply arm is unreachable here — it needs SD fonts,
// which this suite's registry does not provide. Its render-lock contract is
// the same one SideButtonChangesFontSizeUnderTheRenderLock asserts on the
// changeFontSize() path. What IS assertable without SD fonts: navigation on a
// one-row list must not escape the row or turn Confirm into an apply.
TEST_F(ActivityInput, OneRowListKeepsConfirmOnTheSpecimenSwap) {
  host::setRootActivity(makeFontSelection());

  // A Right tap on a single-row list wraps back to the same row.
  host::tap(HalGPIO::BTN_RIGHT);
  host::resetCounters();

  host::pressFrame(HalGPIO::BTN_CONFIRM);

  EXPECT_EQ(host::fontSystemCalls().ensureLoaded, 0)
      << "Confirm applied on a one-row list: the only row is the applied font, so this was a full "
         "unload/reload of the resident face for no change";
  EXPECT_EQ(host::currentActivityName(), "FontSelect") << "Confirm must stay, not leave";
}

// The other half of that split. It is here because the apply test above is the
// kind that quietly stops testing anything: if Confirm ever went back to always
// applying, this case fails and says so, rather than the tap above silently
// becoming pointless.
TEST_F(ActivityInput, ConfirmOnTheAppliedRowSwapsTheSpecimenInsteadOfReapplying) {
  host::setRootActivity(makeFontSelection());
  host::resetCounters();

  // Cursor is on the applied row straight out of onEnter.
  host::pressFrame(HalGPIO::BTN_CONFIRM);

  EXPECT_EQ(host::fontSystemCalls().ensureLoaded, 0)
      << "Confirm re-applied the font that was already applied: that is an ensureLoaded() — a full unload and "
         "reload of the resident face — for no change";
  EXPECT_EQ(host::currentActivityName(), "FontSelect");
}

TEST_F(ActivityInput, SideButtonChangesFontSizeUnderTheRenderLock) {
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
