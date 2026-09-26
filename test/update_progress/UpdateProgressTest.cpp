// What Update Fonts and Update Library show while they work, and when they
// repaint. Owner bug 2026-09-26: "not appearing frozen when i select Update
// Library and Update Fonts" -- docs/update-progress-2026-09-26.md.
//
// The failure these guard is SILENCE, which no crash and no log line reports:
// a repaint rule that drops a phase change leaves the panel naming a file the
// run finished long ago, and one that repaints on every callback flashes an
// e-ink panel dozens of times a second. Both look fine in a code review.
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "UpdateProgress.h"

using updprogress::Next;
using updprogress::Phase;
using updprogress::Snapshot;

namespace {

Snapshot at(Phase phase, uint32_t item, uint32_t file, uint64_t bytes, uint32_t sec) {
  Snapshot s;
  s.state = 3;
  s.phase = static_cast<uint8_t>(phase);
  s.item = item;
  s.file = file;
  s.bytes = bytes;
  s.elapsedSec = sec;
  return s;
}

struct File {
  size_t bytes;
};

}  // namespace

// --- the repaint rule ---------------------------------------------------------

TEST(UpdateProgressRepaint, TheFirstFrameIsAlwaysPainted) {
  EXPECT_TRUE(updprogress::shouldRepaint(false, Snapshot{}, at(Phase::PREPARING, 0, 0, 0, 0), 0));
}

TEST(UpdateProgressRepaint, NothingVisibleChangedMeansNoRefreshHoweverLongItHasBeen) {
  const Snapshot s = at(Phase::DOWNLOADING, 1, 2, 5000, 7);
  EXPECT_FALSE(updprogress::shouldRepaint(true, s, s, 60000));
}

// A slow download calls back on every chunk. Bytes alone repaint at most once a
// second, which is the e-ink budget -- never per callback.
TEST(UpdateProgressRepaint, BytesRepaintAtMostOncePerSecond) {
  const Snapshot shown = at(Phase::DOWNLOADING, 1, 2, 5000, 7);
  const Snapshot now = at(Phase::DOWNLOADING, 1, 2, 9000, 7);
  EXPECT_FALSE(updprogress::shouldRepaint(true, shown, now, 0));
  EXPECT_FALSE(updprogress::shouldRepaint(true, shown, now, updprogress::MIN_REPAINT_INTERVAL_MS - 1));
  EXPECT_TRUE(updprogress::shouldRepaint(true, shown, now, updprogress::MIN_REPAINT_INTERVAL_MS));
}

// The heartbeat: a single file's bytes can sit still (a host fetch delivers a
// body whole; a stalled link delivers nothing), and the clock is then the only
// thing that says the reader is alive. It must repaint on its own.
TEST(UpdateProgressRepaint, TheClockAloneRepaintsOncePerSecond) {
  const Snapshot shown = at(Phase::DOWNLOADING, 1, 2, 5000, 7);
  const Snapshot now = at(Phase::DOWNLOADING, 1, 2, 5000, 8);
  EXPECT_FALSE(updprogress::shouldRepaint(true, shown, now, 500));
  EXPECT_TRUE(updprogress::shouldRepaint(true, shown, now, 1000));
}

// A phase, file or item change says something new, so it does not wait the full
// second -- but it still waits a quarter, so thirteen unchanged families walked
// in milliseconds are not thirteen panel refreshes.
TEST(UpdateProgressRepaint, StructuralChangesRepaintSoonerButNotInstantly) {
  const Snapshot shown = at(Phase::CHECKING, 1, 2, 5000, 7);
  for (const Snapshot& now :
       {at(Phase::DOWNLOADING, 1, 2, 5000, 7), at(Phase::CHECKING, 1, 3, 0, 7), at(Phase::PREPARING, 2, 0, 0, 7)}) {
    EXPECT_FALSE(updprogress::shouldRepaint(true, shown, now, updprogress::MIN_STRUCTURAL_REPAINT_MS - 1));
    EXPECT_TRUE(updprogress::shouldRepaint(true, shown, now, updprogress::MIN_STRUCTURAL_REPAINT_MS));
  }
}

// Back is acknowledged on screen promptly -- the press must be SEEN to land,
// even though it acts only between items.
TEST(UpdateProgressRepaint, APressedBackIsAStructuralChange) {
  const Snapshot shown = at(Phase::DOWNLOADING, 1, 2, 5000, 7);
  Snapshot now = shown;
  now.stopping = true;
  EXPECT_TRUE(updprogress::structuralChange(shown, now));
  EXPECT_TRUE(updprogress::shouldRepaint(true, shown, now, updprogress::MIN_STRUCTURAL_REPAINT_MS));
}

// A change the throttle holds back is not lost: the caller keeps the snapshot it
// last PAINTED, so the difference is still there for the next callback or tick.
// This is the property that lets the rule drop requests at all.
TEST(UpdateProgressRepaint, AHeldBackChangeIsPaintedByALaterCaller) {
  const Snapshot shown = at(Phase::CHECKING, 1, 5, 900, 7);
  const Snapshot next = at(Phase::DOWNLOADING, 1, 0, 0, 7);
  EXPECT_FALSE(updprogress::shouldRepaint(true, shown, next, 10));
  // Nothing moved since, but time passed: the pending change still paints.
  EXPECT_TRUE(updprogress::shouldRepaint(true, shown, next, 300));
}

