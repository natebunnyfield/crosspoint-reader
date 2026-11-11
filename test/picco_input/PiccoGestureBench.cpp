// Monte-Carlo benchmark for the Picco gesture decoder (src/components/PiccoInput.h).
//
// The unit tests pin each rule with a hand-built trace; this drives the REAL
// GestureDecoder with thousands of modelled human gestures through a modelled
// TMA525C report stream and scores every one as correct, a WRONG action, or
// missed. It exists so a change to the decoder is judged by numbers rather
// than by argument (docs/picco-gestures-2026-11-07.md).
//
// EVERY MODEL PARAMETER BELOW IS AN ASSUMPTION, not a measurement: nobody has
// logged the TMA525C's report rate, its position noise or how often a
// lift-off is lost on this unit (TOUCH_PROBE_DEBUG would). The scenarios
// bracket them instead -- a clean controller, a noisy one, one that loses lift-
// offs -- and the gate asserts on all of them. Seeded: the same numbers every
// run.
//
// What the model reproduces from the code it stands in for:
//   * the SDK keeps a contact whose lift-off report was lost "down" for
//     PICCO_TOUCH_STALE_MS (3 s) and then releases it (freeink-sdk
//     InputManager.cpp pollTma525c);
//   * while the stale contact is held, a new finger's reports MOVE the same
//     contact (no new touch-down);
//   * reportMs is the poll time at which a report was read (the SDK stamps it
//     with `now`), so a poll that finds no new report sees the old stamp;
//   * the main loop polls every 10 ms while a contact is down, with occasional
//     stalls (SD work, layout) during which nothing is polled.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "components/PiccoInput.h"

namespace {

using Button = MappedInputManager::Button;

constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 800;
constexpr unsigned long SDK_STALE_MS = 3000;
constexpr double kPi = 3.14159265358979323846;

struct Controller {
  const char* name;
  double reportHz;          // reports per second while a finger is down
  double noisePx;           // per-report position noise (sigma)
  double lostLiftOff;       // probability a lift-off report is lost
  double stallPerPoll;      // probability a poll starts a main-loop stall
  double dropoutPerReport;  // probability a due report starts a 210-320 ms silence with the finger still down
};

// The last two are presses the owner's ruling says must do NOTHING: longer than
// a tap, shorter than a hold; and a drag too slow to be a swipe. Anything they
// fire is WRONG (adversarial review 2026-11-07: without them the benchmark
// could not see a decoder that turns pages on a resting finger).
enum class Intent { Tap, Hold, SwipeLeft, SwipeRight, SwipeUp, SwipeDown, EdgeBack, LongPress, SlowDrag };
constexpr int kIntents = 9;
const char* intentName(const Intent i) {
  switch (i) {
    case Intent::Tap:
      return "tap";
    case Intent::Hold:
      return "hold";
    case Intent::SwipeLeft:
      return "swipe L";
    case Intent::SwipeRight:
      return "swipe R";
    case Intent::SwipeUp:
      return "swipe U";
    case Intent::SwipeDown:
      return "swipe D";
    case Intent::EdgeBack:
      return "edge back";
    case Intent::LongPress:
      return "press 0.5s";
    case Intent::SlowDrag:
      return "slow drag";
  }
  return "?";
}
Button expected(const Intent i) {
  switch (i) {
    case Intent::Tap:
      return Button::Right;
    case Intent::Hold:
      return Button::Confirm;
    case Intent::SwipeLeft:
      return Button::Right;
    case Intent::SwipeRight:
      return Button::Left;
    case Intent::SwipeUp:
      return Button::Back;
    case Intent::SwipeDown:
      return Button::Confirm;
    case Intent::EdgeBack:
      return Button::Back;
    case Intent::LongPress:
    case Intent::SlowDrag:
      return Button::Power;  // stands for "nothing": the decoder never fires Power
  }
  return Button::Power;
}

// A finger's planned path: position at time t (ms since touch-down) for t in
// [0, durationMs], then it lifts.
struct Stroke {
  Intent intent;
  double sx, sy, ex, ey;
  double durationMs;
  double wobblePx;  // slow drift amplitude for taps and holds
  double curve;     // sideways bow of a swipe, as a fraction of its length
};

struct Point {
  double x, y;
};

Point along(const Stroke& s, const double t) {
  const double u = s.durationMs <= 0 ? 1.0 : std::min(1.0, std::max(0.0, t / s.durationMs));
  // Swipes accelerate and decelerate (smoothstep); taps and holds drift.
  const double e = u * u * (3 - 2 * u);
  double x = s.sx + (s.ex - s.sx) * e;
  double y = s.sy + (s.ey - s.sy) * e;
  const double len = std::hypot(s.ex - s.sx, s.ey - s.sy);
  if (len > 1) {
    const double bow = s.curve * len * std::sin(kPi * e);
    x += -(s.ey - s.sy) / len * bow;
    y += (s.ex - s.sx) / len * bow;
  }
  x += s.wobblePx * std::sin(t / 37.0);
  y += s.wobblePx * std::cos(t / 53.0);
  return {x, y};
}

class Rng {
 public:
  explicit Rng(const unsigned seed) : g_(seed) {}
  double uni(const double a, const double b) { return std::uniform_real_distribution<double>(a, b)(g_); }
  double norm(const double sigma) { return std::normal_distribution<double>(0.0, sigma)(g_); }
  bool chance(const double p) { return uni(0, 1) < p; }

