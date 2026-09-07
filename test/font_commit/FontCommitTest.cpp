// The REAL FontUpdater against a REAL filesystem: staging, verification, the
// two-rename commit, the rollback, and what happens to the outgoing family
// directory. See Fixtures.h for why this suite exists.

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "Fixtures.h"
#include "HttpDownloader.h"
#include "network/FontUpdater.h"

namespace fs = std::filesystem;

namespace {

constexpr const char* kReleaseUrl =
    "https://api.github.com/repos/natebunnyfield/claude-tools/releases/tags/fonts-latest";
constexpr const char* kAssetBase = "https://api.github.com/repos/natebunnyfield/claude-tools/releases/assets/";

// The six cuts every installed family ships (owner ruling 2026-08-26).
const std::vector<int> kSizes = {8, 10, 12, 14, 16, 18};

std::string cpfontBytes(const std::string& family, int size, const std::string& generation) {
  // The magic FontInstaller and publish_fonts.py both check, then something
  // that differs per family, per size and per build.
  std::string body = "CPFONT\0\0";
  body.resize(8);
  body += family + "/" + std::to_string(size) + "/" + generation;
  body.resize(400 + size, '\xA5');  // >1 chunk, so a mid-file failure is reachable
  return body;
}

std::string fileName(const std::string& family, int size) { return family + "_" + std::to_string(size) + ".cpfont"; }

// A release carrying `families`, each with the six cuts, all from `generation`.
void publish(const std::vector<std::string>& families, const std::string& generation) {
  auto& s = fakegh::server();
  std::string assets = "[";
  std::string manifestFamilies = "[";
  bool firstFamily = true;
  int assetId = 1;

  for (const auto& family : families) {
    if (!firstFamily) manifestFamilies += ",";
    firstFamily = false;
    manifestFamilies += "{\"family\":\"" + family + "\",\"files\":[";
    bool firstFile = true;
    for (const int size : kSizes) {
      const std::string name = fileName(family, size);
      const std::string body = cpfontBytes(family, size, generation);
      const std::string url = kAssetBase + std::to_string(assetId++);
      s.bodies[url] = body;
      assets += "{\"name\":\"" + name + "\",\"url\":\"" + url + "\",\"size\":" + std::to_string(body.size()) + "},";
      if (!firstFile) manifestFamilies += ",";
      firstFile = false;
      manifestFamilies += "{\"file\":\"" + name + "\",\"asset\":\"" + name +
                          "\",\"bytes\":" + std::to_string(body.size()) + ",\"sha256\":\"" + sha256Hex(body) + "\"}";
    }
    manifestFamilies += "]}";
  }

  const std::string manifest = "{\"version\":1,\"generated\":\"test\",\"families\":" + manifestFamilies + "]}";
  const std::string manifestUrl = kAssetBase + std::string("manifest");
  s.bodies[manifestUrl] = manifest;
  assets +=
      "{\"name\":\"manifest.json\",\"url\":\"" + manifestUrl + "\",\"size\":" + std::to_string(manifest.size()) + "}]";

  s.bodies[kReleaseUrl] = "{\"tag_name\":\"fonts-latest\",\"assets\":" + assets + "}";
}

std::string assetUrlFor(const std::string& family, int size) {
  for (const auto& [url, body] : fakegh::server().bodies) {
    const std::string marker = family + "/" + std::to_string(size) + "/";
    if (body.size() > 8 && body.compare(8, marker.size(), marker) == 0) return url;
  }
  return {};
}

// --- on-disk helpers --------------------------------------------------------

std::string cardPath(const std::string& devicePath) { return halStorageRootRef() + devicePath; }

void writeCardFile(const std::string& devicePath, const std::string& body) {
  const fs::path p = cardPath(devicePath);
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << body;
}

std::string readCardFile(const std::string& devicePath) {
  std::ifstream in(cardPath(devicePath), std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<std::string> entryNames(const std::string& devicePath) {
  std::vector<std::string> names;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(cardPath(devicePath), ec)) {
    names.push_back(e.path().filename().string());
  }
  std::sort(names.begin(), names.end());
  return names;
}

// An already-installed family: six cuts of `generation`, plus the hi-res tiers
// a scaled host reads (SdCardFontManager.cpp:32-35). fs_/fonts carries 2x and
// 3x for every installed family today, which is what makes their fate a real
// question rather than a hypothetical.
void installOnCard(const std::string& root, const std::string& family, const std::string& generation,
                   bool withHiResTiers = true) {
  for (const int size : kSizes) {
    writeCardFile(root + "/" + family + "/" + fileName(family, size), cpfontBytes(family, size, generation));
  }
  if (withHiResTiers) {
    for (const char* tier : {"2x", "3x"}) {
      for (const int size : kSizes) {
        writeCardFile(root + "/" + family + "/" + tier + "/" + fileName(family, size),
                      cpfontBytes(family, size, std::string(generation) + tier));
      }
    }
  }
}

class FontCommit : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ =
        fs::temp_directory_path() / ("cp_font_commit_" + std::to_string(::getpid()) + "_" + std::to_string(counter_++));
    fs::remove_all(root_);
    fs::create_directories(root_);
    halStorageRootRef() = root_.string();
    fakegh::reset();
    // A token, or fetchManifest answers NO_TOKEN before it reaches the network.
    std::snprintf(SETTINGS.githubToken, sizeof(SETTINGS.githubToken), "%s", "ghp_test");
    SETTINGS.sdFontFamilyName[0] = '\0';
  }
  void TearDown() override {
    fs::remove_all(root_);
    halStorageRootRef().clear();
  }