// Simulated run: a callback every 5 ms for 10 s of downloading. The rule must
// produce about one refresh per second -- not zero (the reported bug) and not
// hundreds (a flashing panel).
TEST(UpdateProgressRepaint, ATenSecondDownloadRefreshesAboutTenTimes) {
  Snapshot shown;
  bool hasShown = false;
  uint32_t lastPaint = 0;
  int paints = 0;
  for (uint32_t t = 0; t <= 10000; t += 5) {
    const Snapshot now = at(Phase::DOWNLOADING, 0, t / 2000, t * 100, t / 1000);
    if (updprogress::shouldRepaint(hasShown, shown, now, t - lastPaint)) {
      shown = now;
      hasShown = true;
      lastPaint = t;
      ++paints;
    }
  }
  EXPECT_GE(paints, 10);
  EXPECT_LE(paints, 16);  // ten seconds, plus the five file changes at 250 ms
}

// --- between items -------------------------------------------------------------

TEST(UpdateProgressNext, RunsEachItemThenFinishes) {
  EXPECT_EQ(updprogress::nextStep(false, 0, 3), Next::RUN_ITEM);
  EXPECT_EQ(updprogress::nextStep(false, 2, 3), Next::RUN_ITEM);
  EXPECT_EQ(updprogress::nextStep(false, 3, 3), Next::FINISH);
  EXPECT_EQ(updprogress::nextStep(false, 0, 0), Next::FINISH);
}

// A stop requested at ANY point stops -- including during the last item, where
// FINISH would otherwise run the font mirror's removals. The owner ruled that a
// reader who stopped the run did not ask for a mirror (2026-09-07).
TEST(UpdateProgressNext, AStopNeverFinishesSoACanceledRunNeverRemoves) {
  EXPECT_EQ(updprogress::nextStep(true, 0, 3), Next::STOP);
  EXPECT_EQ(updprogress::nextStep(true, 2, 3), Next::STOP);
  EXPECT_EQ(updprogress::nextStep(true, 3, 3), Next::STOP);
}

// --- the numbers on the line ----------------------------------------------------

TEST(UpdateProgressBytes, AFamilysBytesCountFinishedFilesWholeAndTheCurrentOneSoFar) {
  const std::vector<File> files = {{1000}, {2000}, {3000}};
  EXPECT_EQ(updprogress::bytesTotal(files), 6000u);
  EXPECT_EQ(updprogress::bytesDone(files, 0, 0), 0u);
  EXPECT_EQ(updprogress::bytesDone(files, 1, 500), 1500u);
  EXPECT_EQ(updprogress::bytesDone(files, 2, 3000), 6000u);
}

// A server that sends more than the manifest promised must not run the line past
// its own total ("7.4 of 6.8 MB").
TEST(UpdateProgressBytes, TheCurrentFileIsClampedToItsDeclaredSize) {
  const std::vector<File> files = {{1000}, {2000}};
  EXPECT_EQ(updprogress::bytesDone(files, 1, 999999), 3000u);
  EXPECT_EQ(updprogress::bytesDone(files, 7, 5), 3000u);  // an index past the end counts every file
}

TEST(UpdateProgressFormat, MegabytesAreDecimalAndTruncated) {
  char out[16];
  updprogress::formatMb(0, out, sizeof(out));
  EXPECT_STREQ(out, "0.0");
  updprogress::formatMb(1234567, out, sizeof(out));
  EXPECT_STREQ(out, "1.2");
  // Truncated, never rounded up: 6.79 MB still arriving must not read "6.8 of 6.8".
  updprogress::formatMb(6799999, out, sizeof(out));
  EXPECT_STREQ(out, "6.7");
  updprogress::formatMb(9999999, out, sizeof(out));
  EXPECT_STREQ(out, "9.9");
  updprogress::formatMb(10000000, out, sizeof(out));
  EXPECT_STREQ(out, "10");
  updprogress::formatMb(80400000, out, sizeof(out));
  EXPECT_STREQ(out, "80");
}

TEST(UpdateProgressFormat, ElapsedIsMinutesAndSecondsThenHours) {
  char out[16];
  updprogress::formatElapsed(0, out, sizeof(out));
  EXPECT_STREQ(out, "0:00");
  updprogress::formatElapsed(7, out, sizeof(out));
  EXPECT_STREQ(out, "0:07");
  updprogress::formatElapsed(723, out, sizeof(out));
  EXPECT_STREQ(out, "12:03");
  updprogress::formatElapsed(3723, out, sizeof(out));
  EXPECT_STREQ(out, "1:02:03");
}

TEST(UpdateProgressFormat, AFontFileLosesItsExtensionAndNothingElse) {
  char out[40];
  updprogress::stemOf("Edgar_12.cpfont", out, sizeof(out));
  EXPECT_STREQ(out, "Edgar_12");
  updprogress::stemOf("NoExtension", out, sizeof(out));
  EXPECT_STREQ(out, "NoExtension");
  updprogress::stemOf(".hidden", out, sizeof(out));
  EXPECT_STREQ(out, ".hidden");
  char tiny[5];
  updprogress::stemOf("Coelacanth_11.cpfont", tiny, sizeof(tiny));
  EXPECT_STREQ(tiny, "Coel");
}
