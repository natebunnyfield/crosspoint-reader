// The Update Library compare logic, and specifically the hashVerdict added on
// 2026-08-23 when the owner ruled that a book whose size and modification time
// have not moved must not be read again just to prove it.
//
// Both directions of this are silent on device. Skipping when it should hash
// leaves a stale book on the card forever and reports "unchanged" while doing
// it; hashing when it could skip is the whole cost the ruling was about, and
// looks exactly like the old behaviour. Neither shows up as an error.
#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "LibrarySyncPlan.h"

namespace {

constexpr const char* kShaA = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
constexpr const char* kShaB = "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";

librarysync::CardStamp card(const size_t bytes = 1000, const uint16_t date = 0x5901, const uint16_t time = 0x4321) {
  librarysync::CardStamp stamp;
  stamp.bytes = bytes;
  stamp.fatDate = date;
  stamp.fatTime = time;
  stamp.haveMtime = true;
  return stamp;
}

librarysync::SyncRecord recordMatching(const librarysync::CardStamp& stamp, const char* sha = kShaA) {
  librarysync::SyncRecord record;
  record.present = true;
  record.bytes = stamp.bytes;
  record.fatDate = stamp.fatDate;
  record.fatTime = stamp.fatTime;
  record.sha = sha;
  return record;
}

}  // namespace

TEST(LibrarySyncPlan, SkipsTheHashWhenSizeAndMtimeBothMatchARecordTheManifestStillWants) {
  const auto stamp = card();
  EXPECT_EQ(librarysync::hashVerdict(stamp, recordMatching(stamp), kShaA), librarysync::HashVerdict::SKIP_HASH);
}

TEST(LibrarySyncPlan, TheFirstRunAfterThisShippedHasNoRecordsAndMustHashEverything) {
  // THE failure that would look like a very fast, very successful sync: an
  // absent record read as "nothing to do". Every book on the card would be
  // declared unchanged forever, and no error would ever be printed.
  const auto stamp = card();
  librarysync::SyncRecord none;  // present == false
  EXPECT_EQ(librarysync::hashVerdict(stamp, none, kShaA), librarysync::HashVerdict::MUST_HASH);
}

TEST(LibrarySyncPlan, NoModificationTimeMeansHash) {
  // A HAL or a filesystem that cannot answer is not a filesystem where nothing
  // has changed. getModifyDateTime returns false on a card without timestamps
  // and on any port that has not implemented it.
  auto stamp = card();
  stamp.haveMtime = false;
  EXPECT_EQ(librarysync::hashVerdict(stamp, recordMatching(card()), kShaA), librarysync::HashVerdict::MUST_HASH);
}

TEST(LibrarySyncPlan, ADifferentModificationTimeMeansHash) {
  const auto stamp = card();
  auto stale = recordMatching(stamp);
  stale.fatTime = static_cast<uint16_t>(stamp.fatTime + 1);
  EXPECT_EQ(librarysync::hashVerdict(stamp, stale, kShaA), librarysync::HashVerdict::MUST_HASH);

  stale = recordMatching(stamp);
  stale.fatDate = static_cast<uint16_t>(stamp.fatDate + 1);
  EXPECT_EQ(librarysync::hashVerdict(stamp, stale, kShaA), librarysync::HashVerdict::MUST_HASH);
}

TEST(LibrarySyncPlan, ADifferentSizeMeansHashEvenWhenTheMtimeMatches) {
  // sizeVerdict already catches this before hashVerdict is asked, but the two
  // are separately reachable and a record whose size disagrees with the card is
  // a record about a different file.
  const auto stamp = card();
  auto stale = recordMatching(stamp);
  stale.bytes = stamp.bytes + 1;
  EXPECT_EQ(librarysync::hashVerdict(stamp, stale, kShaA), librarysync::HashVerdict::MUST_HASH);
}

TEST(LibrarySyncPlan, AManifestAskingForDifferentBytesMeansHash) {
  // A new edition of the same book at the same size. The record is evidence
  // that the card holds kShaA; it is not permission to skip reading the file
  // and decide a download from a stored number.
  const auto stamp = card();
  EXPECT_EQ(librarysync::hashVerdict(stamp, recordMatching(stamp, kShaA), kShaB), librarysync::HashVerdict::MUST_HASH);
}

TEST(LibrarySyncPlan, ARecordWithNoDigestNeverSkips) {
  const auto stamp = card();
  auto broken = recordMatching(stamp);
  broken.sha = nullptr;
  EXPECT_EQ(librarysync::hashVerdict(stamp, broken, kShaA), librarysync::HashVerdict::MUST_HASH);
  broken.sha = "";
  EXPECT_EQ(librarysync::hashVerdict(stamp, broken, kShaA), librarysync::HashVerdict::MUST_HASH);
}

TEST(LibrarySyncPlan, DigestCaseDoesNotForceAReRead) {
  // shaMatches is deliberately case-insensitive; a ledger written by one tool
  // and a manifest written by another must not disagree about hex case and
  // re-read the whole library every run.
  const auto stamp = card();
  std::string upper(kShaA);
  for (auto& c : upper) c = static_cast<char>(::toupper(c));
  EXPECT_EQ(librarysync::hashVerdict(stamp, recordMatching(stamp, kShaA), upper.c_str()),
            librarysync::HashVerdict::SKIP_HASH);
}

TEST(LibrarySyncPlan, ZeroIsALegitimateModificationTime) {
  // A card written by a device with no clock stamps every file 0/0. That is a
  // real, comparable value -- treating it as "unknown" would mean the skip
  // never fires on exactly the devices this feature was for.
  auto stamp = card(1000, 0, 0);
  EXPECT_EQ(librarysync::hashVerdict(stamp, recordMatching(stamp), kShaA), librarysync::HashVerdict::SKIP_HASH);
}

TEST(LibrarySyncPlan, SizeVerdictStillDecidesFirst) {
  EXPECT_EQ(librarysync::sizeVerdict(false, 0, 100), librarysync::SizeVerdict::DOWNLOAD);
  EXPECT_EQ(librarysync::sizeVerdict(true, 99, 100), librarysync::SizeVerdict::DOWNLOAD);
  EXPECT_EQ(librarysync::sizeVerdict(true, 100, 100), librarysync::SizeVerdict::CHECK_SHA);
}