 private:
  std::mt19937 g_;
};

// A plausible human gesture of each kind. Ranges are guesses at ordinary use:
// taps 50-250 ms, holds 900-1300 ms, swipes 80-260 px in 100-450 ms with the
// direction off by a few degrees (sometimes a lot), page-turn swipes start
// away from the left quarter, edge-Back swipes start in it.
Stroke makeStroke(Rng& r, const Intent intent) {
  Stroke s{intent, 0, 0, 0, 0, 0, 0, 0};
  const auto angleNoise = [&r]() { return (r.chance(0.15) ? r.norm(28) : r.norm(10)) * kPi / 180.0; };
  switch (intent) {
    case Intent::Tap:
      s.sx = r.uni(40, SCREEN_W - 40);
      s.sy = r.uni(60, SCREEN_H - 60);
      s.ex = s.sx + r.norm(3);
      s.ey = s.sy + r.norm(3);
      s.durationMs = r.uni(50, 250);
      s.wobblePx = r.uni(0, 3);
      return s;
    case Intent::LongPress:
      s.sx = r.uni(40, SCREEN_W - 40);
      s.sy = r.uni(60, SCREEN_H - 60);
      s.ex = s.sx + r.norm(3);
      s.ey = s.sy + r.norm(3);
      s.durationMs = r.uni(450, 700);
      s.wobblePx = r.uni(0, 3);
      return s;
    case Intent::SlowDrag:
      s.sx = r.uni(150, 330);
      s.sy = r.uni(150, SCREEN_H - 150);
      s.ex = s.sx + (r.chance(0.5) ? 1 : -1) * r.uni(70, 120);
      s.ey = s.sy + r.norm(5);
      s.durationMs = r.uni(800, 1200);
      return s;
    case Intent::Hold:
      s.sx = r.uni(40, SCREEN_W - 40);
      s.sy = r.uni(60, SCREEN_H - 60);
      s.ex = s.sx + r.norm(4);
      s.ey = s.sy + r.norm(4);
      s.durationMs = r.uni(900, 1300);
      s.wobblePx = r.uni(0, 5);
      return s;
    default:
      break;
  }
  double base = 0;  // direction of travel, radians, screen coordinates (y down)
  double len = r.uni(80, 260);
  s.durationMs = r.uni(100, 450);
  switch (intent) {
    case Intent::SwipeLeft:
      base = kPi;
      s.sx = r.uni(len + 20, SCREEN_W - 10);
      s.sy = r.uni(100, SCREEN_H - 100);
      break;
    case Intent::SwipeRight:
      base = 0;
      s.sx = r.uni(SCREEN_W * 0.27, std::max(SCREEN_W * 0.27 + 1, SCREEN_W - 10 - len));
      s.sy = r.uni(100, SCREEN_H - 100);
      break;
    case Intent::SwipeUp:
      base = -kPi / 2;
      s.sx = r.uni(60, SCREEN_W - 60);
      s.sy = r.uni(len + 40, SCREEN_H - 20);
      break;
    case Intent::SwipeDown:
      base = kPi / 2;
      s.sx = r.uni(60, SCREEN_W - 60);
      s.sy = r.uni(20, SCREEN_H - len - 40);
      break;
    case Intent::EdgeBack:
      base = 0;
      len = r.uni(80, 220);
      s.sx = r.uni(4, SCREEN_W * 0.2);
      s.sy = r.uni(100, SCREEN_H - 100);
      break;
    default:
      break;
  }
  const double a = base + angleNoise();
  s.ex = s.sx + len * std::cos(a);
  s.ey = s.sy + len * std::sin(a);
  s.curve = r.norm(0.06);
  return s;
}

struct Outcome {
  std::vector<piccoinput::Result> fired;
};

// Plays a sequence of strokes (each starting `gapMs` after the previous one
// lifted) through the modelled controller, SDK and poll loop.
Outcome play(Rng& r, const Controller& c, const std::vector<Stroke>& strokes, const std::vector<double>& gapsMs) {
  piccoinput::GestureDecoder dec;
  Outcome out;
  const double reportEvery = 1000.0 / c.reportHz;
  // Absolute schedule of each stroke.
  std::vector<double> startAt;
  double t0 = 1000;
  for (size_t i = 0; i < strokes.size(); ++i) {
    startAt.push_back(t0);
    t0 += strokes[i].durationMs + (i < gapsMs.size() ? gapsMs[i] : 0);
  }
  const double endAt = t0 + SDK_STALE_MS + 200;

  // SDK state.
  bool sdkDown = false;
  int sdkX = 0, sdkY = 0;
  unsigned long sdkReportMs = 0;
  unsigned long sdkReleaseAt = 0;
  // Controller: which stroke is on the glass, its next report time, whether
  // its lift-off will be lost.
  std::vector<double> nextReport(strokes.size(), 0);
  std::vector<bool> liftSent(strokes.size(), false);
  std::vector<bool> liftLost(strokes.size(), false);
  std::vector<double> silentUntil(strokes.size(), 0);
  for (size_t i = 0; i < strokes.size(); ++i) {
    nextReport[i] = startAt[i] + r.uni(0, reportEvery);
    liftLost[i] = r.chance(c.lostLiftOff);
  }

  double stallUntil = 0;
  for (double now = 1000; now <= endAt; now += 10) {
    if (now < stallUntil) continue;  // the main loop is busy: nothing polls
    if (r.chance(c.stallPerPoll)) stallUntil = now + r.uni(100, 300);
    const unsigned long nowMs = static_cast<unsigned long>(now);
    // The controller's pending report for this poll: the SDK reads one report
    // per poll and keeps the latest, so model "latest report since last poll".
    bool newReport = false;
    bool lifted = false;
    for (size_t i = 0; i < strokes.size(); ++i) {
      const double down = startAt[i];
      const double up = down + strokes[i].durationMs;
      if (now < down) continue;
      if (now <= up) {
        if (now >= nextReport[i] && now >= silentUntil[i] && r.chance(c.dropoutPerReport)) {
          silentUntil[i] = now + r.uni(210, 320);
        }
        if (now >= nextReport[i] && now >= silentUntil[i]) {
          const Point p = along(strokes[i], now - down);
          sdkX = static_cast<int>(std::lround(p.x + r.norm(c.noisePx)));
          sdkY = static_cast<int>(std::lround(p.y + r.norm(c.noisePx)));
          newReport = true;
          while (nextReport[i] <= now) nextReport[i] += reportEvery * r.uni(0.8, 1.2);
        }
      } else if (!liftSent[i]) {
        liftSent[i] = true;
        if (!liftLost[i]) lifted = true;
      }
    }
    if (newReport) {
      sdkDown = true;
      sdkReportMs = nowMs;
      sdkReleaseAt = nowMs + SDK_STALE_MS;
    } else if (lifted && sdkDown) {
      sdkDown = false;
    } else if (sdkDown && nowMs >= sdkReleaseAt) {
      sdkDown = false;  // the SDK's 3 s safety net for a lost lift-off
    }
    piccoinput::Sample s;
    s.contact = sdkDown;
    s.live = sdkDown;
    s.x = sdkX;
    s.y = sdkY;
    s.reportMs = sdkReportMs;
    const piccoinput::Result res = dec.feed(s, nowMs, /*keyboard=*/false, SCREEN_W);
    if (res.fired) out.fired.push_back(res);
  }
  return out;
}

struct Tally {
  int n = 0, correct = 0, wrong = 0, missed = 0;
  int wrongAs[16] = {};  // which button a wrong outcome fired first, by Button value
};

struct Report {
  Tally single[kIntents];
  Tally pairs;  // two quick taps: both must fire Right
  Tally total() const {
    Tally t;
    for (const Tally& x : single) {
      t.n += x.n;
      t.correct += x.correct;
      t.wrong += x.wrong;
      t.missed += x.missed;
    }
    return t;
  }
};

Report bench(const Controller& c, const unsigned seed, const int perIntent) {
  Rng r(seed);
  Report rep;
  for (int k = 0; k < kIntents; ++k) {
    const Intent intent = static_cast<Intent>(k);
    for (int i = 0; i < perIntent; ++i) {
      const Stroke s = makeStroke(r, intent);
      const Outcome o = play(r, c, {s}, {});
      Tally& t = rep.single[k];
      ++t.n;
      const Button want = expected(intent);
      bool wrong = false;
      bool right = false;
      int wrongButton = -1;
      for (const auto& f : o.fired) {
        if (f.button == want && !right) {
          right = true;
        } else {
          if (!wrong) wrongButton = static_cast<int>(f.button);
          wrong = true;
        }
      }
      if (wrong) {
        ++t.wrong;
        if (wrongButton >= 0 && wrongButton < 16) ++t.wrongAs[wrongButton];
      } else if (right || want == Button::Power) {  // Power: "nothing" was the right answer
        ++t.correct;
      } else {
        ++t.missed;
      }
    }
  }
  // Two taps 80-300 ms apart: the reading rhythm of "next, next".
  for (int i = 0; i < perIntent; ++i) {
    const Stroke a = makeStroke(r, Intent::Tap);
    const Stroke b = makeStroke(r, Intent::Tap);
    const Outcome o = play(r, c, {a, b}, {r.uni(80, 300)});
    ++rep.pairs.n;
    int rights = 0;
    bool other = false;
    for (const auto& f : o.fired) {
      if (f.button == Button::Right) {
        ++rights;
      } else {
        other = true;
      }
    }
    if (other || rights > 2) {
      ++rep.pairs.wrong;
    } else if (rights == 2) {
      ++rep.pairs.correct;
    } else {
      ++rep.pairs.missed;
    }
  }
  return rep;
}

const Controller kControllers[] = {
    {"clean 100 Hz", 100, 1.0, 0.0, 0.002, 0.0},
    {"noisy 60 Hz", 60, 3.0, 0.0, 0.004, 0.0},
    {"lossy 100 Hz (5% lift-offs lost)", 100, 1.5, 0.05, 0.002, 0.0},
    {"bad 60 Hz (15% lost, noisy)", 60, 3.0, 0.15, 0.006, 0.0},
    {"clean 100 Hz + 0.2%/report dropouts", 100, 1.0, 0.0, 0.002, 0.002},
    {"clean 100 Hz, no stalls", 100, 1.0, 0.0, 0.0, 0.0},
};

std::string pct(const int a, const int n) {
  char b[16];
  std::snprintf(b, sizeof b, "%5.1f%%", n ? 100.0 * a / n : 0.0);
  return b;
}

void print(const Controller& c, const Report& rep) {
  std::printf("\n== %s ==\n%-10s %7s %7s %7s\n", c.name, "intent", "right", "WRONG", "missed");
  for (int k = 0; k < kIntents; ++k) {
    const Tally& t = rep.single[k];
    std::printf("%-10s %s %s %s", intentName(static_cast<Intent>(k)), pct(t.correct, t.n).c_str(),
                pct(t.wrong, t.n).c_str(), pct(t.missed, t.n).c_str());
    const struct {
      Button b;
      const char* name;
    } names[] = {
        {Button::Right, "Right"}, {Button::Left, "Left"}, {Button::Back, "Back"}, {Button::Confirm, "Confirm"}};
    for (const auto& nm : names) {
      const int c = t.wrongAs[static_cast<int>(nm.b)];
      if (c) std::printf("  %s x%d", nm.name, c);
    }
    std::printf("\n");
  }
  std::printf("%-10s %s %s %s\n", "tap, tap", pct(rep.pairs.correct, rep.pairs.n).c_str(),
              pct(rep.pairs.wrong, rep.pairs.n).c_str(), pct(rep.pairs.missed, rep.pairs.n).c_str());
  const Tally t = rep.total();
  std::printf("%-10s %s %s %s\n", "ALL", pct(t.correct, t.n).c_str(), pct(t.wrong, t.n).c_str(),
              pct(t.missed, t.n).c_str());
}

constexpr int kPerIntent = 2000;

// Prints the table (ctest --output-on-failure, or run the binary) and holds the
// line on the numbers the decoder reaches today. Raise a floor when a change
// earns it; never lower one without saying why in the doc.
// Floors, in tenths of a percent, per controller: overall correct, overall
// WRONG (the presses that must fire nothing count here), and both taps of a
// quick pair landing. Set 2026-11-07 a little under the decoder after pass 5;
// docs/picco-gestures-2026-11-07.md has every table and the passes that were
// measured and rejected.
struct Floor {
  int correctX10, wrongMaxX10, pairsX10;
};
const Floor kFloors[] = {
    {982, 12, 965},  // clean 100 Hz
    {975, 15, 935},  // noisy 60 Hz
    {950, 11, 870},  // lossy 100 Hz
    {865, 17, 670},  // bad 60 Hz
    {953, 15, 965},  // clean + dropouts
    {990, 9, 995},   // clean, no stalls
};
static_assert(std::size(kFloors) == std::size(kControllers), "one floor per controller");

int x10(const int a, const int n) { return n ? (1000 * a) / n : 0; }

TEST(PiccoGestureBench, Scorecard) {
  for (size_t i = 0; i < std::size(kControllers); ++i) {
    const Controller& c = kControllers[i];
    const Report rep = bench(c, 20261107u, kPerIntent);
    print(c, rep);
    const Tally t = rep.total();
    EXPECT_GE(x10(t.correct, t.n), kFloors[i].correctX10) << c.name << ": overall correct";
    EXPECT_LE(x10(t.wrong, t.n), kFloors[i].wrongMaxX10) << c.name << ": overall WRONG";
    EXPECT_GE(x10(rep.pairs.correct, rep.pairs.n), kFloors[i].pairsX10) << c.name << ": tap, tap";
  }
}

}  // namespace
