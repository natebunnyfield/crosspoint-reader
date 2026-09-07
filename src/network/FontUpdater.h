#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "FontSyncPlan.h"  // fontsync::CardStamp / FailureKind, for the record store below

/**
 * Update Fonts: sync the SD font roots against the .cpfont set published as the
 * `fonts-latest` release on natebunnyfield/claude-tools.
 *
 * Deliberately the same shape as LibraryUpdater -- same private repo, same
 * fine-grained PAT (read through network/GithubAuth.h; there is ONE token, not
 * one per feature), same asset API with "Accept: application/octet-stream"
 * because browser_download_url does not serve a private repo's assets, same
 * "never log the token" rule.
 *
 * WHY THE PUBLISHER IS LOCAL AND NOT release-fonts.yml.
 * .github/workflows/release-fonts.yml publishes .cpfont files to the PUBLIC
 * crosspoint-fonts repo, and can only ever build 7 of the 12 families in
 * `installed_families:`. The other five -- Edgar, DanteMT, LutetiaNova, Doves,
 * WarblerText -- come from commercial outlines in lib/EpdFont/local_fonts/,
 * which is gitignored and never leaves the owner's Mac. claude-tools'
 * scripts/publish_fonts.py builds all twelve on that Mac and uploads only the
 * resulting bitmaps.
 *
 * THE UNIT IS A FAMILY, AND THAT IS THE WHOLE DESIGN.
 * Device-side font download existed before and was deleted on 2026-08-10
 * (c3c8d1268) because "it could never install a family completely". A family is
 * six .cpfont files, one per size slot; SdCardFontRegistry offers a family
 * whatever subset of them is on the card, so a missing cut is a font that
 * silently has no 14 pt rather than an error. Three things make that state
 * unreachable here, and all three have to hold:
 *
 *   1. ANY file of a family needing work downloads the WHOLE family
 *      (fontsync::familyVerdict). There is no per-file install.
 *   2. Every byte lands in "<root>/.<Family>.part" first. That directory is
 *      invisible to discovery -- SdCardFontRegistry::scanRoot skips names
 *      beginning with '.' (SdCardFontRegistry.cpp:180) -- so a family being
 *      downloaded is ABSENT from the picker, never partially present.
 *   3. It moves into place only when every file verified against the manifest's
 *      byte count AND sha256 (fontsync::commitVerdict), by TWO directory
 *      renames with a rollback between them:
 *        <root>/<Family>      -> <root>/.<Family>.old   (if it existed)
 *        <root>/.<Family>.part -> <root>/<Family>
 *        remove <root>/.<Family>.old
 *      If the second rename fails the first is undone. A crash between them
 *      leaves the family ABSENT with its old copy in .old, which the next run
 *      restores before doing anything else (recoverStaleStaging). Absent is
 *      self-healing; partial is not.
 *
 * THE SWAP REPLACES THE DIRECTORY, SO THE OLD HI-RES <N>x SUBTREES GO WITH IT.
 * Owner ruling 2026-09-07, after an adversarial review found the deletion and
 * an implementation that preserved the tiers was written and withdrawn: the
 * device renders at scale 1 and never reads them, and a carried-over tier
 * would be older than the base cuts beside it. Deliberate, not incidental --
 * the full argument and the lines that make it invisible are at the deletion
 * itself in syncFamily(). A scaled host (iOS) re-seeds its tiers out of band.
 *
 * WHICH ROOT. The family's existing one if it is installed
 * (SdCardFontRegistry::findFamilyRoot), otherwise
 * SdCardFontRegistry::defaultWriteRoot() -- exactly what FontInstaller does
 * (FontInstaller.cpp:60-63). Writing to the OTHER root would be silently inert:
 * discovery scans /.fonts before /fonts and the first hit wins
 * (SdCardFontRegistry.cpp:210-214), so a new copy in /fonts under a family that
 * already exists in /.fonts would never be read.
 *
 * THE SYNC IS A MIRROR: a family the manifest does not list is REMOVED (owner
 * ruling 2026-09-07, reversing the "removal is not this feature" position
 * inherited from Update Library). After a run the font roots hold what the
 * manifest lists and nothing else. The trade-off was stated and accepted when
 * the ruling was made: a family sideloaded over File Transfer that is not in
 * `installed_families:` will be deleted by a sync. There is deliberately NO
 * exemption for such families -- adding one would narrow the ruling.
 *
 * This is the first thing this feature destroys that the owner put there, so
 * removeUnlistedFamilies() is fenced accordingly; see its declaration.
 *
 * THE DIGEST IS NOT RECOMPUTED WHEN NOTHING MOVED, for the reason Update
 * Library's ledger exists (owner ruling 2026-08-23) and more so: the installed
 * set is ~80 MB of .cpfont, and hashing all of it off the card to learn that
 * nothing changed would make an up-to-date run the slowest one. The ledger at
 * /.crosspoint/font_sync.json records size and mtime beside the digest last
 * verified, and every way of NOT knowing hashes (fontsync::hashVerdict).
 */
