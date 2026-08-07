#include <gtest/gtest.h>

#include <set>
#include <string>

#include "notes/NoteNaming.h"

TEST(NoteNaming, StampIsZeroPaddedYYYYMMDDHHmmss) {
  EXPECT_EQ(notenaming::formatStamp(2026, 8, 6, 9, 5, 3), "20260806090503");
  EXPECT_EQ(notenaming::formatStamp(2026, 12, 31, 23, 59, 59), "20261231235959");
}

TEST(NoteNaming, PathAtRootHasNoDoubleSlash) {
  auto none = [](const char*) { return false; };
  EXPECT_EQ(notenaming::uniqueNotePath("/", "20260806090503", none), "/20260806090503.md");
}

TEST(NoteNaming, PathInSubdirectory) {
  auto none = [](const char*) { return false; };
  EXPECT_EQ(notenaming::uniqueNotePath("/notes", "20260806090503", none), "/notes/20260806090503.md");
}

// Create Note must never reopen an existing note. Two taps in the same second
// have to produce two files.
TEST(NoteNaming, CollisionGetsASuffix) {
  std::set<std::string> present{"/20260806090503.md"};
  auto exists = [&](const char* p) { return present.count(p) > 0; };
  EXPECT_EQ(notenaming::uniqueNotePath("/", "20260806090503", exists), "/20260806090503-1.md");
}

TEST(NoteNaming, RepeatedCollisionsKeepCounting) {
  std::set<std::string> present{"/s.md", "/s-1.md", "/s-2.md"};
  auto exists = [&](const char* p) { return present.count(p) > 0; };
  EXPECT_EQ(notenaming::uniqueNotePath("/", "s", exists), "/s-3.md");
}

// A device whose clock never synced stamps every note identically; each run
// must still get its own file rather than silently opening the previous note.
TEST(NoteNaming, UnsyncedClockStillYieldsDistinctNames) {
  std::set<std::string> present;
  auto exists = [&](const char* p) { return present.count(p) > 0; };
  std::set<std::string> made;
  for (int i = 0; i < 5; ++i) {
    const std::string p = notenaming::uniqueNotePath("/", "19700101000000", exists);
    EXPECT_TRUE(made.insert(p).second) << "duplicate note path: " << p;
    present.insert(p);
  }
  EXPECT_EQ(made.size(), 5u);
}
