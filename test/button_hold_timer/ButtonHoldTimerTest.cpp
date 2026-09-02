// The per-button hold timer, truth-tabled with an explicit clock.
//
// The defect it replaces is a CHORD defect (docs/ux-navigation-audit-2026-09-02.md
// F4/F9/F10): the SDK's single stopwatch is stamped by the first button down
// and answers that button's time for whichever button is asked about. Every
// case here is therefore a two-button case with the second button pressed late,
// and the assertion is always "the second button's own time" — the only reading
// the global timer cannot give.

#include <gtest/gtest.h>

#include "ButtonHoldTimer.h"

namespace {

constexpr uint8_t kRight = 3;
constexpr uint8_t kConfirm = 1;
constexpr uint8_t bit(const uint8_t i) { return static_cast<uint8_t>(1u << i); }

TEST(ButtonHoldTimer, SecondButtonOfAChordMeasuresItsOwnPress) {
  buttonhold::Timer t;
  // Right goes down at t=0 and stays down.
  t.frame(0, bit(kRight), 0, bit(kRight));
  // 900 ms later Confirm joins the chord.
  t.frame(900, bit(kConfirm), 0, bit(kRight) | bit(kConfirm));

  EXPECT_EQ(t.heldMs(kConfirm, 950), 50u) << "Confirm read the chord's first button's time — the F9 defect";
  EXPECT_EQ(t.heldMs(kRight, 950), 950u) << "Right's own press is unaffected by Confirm joining";
}

TEST(ButtonHoldTimer, ReleaseFrameReportsTheFinishedPressLength) {
  buttonhold::Timer t;
  t.frame(0, bit(kRight), 0, bit(kRight));
  t.frame(900, bit(kConfirm), 0, bit(kRight) | bit(kConfirm));
  // Confirm lifts at 1500; Right is still down.
  t.frame(1500, 0, bit(kConfirm), bit(kRight));

  // The release-driven checks (FileBrowser delete, FileManager stall catch)
  // read this on the release frame. 600, not 1500.
  EXPECT_EQ(t.heldMs(kConfirm, 1500), 600u);
  EXPECT_EQ(t.heldMs(kRight, 1500), 1500u);

  // One frame later the release is history and Confirm is out of play.
  t.frame(1550, 0, 0, bit(kRight));
  EXPECT_EQ(t.heldMs(kConfirm, 1550), 0u) << "a finished press must not linger past its release frame";
}

// A button already DOWN on the first frame -- its press edge consumed by a
// gpio.update() outside the pump, as setup()'s absorb loop does -- has no
// start stamp. It must read 0 rather than millis() minus 0, which at any
// plausible uptime is an instant long press (adversarial review 2026-09-02).
TEST(ButtonHoldTimer, ADownButtonWhosePressWasNeverSeenReadsZero) {
  buttonhold::Timer t;
  t.frame(50000, 0, 0, bit(kConfirm));
  EXPECT_EQ(t.heldMs(kConfirm, 50900), 0u);
  t.frame(51000, 0, bit(kConfirm), 0);
  EXPECT_EQ(t.heldMs(kConfirm, 51000), 0u) << "its release has no length either";
  // The next press of the same button is an ordinary one.
  t.frame(52000, bit(kConfirm), 0, bit(kConfirm));
  EXPECT_EQ(t.heldMs(kConfirm, 52700), 700u);
}

TEST(ButtonHoldTimer, NotInPlayReadsZero) {
  buttonhold::Timer t;
  EXPECT_EQ(t.heldMs(kConfirm, 12345), 0u) << "never pressed";
  t.frame(100, bit(kRight), 0, bit(kRight));
  EXPECT_EQ(t.heldMs(kConfirm, 400), 0u) << "another button's press must not leak in";
  EXPECT_EQ(t.heldMs(buttonhold::kButtons, 400), 0u) << "out-of-range index";
  EXPECT_EQ(t.heldMs(99, 400), 0u);
}

TEST(ButtonHoldTimer, ARepressRestampsRatherThanAccumulates) {
  buttonhold::Timer t;
  t.frame(0, bit(kConfirm), 0, bit(kConfirm));
  t.frame(2000, 0, bit(kConfirm), 0);
  EXPECT_EQ(t.heldMs(kConfirm, 2000), 2000u);
  // Same-frame release+press cannot happen (one edge per frame per button in
  // the HAL), so the repress is on a later frame.
  t.frame(2100, bit(kConfirm), 0, bit(kConfirm));
  EXPECT_EQ(t.heldMs(kConfirm, 2130), 30u) << "a second press carried the first press's time";
}

TEST(ButtonHoldTimer, EveryButtonHasItsOwnSlot) {
  buttonhold::Timer t;
  uint8_t all = 0;
  for (uint8_t i = 0; i < buttonhold::kButtons; ++i) {
    all = static_cast<uint8_t>(all | bit(i));
    t.frame(static_cast<unsigned long>(i) * 100, bit(i), 0, all);
  }
  const unsigned long now = 1000;
  for (uint8_t i = 0; i < buttonhold::kButtons; ++i) {
    EXPECT_EQ(t.heldMs(i, now), now - static_cast<unsigned long>(i) * 100) << "button " << int(i);
  }
}

}  // namespace
