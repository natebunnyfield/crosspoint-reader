// The Update Fonts compare logic, and above all the ALL-OR-NOTHING family
// guarantee.
//
// Device-side font download existed once and was deleted on 2026-08-10
// (c3c8d1268, "feat(fonts): remove device-initiated font download entirely").
// The owner's ruling that day was "fonts cannot be installed fully from device
// downloading them. remove it fully from repo," and release-fonts.yml still
// carries the reason in its header: "it could never install a family
// completely."
//
// A family is six files. Five of them is not a smaller font — it is a font that
// silently has no 14 pt, because SdCardFontRegistry discovers whatever .cpfont
// files happen to be in the directory and offers the family regardless
// (SdCardFontRegistry.cpp:158-207). Nothing anywhere prints an error.
//
// So the tests that matter here are the ones that fail if anybody ever makes
// the "obvious" optimization of downloading only the files that changed, or
// relaxes the commit gate to "close enough".
#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "FontSyncPlan.h"

namespace {

constexpr const char* kShaA = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
constexpr const char* kShaB = "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";

// Every installed family ships six cuts since the owner's 2026-08-26 ruling
// ("cut XS and XXS versions of every s tiers shipping font"); the older
// buildable-only recipes ship four. Both are exercised below.
constexpr size_t kSixCuts = 6;
constexpr size_t kFourCuts = 4;

}  // namespace

// ---------------------------------------------------------------------------
// familyVerdict: one missing cut installs the WHOLE family
// ---------------------------------------------------------------------------

TEST(FontSyncPlan, AFamilyWhereEveryFileMatchesIsUntouched) {
  EXPECT_EQ(fontsync::familyVerdict(kSixCuts, kSixCuts), fontsync::FamilyVerdict::UNCHANGED);
  EXPECT_EQ(fontsync::familyVerdict(kFourCuts, kFourCuts), fontsync::FamilyVerdict::UNCHANGED);
}

// THE TEST. Five of six files already correct is the case where downloading
// only the sixth is cheapest, obviously right, and wrong: it makes "five cuts
// from build A plus one from build B" reachable, and the six cuts of a family
// are built together from one set of outlines with one set of metrics. If this
// ever returns UNCHANGED or gains a PARTIAL sibling, the 2026-08-10 bug is back.
TEST(FontSyncPlan, OneMissingCutInstallsTheWholeFamilyNotJustTheMissingCut) {
  EXPECT_EQ(fontsync::familyVerdict(kSixCuts, 5), fontsync::FamilyVerdict::INSTALL_ALL);
  EXPECT_EQ(fontsync::familyVerdict(kSixCuts, 3), fontsync::FamilyVerdict::INSTALL_ALL);
  EXPECT_EQ(fontsync::familyVerdict(kSixCuts, 0), fontsync::FamilyVerdict::INSTALL_ALL);
  EXPECT_EQ(fontsync::familyVerdict(kFourCuts, 3), fontsync::FamilyVerdict::INSTALL_ALL);
}

TEST(FontSyncPlan, AFamilyWithNoFilesIsNeverReportedAsAlreadyCurrent) {
  // A manifest entry with an empty file list must not read as "nothing to do":
  // that is a successful no-op on screen over a family that is not installed.
  EXPECT_EQ(fontsync::familyVerdict(0, 0), fontsync::FamilyVerdict::INSTALL_ALL);
}

// ---------------------------------------------------------------------------
// commitVerdict: the staged copy moves into place only when it is complete
// ---------------------------------------------------------------------------

TEST(FontSyncPlan, CommitsOnlyWhenEveryFileWasStagedAndVerified) {
  EXPECT_EQ(fontsync::commitVerdict(kSixCuts, kSixCuts, /*anyFailure=*/false), fontsync::CommitVerdict::COMMIT);
}