class FontUpdater {
 public:
  using ProgressCallback = void (*)(void* ctx);

  enum FontError {
    OK = 0,
    NO_TOKEN,        // no /github-token.txt and SETTINGS.githubToken empty — say so, do nothing else
    HTTP_ERROR,      // could not reach GitHub (or a non-404 failure)
    BAD_TOKEN,       // GitHub answered 401/403: the token is wrong, expired or unscoped
    NO_REPO_ACCESS,  // the token is valid but cannot see the fonts repo
    NO_RELEASE,      // GitHub answered 404: no fonts-latest release published
    NO_MANIFEST,     // release exists but carries no manifest.json asset
    JSON_PARSE_ERROR,
    MANIFEST_TOO_NEW,  // manifest.json declares a version this firmware does not act on
    OOM_ERROR,
  };

  enum class FamilyResult {
    ADDED,      // was not on the card
    UPDATED,    // was on the card, differed, replaced whole
    UNCHANGED,  // every file's size and sha256 matched — untouched
    FAILED,     // staging or verification failed; the installed family is exactly as it was
  };

  struct FontFile {
    std::string file;    // "<Family>_<size>.cpfont", the name discovery parses
    std::string url;     // GitHub asset API url
    size_t bytes = 0;    // expected size
    std::string sha256;  // expected digest, lowercase hex
  };

  struct Family {
    std::string name;  // "Doves" — also the directory name under the font root
    std::vector<FontFile> files;
  };

  // Which network step fetchManifest() is on, so the screen can say something
  // truer than "Checking for updates" while it blocks. Same reasoning as
  // LibraryUpdater::CheckStep: the whole check runs inside one Activity::loop()
  // call, so two honest labels are the only motion available on e-ink.
  enum class CheckStep {
    CONTACTING,  // asking GitHub for the release
    READING,     // fetching manifest.json from that release
  };
  using StepCallback = void (*)(void* ctx, CheckStep step);

  // Fetches the release JSON and manifest.json. On OK, getFamilies() is the plan.
  FontError fetchManifest(StepCallback onStep = nullptr, void* ctx = nullptr);

  // Delete every family in either font root that the manifest does not list,
  // appending the names removed to `removed`. Returns how many went.
  //
  // THE CATASTROPHIC FAILURE MODE IS "REMOVE EVERYTHING", so the fence comes
  // first:
  //   * it does nothing at all unless a fetchManifest() returned OK on this
  //     object (manifestOk_). A failed fetch, a truncated manifest and a
  //     zero-family manifest are all "we do not know what should be here",
  //     which is the one answer that must never be read as "so remove it all";
  //   * it deletes only names that isSafeFamilyName() accepts -- the SAME
  //     validator the install side uses -- resolved as "<root>/<name>" under
  //     one of the two SdCardFontRegistry root constants. No separator, no
  //     traversal, and nothing outside a font root is expressible;
  //   * it skips names beginning with '.' or '_', so this feature's own
  //     staging directories, macOS forks and .Trashes are never candidates.
  //     Those are also exactly the names discovery skips
  //     (SdCardFontRegistry.cpp:181), so the rule is one rule: it can only
  //     delete a directory the picker would have offered.
  //
  // BOTH ROOTS, because a stale family in the shadowed root is still a family
  // in the picker -- discovery merges /.fonts and /fonts and only dedupes by
  // name (SdCardFontRegistry.cpp:210-216).
  //
  // The CALLER decides when: FontUpdateActivity runs it after the per-family
  // loop and never after a cancel. See its comment there.
  size_t removeUnlistedFamilies(std::vector<std::string>& removed);

  const std::vector<Family>& getFamilies() const { return families; }

