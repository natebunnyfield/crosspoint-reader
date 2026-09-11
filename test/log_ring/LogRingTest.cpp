// The crash log ring's duplicate collapsing.
//
// HalSystem::getPanicInfo dumps this ring verbatim into the crash report, so
// the ring IS the report. It holds sixteen lines. The B-040 report -- a crash
// the owner hits in ordinary daily use -- is THIRTEEN identical
// "buildAdvanceTable: failed to allocate codepoint buffer (16384 bytes)" lines,
// which pushed every line that led up to the failure out of the buffer. The one
// piece of evidence for a recurring crash explained nothing about how the
// device got there.
//
// A retry storm is exactly when the preceding context matters most and exactly
// when the old ring threw it away. These tests pin the fix.

#include <gtest/gtest.h>

#include <string>

#include "Logging.h"

// The ring is fed through logPrintf, the real path every LOG_* macro takes.
// addToLogRingBuffer is internal to Logging.cpp and stays that way.

namespace {

class LogRing : public ::testing::Test {
 protected:
  void SetUp() override { clearLastLogs(); }
};

TEST_F(LogRing, IdenticalLinesCollapseIntoOneSlot) {
  logPrintf("ERR", "SDCF", "alloc failed");
  for (int i = 0; i < 12; i++) logPrintf("ERR", "SDCF", "alloc failed");

  const std::string logs = getLastLogs();
  // One slot, not thirteen, and the count is visible.
  EXPECT_NE(std::string::npos, logs.find("(x13)")) << logs;
  // Exactly one line mentioning the failure.
  size_t occurrences = 0;
  for (size_t at = logs.find("alloc failed"); at != std::string::npos; at = logs.find("alloc failed", at + 1)) {
    occurrences++;
  }
  EXPECT_EQ(1u, occurrences) << logs;
}

// THE POINT OF THE WHOLE CHANGE: context that precedes a storm must survive it.
TEST_F(LogRing, AStormDoesNotEvictWhatCameBeforeIt) {
  logPrintf("INF", "READER", "opened chapter 7");
  logPrintf("INF", "SCT", "paginating");
  for (int i = 0; i < 50; i++) logPrintf("ERR", "SDCF", "alloc failed");

  const std::string logs = getLastLogs();
  EXPECT_NE(std::string::npos, logs.find("opened chapter 7")) << logs;
  EXPECT_NE(std::string::npos, logs.find("paginating")) << logs;
  EXPECT_NE(std::string::npos, logs.find("(x50)")) << logs;
}

// The timestamp differs on every repeat, so comparing whole lines would never
// match and nothing would ever collapse.
TEST_F(LogRing, DifferingTimestampsStillCountAsRepeats) {
  logPrintf("ERR", "X", "same text");
  logPrintf("ERR", "X", "same text");
  const std::string logs = getLastLogs();
  EXPECT_NE(std::string::npos, logs.find("(x2)")) << logs;
}

TEST_F(LogRing, DifferentLinesDoNotCollapse) {
  logPrintf("ERR", "X", "first");
  logPrintf("ERR", "X", "second");
  const std::string logs = getLastLogs();
  EXPECT_NE(std::string::npos, logs.find("first")) << logs;
  EXPECT_NE(std::string::npos, logs.find("second")) << logs;
  EXPECT_EQ(std::string::npos, logs.find("(x")) << logs;
}

// An alternating pair must not collapse -- only CONSECUTIVE duplicates do.
TEST_F(LogRing, AlternatingLinesAreBothKept) {
  for (int i = 0; i < 4; i++) {
    logPrintf("ERR", "X", "a");
    logPrintf("ERR", "X", "b");
  }
  const std::string logs = getLastLogs();
  EXPECT_EQ(std::string::npos, logs.find("(x")) << logs;
}

TEST_F(LogRing, TheRingStillWrapsWhenLinesDiffer) {
  for (int i = 0; i < 40; i++) {
    logPrintf("INF", "X", "line %d", i);
  }
  const std::string logs = getLastLogs();
  EXPECT_EQ(std::string::npos, logs.find("line 0")) << logs;
  EXPECT_NE(std::string::npos, logs.find("line 39")) << logs;
}

}  // namespace