// The other half of the guarantee. The staging directory is discarded and the
// installed family is left exactly as it was — which is why a failed run costs
// the owner nothing rather than costing him the font he was reading.
TEST(FontSyncPlan, AnyShortfallDiscardsTheWholeStagedFamily) {
  EXPECT_EQ(fontsync::commitVerdict(kSixCuts, 5, false), fontsync::CommitVerdict::DISCARD);
  EXPECT_EQ(fontsync::commitVerdict(kSixCuts, 1, false), fontsync::CommitVerdict::DISCARD);
  EXPECT_EQ(fontsync::commitVerdict(kSixCuts, 0, false), fontsync::CommitVerdict::DISCARD);
}

TEST(FontSyncPlan, AFailureAnywhereDiscardsEvenWhenTheCountLooksComplete) {
  // Belt and braces on purpose: a count that reached the total after something
  // went wrong is a count that cannot be trusted to mean what it says.
  EXPECT_EQ(fontsync::commitVerdict(kSixCuts, kSixCuts, /*anyFailure=*/true), fontsync::CommitVerdict::DISCARD);
}

TEST(FontSyncPlan, MoreVerifiedFilesThanTheManifestListsIsAlsoRefused) {
  // Not "at least all of them". A staged directory holding a file the manifest
  // does not list is a directory this run does not understand.
  EXPECT_EQ(fontsync::commitVerdict(kSixCuts, kSixCuts + 1, false), fontsync::CommitVerdict::DISCARD);
}

TEST(FontSyncPlan, AnEmptyStagedFamilyNeverCommits) {
  EXPECT_EQ(fontsync::commitVerdict(0, 0, false), fontsync::CommitVerdict::DISCARD);
}

// ---------------------------------------------------------------------------
// fileVerdict, and the hash ledger borrowed from Update Library
// ---------------------------------------------------------------------------

TEST(FontSyncPlan, SizeDecidesBeforeTheDigest) {
  EXPECT_EQ(fontsync::fileVerdict(false, 0, 100), fontsync::FileVerdict::DOWNLOAD);
  EXPECT_EQ(fontsync::fileVerdict(true, 99, 100), fontsync::FileVerdict::DOWNLOAD);
  EXPECT_EQ(fontsync::fileVerdict(true, 100, 100), fontsync::FileVerdict::CHECK_SHA);
}

TEST(FontSyncPlan, TheLedgerSkipsTheHashOnlyWhenSizeAndMtimeBothMatch) {
  // Fonts reuse librarysync::hashVerdict rather than hashing ~80 MB of .cpfont
  // off the card on every run. Same contract: every way of NOT knowing hashes.
  fontsync::CardStamp stamp;
  stamp.bytes = 1000;
  stamp.fatDate = 0x5901;
  stamp.fatTime = 0x4321;
  stamp.haveMtime = true;

  fontsync::SyncRecord record;
  record.present = true;
  record.bytes = stamp.bytes;
  record.fatDate = stamp.fatDate;
  record.fatTime = stamp.fatTime;
  record.sha = kShaA;
  EXPECT_EQ(fontsync::hashVerdict(stamp, record, kShaA), fontsync::HashVerdict::SKIP_HASH);

  fontsync::SyncRecord none;
  EXPECT_EQ(fontsync::hashVerdict(stamp, none, kShaA), fontsync::HashVerdict::MUST_HASH);

  EXPECT_EQ(fontsync::hashVerdict(stamp, record, kShaB), fontsync::HashVerdict::MUST_HASH);

  auto noMtime = stamp;
  noMtime.haveMtime = false;
  EXPECT_EQ(fontsync::hashVerdict(noMtime, record, kShaA), fontsync::HashVerdict::MUST_HASH);
}

// ---------------------------------------------------------------------------
// Names the manifest is allowed to put on the card
// ---------------------------------------------------------------------------