  // Compare-and-maybe-install one family, all or nothing. Progress is readable
  // through getProcessedSize/getTotalSize (the file in flight) and
  // getCurrentFile/getFileCount (where in the family it is) while this runs.
  FamilyResult syncFamily(size_t index, ProgressCallback onProgress = nullptr, void* ctx = nullptr);

  size_t getProcessedSize() const { return processedSize; }
  size_t getTotalSize() const { return totalSize; }
  // Files of the CURRENT family already finished, and how many it has. The
  // screen needs both to draw one honest bar over six downloads.
  size_t getCurrentFile() const { return currentFile; }
  size_t getFileCount() const { return fileCount; }

  // Why the most recent syncFamily() answered FAILED (NONE after any other
  // answer), so the summary can name the thing to fix rather than a count.
  fontsync::FailureKind lastFailure() const { return lastFailure_; }

  // Zero the per-family counters. syncFamily() does this first thing, but the
  // activity repaints the whole-run bar the moment it moves currentFamily on,
  // BEFORE syncFamily runs -- so it calls this under its render lock first, or
  // a tick between the two paints the previous family's 100%. Same defect and
  // same fix as LibraryUpdater::resetBookProgress (review 2026-09-04).
  void resetFamilyProgress() {
    processedSize = 0;
    totalSize = 0;
    currentFile = 0;
    fileCount = 0;
  }

  // End of run, called once. Writes the ledger if a syncFamily() changed it
  // (deliberately not per family -- the first run touches every entry and a
  // rewrite each time would be O(families^2) card writes), and then does the
  // post-install work that only matters if something actually installed:
  //
  //   * sdFontSystem.markRegistryDirty(), so the next ensureLoaded()
  //     re-discovers. That is what makes a NEW family appear in the picker,
  //     and what reloads a REPLACED file that is currently resident --
  //     ensureLoaded force-reloads after a re-discovery for exactly this
  //     reason (SdCardFontSystem.cpp:93-100).
  //   * clears /.crosspoint/epub_*/sections when the family the reader is
  //     using was replaced. Layout caches are keyed by fontId, which is
  //     computeFontId(contentHash, family, pointSize) and is checked on load
  //     (Section.cpp:362) -- so a rebuilt font normally invalidates its own
  //     caches. contentHash covers the global header plus each style's TOC
  //     entry only (SdCardFont.cpp:780-818), NOT glyph bitmaps or kern bytes,
  //     so a rebuild that changes kern VALUES while every count stays equal
  //     keeps the same id and silently reuses the old pagination. This run
  //     knows the file changed; the cache does not.
  void finishRun();

 private:
  // What a font file looked like when its digest was last verified. Persisted
  // to /.crosspoint/font_sync.json, keyed "<Family>/<file>".
  struct StoredRecord {
    std::string key;
    std::string sha;
    size_t bytes = 0;
    uint16_t fatDate = 0;
    uint16_t fatTime = 0;
  };

  std::vector<Family> families;
  fontsync::FailureKind lastFailure_ = fontsync::FailureKind::NONE;
  std::vector<StoredRecord> records;
  bool recordsLoaded = false;
  bool recordsDirty = false;
  bool manifestOk_ = false;          // a fetchManifest() returned OK -- the gate on any removal
  bool removedAny = false;           // at least one family was deleted this run
  bool installedAny = false;         // at least one family was ADDED or UPDATED this run
  bool activeFamilyChanged = false;  // ...and one of them is the family the reader is set to
  size_t processedSize = 0;
  size_t totalSize = 0;
  size_t currentFile = 0;
  size_t fileCount = 0;

  bool computeCardSha256(const std::string& path, char outHex[65]);
  void loadSyncRecords();
  void flushSyncRecords();
  const StoredRecord* findRecord(const std::string& key) const;
  void putRecord(const std::string& key, const fontsync::CardStamp& stamp, const std::string& sha);
  // How many of `family`'s files the card already holds byte-identical.
  size_t countMatchingFiles(const Family& family, const char* root);
  // Undo a run that died between the two commit renames, and sweep any
  // abandoned staging directory, BEFORE deciding anything about this family.
  void recoverStaleStaging(const char* root, const std::string& name);
  // Download every file of `family` into `stageDir`, verifying as it streams.
  // Returns how many verified; sets lastFailure_ and `failed` on the first
  // file that does not.
  size_t stageFamily(const Family& family, const std::string& stageDir, bool& failed, ProgressCallback onProgress,
                     void* ctx);
  void clearStaleSectionCaches();
};
