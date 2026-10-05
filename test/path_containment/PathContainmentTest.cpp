#include <gtest/gtest.h>

#include "FsHelpers.h"

using FsHelpers::isSameOrInside;

TEST(PathContainment, DescendantsAreInside) {
  EXPECT_TRUE(isSameOrInside("/a/b", "/a"));
  EXPECT_TRUE(isSameOrInside("/a/b/c/a", "/a"));
  EXPECT_TRUE(isSameOrInside("/a", "/a"));
}

TEST(PathContainment, SiblingsWithACommonPrefixAreNot) {
  EXPECT_FALSE(isSameOrInside("/ab", "/a"));
  EXPECT_FALSE(isSameOrInside("/a-b/c", "/a"));
  EXPECT_FALSE(isSameOrInside("/b", "/a"));
  EXPECT_FALSE(isSameOrInside("/a", "/a/b"));
}

TEST(PathContainment, CaseInsensitiveLikeFat) {
  EXPECT_TRUE(isSameOrInside("/BOOKS/new", "/books"));
  EXPECT_TRUE(isSameOrInside("/Books", "/books"));
}

TEST(PathContainment, TrailingSlashesIgnored) {
  EXPECT_TRUE(isSameOrInside("/a/b/", "/a/"));
  EXPECT_TRUE(isSameOrInside("/a/", "/a"));
  EXPECT_FALSE(isSameOrInside("/ab/", "/a/"));
}

TEST(PathContainment, RootContainsEverything) {
  EXPECT_TRUE(isSameOrInside("/x", "/"));
  EXPECT_TRUE(isSameOrInside("/", "/"));
}

// The card folds long names with SdFat's Unicode table, not just ASCII, so a
// guard that only folded ASCII let MOVE /Café -> /CAFÉ/x through and SdFat
// built the directory loop anyway (adversarial review, 2026-10-04).
TEST(PathContainment, FoldsNonAsciiLikeSdFat) {
  EXPECT_TRUE(isSameOrInside("/CAF\xC3\x89/x", "/Caf\xC3\xA9"));  // É vs é
  EXPECT_TRUE(isSameOrInside("/\xD0\x9A\xD0\x9D\xD0\x98\xD0\x93\xD0\x98/a",
                             "/\xD0\xBA\xD0\xBD\xD0\xB8\xD0\xB3\xD0\xB8"));  // КНИГИ vs книги
  EXPECT_TRUE(isSameOrInside("/\xCE\xA3/b", "/\xCF\x83"));                   // Σ vs σ
  EXPECT_FALSE(isSameOrInside("/Cafe/x", "/Caf\xC3\xA9"));                   // e is not é
}

TEST(PathContainment, SameFatPath) {
  EXPECT_TRUE(FsHelpers::isSameFatPath("/Caf\xC3\xA9.txt", "/CAF\xC3\x89.TXT"));
  EXPECT_TRUE(FsHelpers::isSameFatPath("/a/", "/A"));
  EXPECT_FALSE(FsHelpers::isSameFatPath("/a/b", "/a"));
  EXPECT_FALSE(FsHelpers::isSameFatPath("/a", "/a/b"));
}

TEST(PathContainment, MalformedUtf8ComparesExactly) {
  EXPECT_TRUE(isSameOrInside("/\xFF/x", "/\xFF"));
  EXPECT_FALSE(isSameOrInside("/\xFE/x", "/\xFF"));
  EXPECT_FALSE(isSameOrInside("/\xC3", "/\xC3\xA9"));  // truncated sequence
}