TEST(FontSyncPlan, FamilyNamesAreAlphanumericHyphenUnderscoreOnly) {
  EXPECT_TRUE(fontsync::isSafeFamilyName("Doves"));
  EXPECT_TRUE(fontsync::isSafeFamilyName("TeXGyreSchola"));
  EXPECT_TRUE(fontsync::isSafeFamilyName("Bookerly-SD"));
  EXPECT_TRUE(fontsync::isSafeFamilyName("iA_Writer"));

  EXPECT_FALSE(fontsync::isSafeFamilyName(nullptr));
  EXPECT_FALSE(fontsync::isSafeFamilyName(""));
  EXPECT_FALSE(fontsync::isSafeFamilyName(".."));
  EXPECT_FALSE(fontsync::isSafeFamilyName("../books"));
  EXPECT_FALSE(fontsync::isSafeFamilyName("a/b"));
  EXPECT_FALSE(fontsync::isSafeFamilyName("a\\b"));
  // A leading dot is what this feature's OWN staging directories use to hide
  // from SdCardFontRegistry::scanRoot. A manifest must never be able to name one.
  EXPECT_FALSE(fontsync::isSafeFamilyName(".Doves.part"));
  EXPECT_FALSE(fontsync::isSafeFamilyName("Doves.old"));
}

TEST(FontSyncPlan, AFileMustBeNamedTheWayDiscoveryParsesIt) {
  EXPECT_TRUE(fontsync::isSafeFontFileName("Doves", "Doves_8.cpfont"));
  EXPECT_TRUE(fontsync::isSafeFontFileName("Doves", "Doves_18.cpfont"));
  EXPECT_TRUE(fontsync::isSafeFontFileName("Bookerly-SD", "Bookerly-SD_14.cpfont"));

  // The silent one: a well-formed, verifiable, wrongly-named file would be
  // written and counted toward the commit gate, then ignored by discovery —
  // a family that "installed completely" and is missing a size on screen.
  EXPECT_FALSE(fontsync::isSafeFontFileName("Doves", "Doves-8.cpfont"));
  EXPECT_FALSE(fontsync::isSafeFontFileName("Doves", "Doves_8.cpfont.tmp"));
  EXPECT_FALSE(fontsync::isSafeFontFileName("Doves", "Doves_.cpfont"));
  EXPECT_FALSE(fontsync::isSafeFontFileName("Doves", "Doves_8.ttf"));

  // The size must be one SdCardFontRegistry::parseFilename accepts (1..255,
  // SdCardFontRegistry.cpp:99). Outside that range the file would download,
  // verify and then be invisible to discovery -- the same silent hole as a
  // wrong separator, reached through the digits.
  EXPECT_FALSE(fontsync::isSafeFontFileName("Doves", "Doves_0.cpfont"));
  EXPECT_FALSE(fontsync::isSafeFontFileName("Doves", "Doves_000.cpfont"));
  EXPECT_FALSE(fontsync::isSafeFontFileName("Doves", "Doves_256.cpfont"));

  // LEADING ZEROS, which the divergence test below CANNOT catch because they
  // are not a divergence: parseFilename accepts "Doves_08.cpfont" perfectly
  // well, at size 8 -- the same size as "Doves_8.cpfont". Two manifest entries,
  // two files written and verified, two counted toward the commit gate, and
  // ONE cut on disk once scanDirectory drops the duplicate
  // (SdCardFontRegistry.cpp:138-148). Refusing the leading zero here is the
  // belt; FontUpdater's per-family duplicate-size check is the braces, because
  // a validator that sees one name at a time cannot see a collision.
  EXPECT_FALSE(fontsync::isSafeFontFileName("Doves", "Doves_08.cpfont"));
  EXPECT_FALSE(fontsync::isSafeFontFileName("Doves", "Doves_018.cpfont"));
  EXPECT_FALSE(fontsync::isSafeFontFileName("Doves", "Doves_0255.cpfont"));
  EXPECT_EQ(fontsync::fontFileSize("Doves", "Doves_08.cpfont"), 0);
  EXPECT_EQ(fontsync::fontFileSize("Doves", "Doves_8.cpfont"), 8);
  EXPECT_FALSE(fontsync::isSafeFontFileName("Doves", "Doves_99999999999.cpfont"));
  EXPECT_TRUE(fontsync::isSafeFontFileName("Doves", "Doves_255.cpfont"));
  EXPECT_TRUE(fontsync::isSafeFontFileName("Doves", "Doves_1.cpfont"));

  // ...and a file that claims to belong to a different family than the one
  // whose directory it is about to be written into.
  EXPECT_FALSE(fontsync::isSafeFontFileName("Doves", "Edgar_8.cpfont"));
  EXPECT_FALSE(fontsync::isSafeFontFileName("Doves", "../Edgar/Edgar_8.cpfont"));
  EXPECT_FALSE(fontsync::isSafeFontFileName("Doves", ""));
  EXPECT_FALSE(fontsync::isSafeFontFileName("Doves", nullptr));
  EXPECT_FALSE(fontsync::isSafeFontFileName("", "_8.cpfont"));
}

