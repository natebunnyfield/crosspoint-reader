#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "activities/reader/ProgressFile.h"

namespace {

constexpr const char* CACHE = "/.crosspoint/epub_1";
constexpr const char* FINAL = "/.crosspoint/epub_1/progress.bin";
constexpr const char* TMP = "/.crosspoint/epub_1/progress.bin.tmp";

std::vector<uint8_t> onCard() { return stubCard().files.at(FINAL); }

std::vector<uint8_t> record(size_t len, uint8_t seed) {
  std::vector<uint8_t> r(len);
  for (size_t i = 0; i < len; i++) r[i] = static_cast<uint8_t>(seed + i);
  return r;
}

class ProgressFile : public ::testing::Test {
 protected:
  void SetUp() override { stubCard().reset(); }
};

}  // namespace

TEST_F(ProgressFile, FirstSaveStagesThroughTheTemp) {
  const auto r = record(12, 1);
  ASSERT_TRUE(::ProgressFile::writeAtomic(CACHE, r.data(), r.size()));
  EXPECT_EQ(onCard(), r);
  EXPECT_EQ(stubCard().files.count(TMP), 0u);
  EXPECT_EQ(stubCard().renames, 1);
  EXPECT_EQ(stubCard().inPlaceWrites, 0);
}

// The page-turn case. Same record length as the save before it: the bytes go
// into the sector the file already owns and nothing else on the card moves.
TEST_F(ProgressFile, SameLengthRewritesInPlaceWithNoFatTraffic) {
  const auto first = record(12, 1);
  ASSERT_TRUE(::ProgressFile::writeAtomic(CACHE, first.data(), first.size()));
  const int renamesBefore = stubCard().renames;

  const auto second = record(12, 50);
  ASSERT_TRUE(::ProgressFile::writeAtomic(CACHE, second.data(), second.size()));
  EXPECT_EQ(onCard(), second);
  EXPECT_EQ(stubCard().inPlaceWrites, 1);
  EXPECT_EQ(stubCard().renames, renamesBefore) << "a page turn must not rename";
  EXPECT_EQ(stubCard().removes, 0) << "a page turn must not remove";
  EXPECT_EQ(stubCard().truncates, 0) << "a page turn must not truncate";
  EXPECT_EQ(stubCard().files.count(TMP), 0u) << "a page turn must not stage a temp";
}

// A different record length (a format upgrade, or a short file left by an
// older firmware) cannot be written in place: the staged path takes over, and
// the save after it is back in place at the new length.
TEST_F(ProgressFile, LengthChangeFallsBackToTheStagedWriteThenResumesInPlace) {
  const auto six = record(6, 1);
  ASSERT_TRUE(::ProgressFile::writeAtomic(CACHE, six.data(), six.size()));

  const auto twelve = record(12, 9);
  ASSERT_TRUE(::ProgressFile::writeAtomic(CACHE, twelve.data(), twelve.size()));
  EXPECT_EQ(onCard(), twelve);
  EXPECT_EQ(stubCard().removes, 1);
  EXPECT_EQ(stubCard().renames, 2);
  EXPECT_EQ(stubCard().inPlaceWrites, 0) << "the size probe must not write a byte";
  EXPECT_EQ(stubCard().files.count(TMP), 0u);

  const auto twelveAgain = record(12, 77);
  ASSERT_TRUE(::ProgressFile::writeAtomic(CACHE, twelveAgain.data(), twelveAgain.size()));
  EXPECT_EQ(onCard(), twelveAgain);
  EXPECT_EQ(stubCard().inPlaceWrites, 1);
  EXPECT_EQ(stubCard().renames, 2);
}

// Every record length the reader has ever written (docs/file-formats.md:
// 4, 6, 8, 12 bytes) takes the in-place path once a file of that length exists.
TEST_F(ProgressFile, EveryHistoricalRecordLengthRewritesInPlace) {
  for (const size_t len : {4u, 6u, 8u, 12u}) {
    stubCard().reset();
    const auto a = record(len, 1);
    const auto b = record(len, 2);
    ASSERT_TRUE(::ProgressFile::writeAtomic(CACHE, a.data(), a.size()));
    ASSERT_TRUE(::ProgressFile::writeAtomic(CACHE, b.data(), b.size()));
    EXPECT_EQ(onCard(), b) << "len " << len;
    EXPECT_EQ(stubCard().inPlaceWrites, 1) << "len " << len;
    EXPECT_EQ(stubCard().renames, 1) << "len " << len;
  }
}
