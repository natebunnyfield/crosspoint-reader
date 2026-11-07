// BOOX Picco input (src/components/PiccoInput.h), owner ruling 2026-11-06:
// the rocker sends Right/Left, and outside keyboards every single-finger
// gesture is a synthesized button with the iOS zen defaults.
//
// Points are the renderer's LOGICAL portrait pixels (480 x 800 on the Picco's
// 800 x 480 panel, GfxRenderer::tapToLogical), the space
// MappedInputManager::update feeds the decoder in.
#include <gtest/gtest.h>

#include <vector>

#include "components/PiccoInput.h"

namespace {

using Button = MappedInputManager::Button;
using piccoinput::Route;

constexpr int SCREEN_W = 480;

// ---------------------------------------------------------------------------
// The rocker
// ---------------------------------------------------------------------------

TEST(PiccoRocker, UpperEndIsRightLowerEndIsLeft) {
  EXPECT_EQ(piccoinput::rockerRoute(Button::Right), Route::RockerUpper);
  EXPECT_EQ(piccoinput::rockerRoute(Button::Left), Route::RockerLower);
  // The list/option steppers and the screen directions resolve the same way.
  EXPECT_EQ(piccoinput::rockerRoute(Button::NavNext), Route::RockerUpper);
  EXPECT_EQ(piccoinput::rockerRoute(Button::NavPrevious), Route::RockerLower);
  EXPECT_EQ(piccoinput::rockerRoute(Button::ScreenRight), Route::RockerUpper);
  EXPECT_EQ(piccoinput::rockerRoute(Button::ScreenLeft), Route::RockerLower);
}

TEST(PiccoRocker, NoUpDownOrPageButtonFromTheRocker) {
  for (const Button b : {Button::Up, Button::Down, Button::ScreenUp, Button::ScreenDown, Button::PageBack,
                         Button::PageForward, Button::PageNext, Button::PagePrevious}) {
    EXPECT_EQ(piccoinput::rockerRoute(b), Route::None) << static_cast<int>(b);
  }
}

TEST(PiccoRocker, BackConfirmPowerKeepTheSharedMapping) {
  EXPECT_EQ(piccoinput::rockerRoute(Button::Back), Route::Unchanged);
  EXPECT_EQ(piccoinput::rockerRoute(Button::Confirm), Route::Unchanged);  // GPIO5
  EXPECT_EQ(piccoinput::rockerRoute(Button::Power), Route::Unchanged);
}

// Every enumerator, so a Button added later cannot slip onto the rocker
// unnoticed: exactly six logical buttons come from it, three per end.
TEST(PiccoRocker, ExactlySixButtonsComeFromTheRocker) {
  int upper = 0;
  int lower = 0;
  for (int i = 0; i <= static_cast<int>(Button::ScreenDown); ++i) {
    const Route r = piccoinput::rockerRoute(static_cast<Button>(i));
    if (r == Route::RockerUpper) ++upper;
    if (r == Route::RockerLower) ++lower;
  }
  EXPECT_EQ(upper, 3);
  EXPECT_EQ(lower, 3);
}

// ---------------------------------------------------------------------------
// Gestures
// ---------------------------------------------------------------------------

struct Frame {
  piccoinput::Sample s;
  unsigned long ms;
};

Frame at(const int x, const int y, const unsigned long ms) { return Frame{{true, true, x, y}, ms}; }
Frame lift(const unsigned long ms) { return Frame{{false, false, 0, 0}, ms}; }
Frame ended(const unsigned long ms) { return Frame{{true, false, 0, 0}, ms}; }  // suppressed contact

// A still finger at (x, y) from t0 for heldMs, sampled every 10 ms,
// then (if liftAt != 0) lifted at liftAt.
std::vector<Frame> still(const int x, const int y, const unsigned long t0, const unsigned long heldMs,
                         const unsigned long liftAt = 0) {
  std::vector<Frame> f;
  for (unsigned long t = 0; t <= heldMs; t += 10) f.push_back(at(x, y, t0 + t));
  if (f.back().ms != t0 + heldMs) f.push_back(at(x, y, t0 + heldMs));
  if (liftAt != 0) f.push_back(lift(liftAt));
  return f;
}

// Feeds the frames and returns every button the decoder fired, in order.
std::vector<piccoinput::Result> run(piccoinput::GestureDecoder& d, const std::vector<Frame>& frames,
                                    const bool keyboard = false) {
  std::vector<piccoinput::Result> out;
  for (const Frame& f : frames) {
    const piccoinput::Result r = d.feed(f.s, f.ms, keyboard, SCREEN_W);
    if (r.fired) out.push_back(r);
  }
  return out;
}

// A straight stroke from (sx, sy) to (ex, ey) over durationMs, sampled every
// 10 ms (the 100 Hz poll while a finger is down), then lifted.
std::vector<Frame> stroke(const int sx, const int sy, const int ex, const int ey, const unsigned long durationMs,
                          const unsigned long t0 = 1000) {
  std::vector<Frame> f;
  const int steps = static_cast<int>(durationMs / 10);
  for (int i = 0; i <= steps; ++i) {
    const int x = sx + (ex - sx) * i / (steps == 0 ? 1 : steps);
    const int y = sy + (ey - sy) * i / (steps == 0 ? 1 : steps);
    f.push_back(at(x, y, t0 + static_cast<unsigned long>(i) * 10));
  }
  f.push_back(lift(t0 + durationMs + 10));
  return f;
}

Button only(const std::vector<piccoinput::Result>& r) {
  EXPECT_EQ(r.size(), 1u);
  return r.empty() ? Button::Power : r.front().button;
}

TEST(PiccoGesture, TapIsRight) {
  piccoinput::GestureDecoder d;
  EXPECT_EQ(only(run(d, stroke(240, 400, 240, 400, 80))), Button::Right);
}

TEST(PiccoGesture, SwipesFollowTheIosZenDefaults) {
  {
    piccoinput::GestureDecoder d;
    EXPECT_EQ(only(run(d, stroke(400, 400, 200, 410, 200))), Button::Right) << "swipe left";
  }
  {
    piccoinput::GestureDecoder d;
    EXPECT_EQ(only(run(d, stroke(200, 400, 400, 410, 200))), Button::Left) << "swipe right, mid-screen";
  }
  {
    piccoinput::GestureDecoder d;
    EXPECT_EQ(only(run(d, stroke(240, 600, 250, 300, 200))), Button::Back) << "swipe up";
  }
  {
    piccoinput::GestureDecoder d;
    EXPECT_EQ(only(run(d, stroke(240, 300, 250, 600, 200))), Button::Confirm) << "swipe down";
  }
}

TEST(PiccoGesture, LeftEdgeSwipeIsBackAndWinsOverSwipeRight) {
  piccoinput::GestureDecoder d;
  EXPECT_EQ(only(run(d, stroke(20, 400, 260, 410, 200))), Button::Back);
  // The left quarter is inclusive: 480 * 0.25 = 120.
  EXPECT_EQ(only(run(d, stroke(120, 400, 300, 400, 200, 3000))), Button::Back);
  // One pixel past it is an ordinary swipe right.
  EXPECT_EQ(only(run(d, stroke(121, 400, 300, 400, 200, 5000))), Button::Left);
  // From the edge but more down than across: a swipe down, not Back.
  EXPECT_EQ(only(run(d, stroke(20, 300, 80, 600, 200, 7000))), Button::Confirm);
}

TEST(PiccoGesture, HoldIsConfirmOnceAndEndsTheContact) {
  piccoinput::GestureDecoder d;
  std::vector<Frame> f;
  for (unsigned long t = 0; t <= 1200; t += 10) f.push_back(at(240 + static_cast<int>(t % 3), 400, 1000 + t));
  f.push_back(lift(2300));
  // Find the frame it fires on: the first at or past 500 ms.
  std::vector<piccoinput::Result> fired;
  unsigned long firedAt = 0;
  for (const Frame& fr : f) {
    const piccoinput::Result r = d.feed(fr.s, fr.ms, false, SCREEN_W);
    if (r.fired) {
      fired.push_back(r);
      firedAt = fr.ms;
    }
  }
  ASSERT_EQ(fired.size(), 1u) << "fires once, and the lift does not also tap";
  EXPECT_EQ(fired[0].button, Button::Confirm);
  EXPECT_TRUE(fired[0].endContact);
  EXPECT_EQ(firedAt, 1000u + piccoinput::LONG_PRESS_MS);
}

// --- "Match iOS exactly" (owner 2026-11-06): tap <= 400 ms and <= 28 px,
// hold at 750 ms, nothing in between.

TEST(PiccoGestureTiming, ATapLiftsWithin400Ms) {
  {
    piccoinput::GestureDecoder d;
    EXPECT_EQ(only(run(d, still(240, 400, 1000, 390, 1399))), Button::Right) << "399 ms";
  }
  {
    piccoinput::GestureDecoder d;
    EXPECT_EQ(only(run(d, still(240, 400, 1000, 390, 1400))), Button::Right) << "400 ms";
  }
  {
    piccoinput::GestureDecoder d;
    EXPECT_TRUE(run(d, still(240, 400, 1000, 400, 1401)).empty()) << "401 ms";
  }
}

TEST(PiccoGestureTiming, ATapMovesAtMost28Px) {
  {
    piccoinput::GestureDecoder d;
    EXPECT_EQ(only(run(d, {at(240, 400, 1000), at(268, 400, 1050), lift(1060)})), Button::Right) << "28 px across";
  }
  {
    piccoinput::GestureDecoder d;
    EXPECT_TRUE(run(d, {at(240, 400, 1000), at(269, 400, 1050), lift(1060)}).empty()) << "29 px across";
  }
  {
    piccoinput::GestureDecoder d;
    EXPECT_EQ(only(run(d, {at(240, 400, 1000), at(240, 372, 1050), lift(1060)})), Button::Right) << "28 px up";
  }
  {
    piccoinput::GestureDecoder d;
    EXPECT_TRUE(run(d, {at(240, 400, 1000), at(240, 371, 1050), lift(1060)}).empty()) << "29 px up";
  }
  // Out past the slop and back again: the excursion counts, not the end point.
  {
    piccoinput::GestureDecoder d;
    EXPECT_TRUE(run(d, {at(240, 400, 1000), at(270, 400, 1030), at(240, 400, 1060), lift(1070)}).empty());
  }
}

TEST(PiccoGestureTiming, ADriftShortOfASwipeIsNothing) {
  // 29..59 px used to be a tap under the SDK's release slop; now it is nothing.
  {
    piccoinput::GestureDecoder d;
    EXPECT_TRUE(run(d, {at(300, 400, 1000), at(241, 400, 1050), lift(1060)}).empty()) << "59 px";
  }
  // 60 px is a swipe, unchanged (InputManager.h:327).
  {
    piccoinput::GestureDecoder d;
    EXPECT_EQ(only(run(d, {at(300, 400, 1000), at(360, 400, 1050), lift(1060)})), Button::Left) << "60 px swipe";
  }
}

TEST(PiccoGestureTiming, TheHoldFiresAt750Ms) {
  piccoinput::GestureDecoder d;
  const std::vector<Frame> f = still(240, 400, 1000, 800);
  unsigned long firedAt = 0;
  int fires = 0;
  for (const Frame& fr : f) {
    const piccoinput::Result r = d.feed(fr.s, fr.ms, false, SCREEN_W);
    if (r.fired) {
      ++fires;
      firedAt = fr.ms;
    }
  }
  EXPECT_EQ(fires, 1);
  EXPECT_EQ(firedAt, 1750u);
  EXPECT_EQ(piccoinput::LONG_PRESS_MS, 750u);
}

TEST(PiccoGestureTiming, BetweenATapAndAHoldIsNothing) {
  // Down 749 ms (last frame at 749), lifted at 755: too long for a tap, too
  // short for a hold.
  {
    piccoinput::GestureDecoder d;
    EXPECT_TRUE(run(d, still(240, 400, 1000, 749, 1755)).empty()) << "749 ms";
  }
  // The same contact seen at 750 ms holds, and its lift adds nothing.
  {
    piccoinput::GestureDecoder d;
    EXPECT_EQ(only(run(d, still(240, 400, 1000, 750, 1755))), Button::Confirm) << "750 ms";
  }
}

TEST(PiccoGestureTiming, AHoldNeedsAStillFinger) {
  // Within the 28 px slop: still a hold.
  {
    piccoinput::GestureDecoder d;
    std::vector<Frame> f = {at(240, 400, 1000)};
    for (const Frame& fr : still(268, 400, 1010, 790)) f.push_back(fr);
    f.push_back(lift(1900));
    EXPECT_EQ(only(run(d, f)), Button::Confirm);
  }
  // 29 px: no hold, and (too long for a tap) no tap either.
  {
    piccoinput::GestureDecoder d;
    std::vector<Frame> f = {at(240, 400, 1000)};
    for (const Frame& fr : still(269, 400, 1010, 790)) f.push_back(fr);
    f.push_back(lift(1900));
    EXPECT_TRUE(run(d, f).empty());
  }
}

TEST(PiccoGesture, ASlowDragIsNothing) {
  // 700 ms is the SDK's swipe window (InputManager.h:329).
  {
    piccoinput::GestureDecoder d;
    EXPECT_EQ(only(run(d, {at(400, 400, 1000), at(200, 400, 1690), lift(1700)})), Button::Right) << "700 ms";
  }
  {
    piccoinput::GestureDecoder d;
    EXPECT_TRUE(run(d, {at(400, 400, 1000), at(200, 400, 1691), lift(1701)}).empty()) << "701 ms";
  }
}

TEST(PiccoGesture, AnEndedContactIsNoGesture) {
  // suppressTouchContact() mid-contact: the finger is still down but no longer
  // delivered. Its lift must not tap.
  piccoinput::GestureDecoder d;
  EXPECT_TRUE(run(d, {at(240, 400, 1000), at(240, 400, 1050), ended(1060), ended(1100), lift(1110)}).empty());
  // The next contact is delivered normally.
  EXPECT_EQ(only(run(d, stroke(240, 400, 240, 400, 50, 2000))), Button::Right);
}

// ---------------------------------------------------------------------------
// Inside keyboards: the left-edge Back, nothing else
// ---------------------------------------------------------------------------

TEST(PiccoGestureKeyboard, LeftEdgeSwipeIsStillBack) {
  piccoinput::GestureDecoder d;
  EXPECT_EQ(only(run(d, stroke(20, 600, 260, 610, 200), true)), Button::Back);
}

TEST(PiccoGestureKeyboard, EveryOtherGestureIsOff) {
  struct Case {
    std::vector<Frame> frames;
    const char* what;
  };
  std::vector<Frame> hold;
  for (unsigned long t = 0; t <= 1000; t += 10) hold.push_back(at(240, 600, 1000 + t));
  hold.push_back(lift(2010));
  const Case cases[] = {
      {stroke(240, 600, 240, 600, 80), "tap (the keyboard types it itself)"},
      {hold, "hold"},
      {stroke(400, 600, 200, 600, 200), "swipe left"},
      {stroke(200, 600, 400, 600, 200), "swipe right, mid-screen"},
      {stroke(240, 700, 240, 400, 200), "swipe up"},
      {stroke(240, 400, 240, 700, 200), "swipe down"},
  };
  for (const Case& c : cases) {
    piccoinput::GestureDecoder d;
    EXPECT_TRUE(run(d, c.frames, true).empty()) << c.what;
  }
}

TEST(PiccoGestureKeyboard, AKeyboardOpeningUnderTheFingerEndsTheContact) {
  // The contact began outside a keyboard; the keyboard switched on mid-way.
  piccoinput::GestureDecoder d;
  EXPECT_FALSE(d.feed(Frame{{true, true, 240, 400}, 0}.s, 1000, false, SCREEN_W).fired);
  EXPECT_FALSE(d.feed(Frame{{true, true, 240, 400}, 0}.s, 1100, true, SCREEN_W).fired);
  EXPECT_FALSE(d.feed(Frame{{true, true, 240, 400}, 0}.s, 1700, true, SCREEN_W).fired) << "no hold";
  EXPECT_FALSE(d.feed(Frame{{false, false, 0, 0}, 0}.s, 1710, true, SCREEN_W).fired) << "no tap";
}

}  // namespace