// ---------------------------------------------------------------------------
// Progress
// ---------------------------------------------------------------------------

TEST(FontSyncPlan, TheBarCountsFamiliesAndTheFilesWithinTheCurrentOne) {
  EXPECT_EQ(fontsync::overallPercent(0, 12, 0), 0u);
  EXPECT_EQ(fontsync::overallPercent(12, 12, 0), 100u);
  EXPECT_EQ(fontsync::overallPercent(6, 12, 0), 50u);
  // Halfway through the second of four families.
  EXPECT_EQ(fontsync::overallPercent(1, 4, 50), 37u);
  // Nothing to do at all must not divide by zero.
  EXPECT_EQ(fontsync::overallPercent(0, 0, 50), 0u);
  // Three of six files done, the fourth half downloaded.
  EXPECT_EQ(fontsync::familyPercent(3, 6, 50), 58u);
  EXPECT_EQ(fontsync::familyPercent(6, 6, 0), 100u);
}

// ---------------------------------------------------------------------------
// The summary's reason line, shared with Update Library
// ---------------------------------------------------------------------------

TEST(FontSyncPlan, TheDominantFailureKindIsNamed) {
  EXPECT_EQ(fontsync::dominantFailure(0, 0, 0), fontsync::FailureKind::NONE);
  EXPECT_EQ(fontsync::dominantFailure(3, 0, 0), fontsync::FailureKind::STORAGE);
  EXPECT_EQ(fontsync::dominantFailure(0, 3, 0), fontsync::FailureKind::NETWORK);
  EXPECT_EQ(fontsync::dominantFailure(0, 0, 3), fontsync::FailureKind::VERIFY);
}

// ---------------------------------------------------------------------------
// THE DIVERGENCE TEST
//
// Every bug this file has caught in the name validators has been the same bug:
// FontSyncPlan.h says a name is safe and SdCardFontRegistry does not discover
// it. The two predicates live in different files with nothing linking them, and
// the failure is always silent -- the file downloads, verifies, counts toward
// the commit gate, and is then ignored, so the family "installed completely"
// and is missing a size on screen.
//
// Three found that way before this test existed (2026-09-07):
//   * "Doves_999.cpfont" -- parseFilename takes only 1..255
//   * "_Doves"           -- scanRoot skips a leading '_' as well as a leading '.'
//   * "Doves_08.cpfont"  -- strtol reads it as 8, colliding with Doves_8
//
// So: transcribe the discovery rules from the registry, cite the lines, and
// assert over a generated corpus that ANYTHING THE MANIFEST MAY NAME IS
// SOMETHING DISCOVERY WILL FIND. If the registry's rules move, this test does
// not automatically follow -- but a divergence introduced on THIS side is
// caught, and that is the side that changes.
// ---------------------------------------------------------------------------

namespace discovery {

// SdCardFontRegistry::scanRoot:181 (directories) and scanDirectory:129 (files).
// Both read: if (nameBuffer[0] == '.' || nameBuffer[0] == '_') continue;
bool acceptsEntryName(const std::string& name) { return !name.empty() && name[0] != '.' && name[0] != '_'; }

// SdCardFontRegistry::parseFilename:75-108, transcribed.
bool parsesAsFontFile(const std::string& filename, unsigned& sizeOut) {
  static const std::string kExt = ".cpfont";
  if (filename.size() <= kExt.size()) return false;
  if (filename.compare(filename.size() - kExt.size(), kExt.size(), kExt) != 0) return false;
  const std::string base = filename.substr(0, filename.size() - kExt.size());
  if (base.empty() || base.size() > 127) return false;
  const size_t lastUnderscore = base.rfind('_');
  if (lastUnderscore == std::string::npos || lastUnderscore == 0) return false;
  const std::string sizeStr = base.substr(lastUnderscore + 1);
  // strtol + "*endPtr != '\0'" + the 1..255 range check.
  if (sizeStr.empty()) return false;
  for (const char c : sizeStr) {
    if (c < '0' || c > '9') return false;  // strtol would leave endPtr mid-string
  }
  const long value = std::strtol(sizeStr.c_str(), nullptr, 10);
  if (value < 1 || value > 255) return false;
  sizeOut = static_cast<unsigned>(value);
  return true;
}

}  // namespace discovery