  fs::path root_;
  static int counter_;
};

int FontCommit::counter_ = 0;

// Run a whole sync the way FontUpdateActivity does: check, then one family per
// call, then finishRun.
struct RunResult {
  FontUpdater::FontError check = FontUpdater::OK;
  std::vector<FontUpdater::FamilyResult> families;
  std::vector<std::string> removed;
};

// The order FontUpdateActivity uses: check, one family at a time, then removal,
// then finishRun. `stopAfterFamilies` models a CANCEL -- which, per the owner's
// 2026-09-07 ruling, skips removal entirely.
RunResult runSync(FontUpdater& updater, size_t stopAfterFamilies = 0) {
  RunResult r;
  r.check = updater.fetchManifest();
  bool canceled = false;
  if (r.check == FontUpdater::OK) {
    for (size_t i = 0; i < updater.getFamilies().size(); ++i) {
      updater.resetFamilyProgress();
      r.families.push_back(updater.syncFamily(i));
      if (stopAfterFamilies != 0 && r.families.size() >= stopAfterFamilies) {
        canceled = true;
        break;
      }
    }
    if (!canceled) updater.removeUnlistedFamilies(r.removed);
  }
  updater.finishRun();
  return r;
}

}  // namespace

// The digest primitive is ours, not mbedtls's, so prove it before anything
// leans on it. A self-consistent wrong hash would let every other test in this
// file pass while proving nothing.
TEST_F(FontCommit, TheSuitesSha256IsActuallySha256) {
  EXPECT_EQ(sha256Hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

// ---------------------------------------------------------------------------
// THE HI-RES RULING (owner, 2026-09-07)
// ---------------------------------------------------------------------------

// A commit REPLACES the family directory, so the outgoing hi-res <N>x subtrees
// go with it. This is deliberate and ruled, not incidental:
//
//   "the X4 renders at scale 1 and never reads 2x/3x, so deleting costs it
//    nothing and keeps each family internally consistent. Only a
//    RENDER_SCALE=2 host (the iOS build) notices, and falling back to
//    base-tier glyphs is better than reading a hi-res tier that is older than
//    the 1x cuts beside it."
//
// An implementation that CARRIED THE TIERS OVER was written on 2026-09-07 in
// response to the adversarial review that found the deletion, and was then
// withdrawn by that ruling. This test is what stops the next reader restoring
// it as an obvious oversight: the deletion is invisible in every other way,
// because scanDirectory skips subdirectories (SdCardFontRegistry.cpp:120-123)
// and removeDir is recursive (SDCardManager.cpp:380-411).
TEST_F(FontCommit, ACommitLeavesTheFamilysManifestCutsAndNoStaleHiResSubtree) {
  installOnCard("/.fonts", "Doves", "old");
  ASSERT_TRUE(fs::exists(cardPath("/.fonts/Doves/2x")));
  ASSERT_TRUE(fs::exists(cardPath("/.fonts/Doves/3x")));

  publish({"Doves"}, "new");
  FontUpdater updater;
  const RunResult r = runSync(updater);
  ASSERT_EQ(r.check, FontUpdater::OK);
  ASSERT_EQ(r.families.size(), 1u);
  EXPECT_EQ(r.families[0], FontUpdater::FamilyResult::UPDATED);

  // Exactly the manifest's six cuts, and nothing else -- no 2x, no 3x, no
  // staging leftovers.
  std::vector<std::string> expected;
  for (const int size : kSizes) expected.push_back(fileName("Doves", size));
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(entryNames("/.fonts/Doves"), expected);
  EXPECT_FALSE(fs::exists(cardPath("/.fonts/Doves/2x")));
  EXPECT_FALSE(fs::exists(cardPath("/.fonts/Doves/3x")));

  // ...and the six that remain are the NEW ones.
  for (const int size : kSizes) {
    EXPECT_EQ(readCardFile("/.fonts/Doves/" + fileName("Doves", size)), cpfontBytes("Doves", size, "new"));
  }
  // The whole root holds only the family: no .Doves.part, no .Doves.old.
  EXPECT_EQ(entryNames("/.fonts"), std::vector<std::string>{"Doves"});
}

// ---------------------------------------------------------------------------
// ALL OR NOTHING, on a real filesystem
// ---------------------------------------------------------------------------

// The download dies on the FOURTH cut. The previous family must be exactly as
// it was -- all six old cuts, byte for byte -- and nothing half-written may be
// visible anywhere under the root.
TEST_F(FontCommit, AFailureMidFamilyLeavesThePreviousInstallByteForByte) {
  installOnCard("/.fonts", "Doves", "old", /*withHiResTiers=*/false);
  publish({"Doves"}, "new");
  fakegh::server().failures[assetUrlFor("Doves", 14)] = {100, HttpDownloader::HTTP_ERROR};

  FontUpdater updater;
  const RunResult r = runSync(updater);
  ASSERT_EQ(r.families.size(), 1u);
  EXPECT_EQ(r.families[0], FontUpdater::FamilyResult::FAILED);
  EXPECT_EQ(updater.lastFailure(), fontsync::FailureKind::NETWORK);

  std::vector<std::string> expected;
  for (const int size : kSizes) expected.push_back(fileName("Doves", size));
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(entryNames("/.fonts/Doves"), expected);
  for (const int size : kSizes) {
    EXPECT_EQ(readCardFile("/.fonts/Doves/" + fileName("Doves", size)), cpfontBytes("Doves", size, "old"))
        << "cut " << size << " was disturbed by a failed install";
  }
  EXPECT_EQ(entryNames("/.fonts"), std::vector<std::string>{"Doves"});
}

// A family that was NOT installed and whose download fails must leave nothing
// behind at all -- in particular not a directory the picker would discover with
// three cuts in it.
TEST_F(FontCommit, AFailedFirstInstallLeavesNoFamilyDirectoryAtAll) {
  publish({"Doves"}, "new");
  fakegh::server().failures[assetUrlFor("Doves", 12)] = {50, HttpDownloader::HTTP_ERROR};

  FontUpdater updater;
  const RunResult r = runSync(updater);
  ASSERT_EQ(r.families.size(), 1u);
  EXPECT_EQ(r.families[0], FontUpdater::FamilyResult::FAILED);
  EXPECT_FALSE(fs::exists(cardPath("/.fonts/Doves")));
  EXPECT_TRUE(entryNames("/.fonts").empty());
}

// Bytes that arrive and do not match the manifest are a VERIFY failure, not a
// network one, and cost the installed family nothing.
TEST_F(FontCommit, CorruptedBytesAreRefusedAndNamedAsVerification) {
  installOnCard("/.fonts", "Doves", "old", /*withHiResTiers=*/false);
  publish({"Doves"}, "new");
  // Same length, different content: only the digest can catch this.
  std::string& body = fakegh::server().bodies[assetUrlFor("Doves", 16)];
  body[9] = static_cast<char>(body[9] ^ 0xFF);

  FontUpdater updater;
  const RunResult r = runSync(updater);
  ASSERT_EQ(r.families.size(), 1u);
  EXPECT_EQ(r.families[0], FontUpdater::FamilyResult::FAILED);
  EXPECT_EQ(updater.lastFailure(), fontsync::FailureKind::VERIFY);
  for (const int size : kSizes) {
    EXPECT_EQ(readCardFile("/.fonts/Doves/" + fileName("Doves", size)), cpfontBytes("Doves", size, "old"));
  }
}

// One family failing must not stop or damage the next.
TEST_F(FontCommit, AFamilyThatFailsDoesNotTakeItsNeighbourWithIt) {
  installOnCard("/.fonts", "Doves", "old", /*withHiResTiers=*/false);
  publish({"Doves", "Edgar"}, "new");
  fakegh::server().failures[assetUrlFor("Doves", 8)] = {32, HttpDownloader::HTTP_ERROR};

  FontUpdater updater;
  const RunResult r = runSync(updater);
  ASSERT_EQ(r.families.size(), 2u);
  EXPECT_EQ(r.families[0], FontUpdater::FamilyResult::FAILED);
  EXPECT_EQ(r.families[1], FontUpdater::FamilyResult::ADDED);
  for (const int size : kSizes) {
    EXPECT_EQ(readCardFile("/.fonts/Doves/" + fileName("Doves", size)), cpfontBytes("Doves", size, "old"));
    EXPECT_EQ(readCardFile("/.fonts/Edgar/" + fileName("Edgar", size)), cpfontBytes("Edgar", size, "new"));
  }
}

// ---------------------------------------------------------------------------
// Recovery from an interrupted commit -- the crash window, never otherwise run
// ---------------------------------------------------------------------------

// Power lost BETWEEN the two renames: the family is absent and its previous
// copy sits in .<Family>.old. The next run must put it back before deciding
// anything, which is what makes "absent" self-healing rather than a lost font.
TEST_F(FontCommit, AFamilyLeftAbsentByAnInterruptedCommitIsRestoredFirst) {
  installOnCard("/.fonts", "Doves", "old", /*withHiResTiers=*/false);
  fs::rename(cardPath("/.fonts/Doves"), cardPath("/.fonts/.Doves.old"));
  ASSERT_FALSE(fs::exists(cardPath("/.fonts/Doves")));

  // The manifest asks for exactly what the restored copy already holds, so the
  // ONLY way this can answer UNCHANGED is by having restored it first.
  publish({"Doves"}, "old");
  FontUpdater updater;
  const RunResult r = runSync(updater);
  ASSERT_EQ(r.families.size(), 1u);
  EXPECT_EQ(r.families[0], FontUpdater::FamilyResult::UNCHANGED);
  EXPECT_FALSE(fs::exists(cardPath("/.fonts/.Doves.old")));
  for (const int size : kSizes) {
    EXPECT_EQ(readCardFile("/.fonts/Doves/" + fileName("Doves", size)), cpfontBytes("Doves", size, "old"));
  }
}

// Power lost after the commit but before the cleanup: both present. The old
// copy is rubbish now and must simply go.
TEST_F(FontCommit, ABackupLeftBesideACommittedFamilyIsSweptAway) {
  installOnCard("/.fonts", "Doves", "old", /*withHiResTiers=*/false);
  installOnCard("/.fonts", ".Doves.old.tmp", "stale", /*withHiResTiers=*/false);
  fs::rename(cardPath("/.fonts/.Doves.old.tmp"), cardPath("/.fonts/.Doves.old"));

  publish({"Doves"}, "old");
  FontUpdater updater;
  runSync(updater);
  EXPECT_FALSE(fs::exists(cardPath("/.fonts/.Doves.old")));
  EXPECT_TRUE(fs::exists(cardPath("/.fonts/Doves")));
}

// An abandoned staging directory is a partial download with no way to know how
// partial. It is discarded, never adopted.
TEST_F(FontCommit, AnAbandonedStagingDirectoryIsDiscardedNotAdopted) {
  writeCardFile("/.fonts/.Doves.part/" + fileName("Doves", 8), "rubbish");
  writeCardFile("/.fonts/.Doves.part/" + fileName("Doves", 10), "rubbish");

  publish({"Doves"}, "new");
  FontUpdater updater;
  const RunResult r = runSync(updater);
  ASSERT_EQ(r.families.size(), 1u);
  EXPECT_EQ(r.families[0], FontUpdater::FamilyResult::ADDED);
  EXPECT_FALSE(fs::exists(cardPath("/.fonts/.Doves.part")));
  for (const int size : kSizes) {
    EXPECT_EQ(readCardFile("/.fonts/Doves/" + fileName("Doves", size)), cpfontBytes("Doves", size, "new"));
  }
}

// ---------------------------------------------------------------------------
// Skipping, roots, and the families this feature does not own
// ---------------------------------------------------------------------------

TEST_F(FontCommit, AnUpToDateFamilyIsNotRewritten) {
  installOnCard("/.fonts", "Doves", "same", /*withHiResTiers=*/false);
  publish({"Doves"}, "same");

  FontUpdater updater;
  const RunResult r = runSync(updater);
  ASSERT_EQ(r.families.size(), 1u);
  EXPECT_EQ(r.families[0], FontUpdater::FamilyResult::UNCHANGED);
  // Nothing was downloaded: the only requests are the release and the manifest.
  EXPECT_EQ(fakegh::server().requested.size(), 2u);
}

// A family in the VISIBLE root updates in the visible root. Writing the new
// copy into /.fonts instead would be silently inert on a card that has both,
// since discovery scans hidden first and the first hit wins
// (SdCardFontRegistry.cpp:210-216).
TEST_F(FontCommit, AFamilyInTheVisibleRootIsUpdatedWhereItLives) {
  fs::create_directories(cardPath("/.fonts"));  // exists, and is therefore the default write root
  installOnCard("/fonts", "Doves", "old", /*withHiResTiers=*/false);

  publish({"Doves"}, "new");
  FontUpdater updater;
  const RunResult r = runSync(updater);
  ASSERT_EQ(r.families.size(), 1u);
  EXPECT_EQ(r.families[0], FontUpdater::FamilyResult::UPDATED);
  EXPECT_FALSE(fs::exists(cardPath("/.fonts/Doves")));
  for (const int size : kSizes) {
    EXPECT_EQ(readCardFile("/fonts/Doves/" + fileName("Doves", size)), cpfontBytes("Doves", size, "new"));
  }
}

// ---------------------------------------------------------------------------
// REMOVAL (owner ruling 2026-09-07): the sync is a MIRROR.
//
// This reverses the "removal is not this feature" position inherited from
// Update Library, and it is the first thing this feature destroys that the
// owner put on the card. The ruling was made with the trade-off stated: a
// family sideloaded over File Transfer that is not in `installed_families:`
// WILL be deleted by a sync. There is deliberately no exemption for one, so
// there is deliberately no test asserting an exemption either.
//
// What the tests below are really guarding is the failure mode on the other
// side: "I could not read the manifest" being acted on as "so remove
// everything."
// ---------------------------------------------------------------------------

TEST_F(FontCommit, AFamilyTheManifestNoLongerListsIsRemoved) {
  installOnCard("/.fonts", "Rosarivo", "old", /*withHiResTiers=*/false);
  publish({"Doves"}, "new");

  FontUpdater updater;
  const RunResult r = runSync(updater);
  EXPECT_EQ(r.removed, std::vector<std::string>{"Rosarivo"});
  EXPECT_FALSE(fs::exists(cardPath("/.fonts/Rosarivo")));
  EXPECT_TRUE(fs::exists(cardPath("/.fonts/Doves")));
}

// THE LIVE CASE, and the reason this ruling was made concrete. fs_/fonts holds
// a family directory named DovesType while sd-fonts.yaml's recipe is now named
// Doves, so the two coexisted in the picker as near-duplicates. One sync must
// install Doves and take DovesType with it.
TEST_F(FontCommit, TheRenamedDovesTypeDirectoryIsReplacedByDovesInOneRun) {
  installOnCard("/.fonts", "DovesType", "old");  // with its 2x/3x tiers, as the card has
  publish({"Doves"}, "new");

  FontUpdater updater;
  const RunResult r = runSync(updater);
  ASSERT_EQ(r.families.size(), 1u);
  EXPECT_EQ(r.families[0], FontUpdater::FamilyResult::ADDED);
  EXPECT_EQ(r.removed, std::vector<std::string>{"DovesType"});
  EXPECT_FALSE(fs::exists(cardPath("/.fonts/DovesType")));
  EXPECT_EQ(entryNames("/.fonts"), std::vector<std::string>{"Doves"});
  for (const int size : kSizes) {
    EXPECT_EQ(readCardFile("/.fonts/Doves/" + fileName("Doves", size)), cpfontBytes("Doves", size, "new"));
  }
}

// BOTH ROOTS. Discovery merges /.fonts and /fonts and dedupes only by name
// (SdCardFontRegistry.cpp:210-216), so a stale family in the shadowed root is
// still a family in the picker.
TEST_F(FontCommit, AnUnlistedFamilyIsRemovedFromEitherRoot) {
  installOnCard("/.fonts", "Rosarivo", "old", /*withHiResTiers=*/false);
  installOnCard("/fonts", "Vollkorn", "old", /*withHiResTiers=*/false);
  publish({"Doves"}, "new");

  FontUpdater updater;
  RunResult r = runSync(updater);
  std::sort(r.removed.begin(), r.removed.end());
  EXPECT_EQ(r.removed, (std::vector<std::string>{"Rosarivo", "Vollkorn"}));
  EXPECT_FALSE(fs::exists(cardPath("/.fonts/Rosarivo")));
  EXPECT_FALSE(fs::exists(cardPath("/fonts/Vollkorn")));
}

// --- the three "remove nothing" cases --------------------------------------
//
// Each of these is a way of NOT KNOWING what belongs on the card, and each must
// be acted on as "leave it alone". Getting any one of them wrong wipes the
// owner's fonts on the first bad network day.

TEST_F(FontCommit, AFetchFailureRemovesNothing) {
  installOnCard("/.fonts", "Rosarivo", "old", /*withHiResTiers=*/false);
  // No release published at all: fetchManifest answers NO_RELEASE.
  FontUpdater updater;
  std::vector<std::string> removed;
  EXPECT_NE(updater.fetchManifest(), FontUpdater::OK);
  EXPECT_EQ(updater.removeUnlistedFamilies(removed), 0u);
  EXPECT_TRUE(removed.empty());
  EXPECT_TRUE(fs::exists(cardPath("/.fonts/Rosarivo")));
}

TEST_F(FontCommit, AManifestListingNoFamiliesRemovesNothing) {
  installOnCard("/.fonts", "Rosarivo", "old", /*withHiResTiers=*/false);
  publish({}, "new");  // a real, well-formed release whose families array is empty

  FontUpdater updater;
  std::vector<std::string> removed;
  EXPECT_EQ(updater.fetchManifest(), FontUpdater::OK);
  ASSERT_TRUE(updater.getFamilies().empty());
  EXPECT_EQ(updater.removeUnlistedFamilies(removed), 0u);
  EXPECT_TRUE(fs::exists(cardPath("/.fonts/Rosarivo")));
}

TEST_F(FontCommit, ATruncatedManifestRemovesNothing) {
  installOnCard("/.fonts", "Rosarivo", "old", /*withHiResTiers=*/false);
  publish({"Doves"}, "new");
  // Cut the manifest in half: valid JSON prefix, unparseable document.
  std::string& manifest = fakegh::server().bodies[std::string(kAssetBase) + "manifest"];
  manifest = manifest.substr(0, manifest.size() / 2);

  FontUpdater updater;
  std::vector<std::string> removed;
  EXPECT_EQ(updater.fetchManifest(), FontUpdater::JSON_PARSE_ERROR);
  EXPECT_EQ(updater.removeUnlistedFamilies(removed), 0u);
  EXPECT_TRUE(fs::exists(cardPath("/.fonts/Rosarivo")));
}

// A SECOND fetch that fails must re-close the gate a first, successful fetch
// opened. Without clearing manifestOk_ on entry, a retry after a network drop
// would still be holding the previous run's family list.
TEST_F(FontCommit, AFailedRefetchClosesTheGateAgain) {
  installOnCard("/.fonts", "Rosarivo", "old", /*withHiResTiers=*/false);
  publish({"Doves"}, "new");

  FontUpdater updater;
  ASSERT_EQ(updater.fetchManifest(), FontUpdater::OK);
  fakegh::server().bodies.clear();  // the network goes away
  EXPECT_NE(updater.fetchManifest(), FontUpdater::OK);

  std::vector<std::string> removed;
  EXPECT_EQ(updater.removeUnlistedFamilies(removed), 0u);
  EXPECT_TRUE(fs::exists(cardPath("/.fonts/Rosarivo")));
}

// --- what removal may never reach ------------------------------------------

// Nothing outside a font root, and nothing inside one that discovery would not
// have offered. The staging directories are this feature's own and are swept
// per family by recoverStaleStaging, never by the mirror.
TEST_F(FontCommit, RemovalTouchesNothingOutsideTheFontRootsAndNoDotDirectories) {
  writeCardFile("/books/Novel.epub", "an epub");
  writeCardFile("/.crosspoint/settings.json", "{}");
  writeCardFile("/github-token.txt", "ghp_test");
  writeCardFile("/.fonts/.Doves.part/" + fileName("Doves", 8), "partial");
  writeCardFile("/.fonts/.Trashes/whatever", "junk");
  writeCardFile("/.fonts/_MACOSX/whatever", "junk");
  publish({"Edgar"}, "new");

  FontUpdater updater;
  const RunResult r = runSync(updater);
  EXPECT_TRUE(r.removed.empty());
  EXPECT_TRUE(fs::exists(cardPath("/books/Novel.epub")));
  EXPECT_TRUE(fs::exists(cardPath("/.crosspoint/settings.json")));
  EXPECT_TRUE(fs::exists(cardPath("/github-token.txt")));
  EXPECT_TRUE(fs::exists(cardPath("/.fonts/.Trashes/whatever")));
  EXPECT_TRUE(fs::exists(cardPath("/.fonts/_MACOSX/whatever")));
}

// A CANCEL NEVER REMOVES. A reader who stopped the run did not ask for a
// mirror, and the families that were never reached are not evidence of
// anything.
TEST_F(FontCommit, ACancelledRunRemovesNothingAndLeavesReachedFamiliesComplete) {
  installOnCard("/.fonts", "Rosarivo", "old", /*withHiResTiers=*/false);
  publish({"Doves", "Edgar"}, "new");

  FontUpdater updater;
  const RunResult r = runSync(updater, /*stopAfterFamilies=*/1);
  ASSERT_EQ(r.families.size(), 1u);
  EXPECT_TRUE(r.removed.empty());
  EXPECT_TRUE(fs::exists(cardPath("/.fonts/Rosarivo")));

  // The family that WAS reached is complete; the one that was not is absent.
  // No third state -- that is the whole point of stopping between families.
  std::vector<std::string> expected;
  for (const int size : kSizes) expected.push_back(fileName("Doves", size));
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(entryNames("/.fonts/Doves"), expected);
  EXPECT_FALSE(fs::exists(cardPath("/.fonts/Edgar")));
}

// The ledger exists so an up-to-date run does not read ~80 MB off the card. The
// first run writes it; the second must not re-verify by hashing.
TEST_F(FontCommit, TheLedgerIsWrittenAndTheNextRunSkipsTheHash) {
  publish({"Doves"}, "new");
  {
    FontUpdater updater;
    const RunResult r = runSync(updater);
    ASSERT_EQ(r.families.size(), 1u);
    EXPECT_EQ(r.families[0], FontUpdater::FamilyResult::ADDED);
  }
  EXPECT_TRUE(fs::exists(cardPath("/.crosspoint/font_sync.json")));

  fakegh::server().requested.clear();
  {
    FontUpdater updater;
    const RunResult r = runSync(updater);
    ASSERT_EQ(r.families.size(), 1u);
    EXPECT_EQ(r.families[0], FontUpdater::FamilyResult::UNCHANGED);
    EXPECT_EQ(fakegh::server().requested.size(), 2u);  // release + manifest, no cuts
  }
}