TEST(FontSyncPlan, EveryFamilyNameTheManifestMayUseIsOneDiscoveryWillScan) {
  const std::vector<std::string> corpus = {
      "Doves",  "Edgar",  "TeXGyreSchola", "LibrisADF", "iA_Writer", "Bookerly-SD", "_Doves", "__",  "_", ".Doves",
      ".",      "..",     "-Doves",        "Doves.old", "a/b",       "a\\b",        "D",      "8pt", "0", "",
      "Doves ", " Doves", "Dov es",        "Doves\t",   "._Doves",   "Doves_",      "_8",     "A_",  "z", "Z9",
      "9Doves"};
  for (const auto& name : corpus) {
    if (!fontsync::isSafeFamilyName(name.c_str())) continue;
    EXPECT_TRUE(discovery::acceptsEntryName(name))
        << "isSafeFamilyName accepted \"" << name << "\", which SdCardFontRegistry::scanRoot skips. "
        << "The family would install, verify and never appear in the picker.";
  }
  // ...and the corpus is not vacuous: the real names must still pass.
  EXPECT_TRUE(fontsync::isSafeFamilyName("Doves"));
  EXPECT_TRUE(fontsync::isSafeFamilyName("TeXGyreSchola"));
  EXPECT_FALSE(fontsync::isSafeFamilyName("_Doves"));
}

TEST(FontSyncPlan, EveryFileNameTheManifestMayUseIsOneDiscoveryWillParse) {
  const std::vector<std::string> families = {"Doves", "Edgar", "iA_Writer", "Bookerly-SD", "D"};
  const std::vector<std::string> tails = {"_8.cpfont",
                                          "_08.cpfont",
                                          "_008.cpfont",
                                          "_0.cpfont",
                                          "_1.cpfont",
                                          "_255.cpfont",
                                          "_256.cpfont",
                                          "_999.cpfont",
                                          "_18.cpfont",
                                          "_8.CPFONT",
                                          "_8.cpfont.tmp",
                                          "_8.ttf",
                                          "_.cpfont",
                                          "_-8.cpfont",
                                          "_8_9.cpfont",
                                          ".cpfont",
                                          "_8",
                                          "",
                                          "_00000000008.cpfont"};
  unsigned accepted = 0;
  for (const auto& family : families) {
    for (const auto& tail : tails) {
      for (const std::string& name : {family + tail, tail, std::string("Edgar") + tail}) {
        const uint8_t mine = fontsync::fontFileSize(family.c_str(), name.c_str());
        if (mine == 0) continue;
        ++accepted;
        unsigned theirs = 0;
        // 1. discovery must not skip the file outright...
        EXPECT_TRUE(discovery::acceptsEntryName(name))
            << "fontFileSize accepted \"" << name << "\", which scanDirectory skips outright.";
        // 2. ...it must parse as a font file...
        ASSERT_TRUE(discovery::parsesAsFontFile(name, theirs))
            << "fontFileSize accepted \"" << name << "\" for family \"" << family
            << "\", which SdCardFontRegistry::parseFilename rejects. It would download, verify, "
            << "count toward the commit gate, and then be invisible.";
        // 3. ...and at the SAME size, or two manifest entries could collapse
        //    onto one on-disk cut while both are counted.
        EXPECT_EQ(static_cast<unsigned>(mine), theirs)
            << "fontFileSize and parseFilename disagree about the size of \"" << name << "\".";
      }
    }
  }
  // The corpus must actually exercise the accepting branch.
  EXPECT_GT(accepted, 5u);
}
