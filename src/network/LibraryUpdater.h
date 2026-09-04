#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "LibrarySyncPlan.h"  // librarysync::CardStamp, for the record store below

/**
 * Update Library: sync /books/ on the card against the epub set published as
 * the `library-latest` release on natebunnyfield/claude-tools.
 *
 * The shape mirrors OtaUpdater (check first, then act, progress by callback),
 * but the source repo is PRIVATE, so every request carries
 * "Authorization: Bearer <token>" — a GitHub fine-grained PAT read from
 * /github-token.txt on the card, or failing that SETTINGS.githubToken — and
 * assets download through the GitHub asset API with
 * "Accept: application/octet-stream" (browser_download_url does not serve a
 * private repo's assets). The token is never logged.
 *
 * Sync semantics, per book in manifest.json:
 *   missing on card, or size/sha256 differ  -> download to <file>.part, verify
 *                                              sha256, atomic rename over dest
 *   identical (size AND sha256)             -> skip
 * Books on the card that the manifest does not mention are NEVER touched —
 * removal is not this feature. Compare logic: LibrarySyncPlan.h (pure).
 *
 * THE DIGEST IS NOT RECOMPUTED WHEN NOTHING MOVED (owner ruling 2026-08-23).
 * Hashing every book on every run meant reading the whole library end to end to
 * learn, almost always, that it was unchanged. A ledger beside the card records
 * the size and modification time a book had when its digest was last verified,
 * and a book whose size AND mtime still match that record — against a manifest
 * still asking for the same digest — is passed over unread. Every way of not
 * knowing (no record, no mtime from the HAL, a manifest that now wants
 * something else) hashes, so the first run after this shipped hashes
 * everything and writes the ledger it will use next time.
 */
class LibraryUpdater {
 public:
  using ProgressCallback = void (*)(void* ctx);

  enum LibraryError {
    OK = 0,
    NO_TOKEN,        // no /github-token.txt and SETTINGS.githubToken empty — say so, do nothing else
    HTTP_ERROR,      // could not reach GitHub (or a non-404 failure)
    BAD_TOKEN,       // GitHub answered 401/403: the token is wrong, expired or unscoped
    NO_REPO_ACCESS,  // the token is valid but cannot see the library repo
    NO_RELEASE,      // GitHub answered 404: no library-latest release published
    NO_MANIFEST,     // release exists but carries no manifest.json asset
    JSON_PARSE_ERROR,
    MANIFEST_TOO_NEW,  // manifest.json declares a version this firmware does not act on
    OOM_ERROR,
  };

  enum class BookResult {
    ADDED,      // was not on the card
    UPDATED,    // was on the card, differed, replaced
    UNCHANGED,  // size and sha256 matched — untouched
    FAILED,     // download or verification failed; card file left as it was
  };

  struct Book {
    std::string file;    // true filename, restored on the card
    std::string url;     // GitHub asset API url
    size_t bytes = 0;    // expected size
    std::string sha256;  // expected digest, lowercase hex
  };

  // Which network step fetchManifest() is on, so the screen can say something
  // truer than "Checking for updates" while it blocks.
  //
  // The whole check runs inside one Activity::loop() call, so nothing repaints
  // and no button is read until it returns. On a slow network that is a dead
  // screen for seconds -- reported as the update "hanging on kickoff". These
  // steps are what it is actually doing, and the activity repaints between them
  // exactly as the per-book sync already does.
  //
  // Named steps rather than a spinner because this panel is e-ink: an animation
  // costs a refresh per frame, and two honest labels cost two.
  enum class CheckStep {
    CONTACTING,  // asking GitHub for the release
    READING,     // fetching manifest.json from that release
  };
  using StepCallback = void (*)(void* ctx, CheckStep step);

  // Fetches the release JSON and manifest.json. On OK, getBooks() is the plan.
  // onStep, when given, fires before each network request.
  LibraryError fetchManifest(StepCallback onStep = nullptr, void* ctx = nullptr);

  const std::vector<Book>& getBooks() const { return books; }

  // Compare-and-maybe-download one book. Progress (for the bar) is readable
  // through getProcessedSize/getTotalSize while this runs; the callback fires
  // on whole-percent change only, same reasoning as OtaUpdater.
  BookResult syncBook(size_t index, ProgressCallback onProgress = nullptr, void* ctx = nullptr);

  size_t getProcessedSize() const { return processedSize; }
  size_t getTotalSize() const { return totalSize; }
  // Zero the per-book counters. syncBook() does this first thing, but the
  // activity repaints the whole-sync bar the moment it moves currentBook on,
  // BEFORE syncBook runs -- so it calls this under its render lock first, or
  // a tick between the two paints the previous book's 100% (review
  // 2026-09-04, second pass).
  void resetBookProgress() {
    processedSize = 0;
    totalSize = 0;
  }

  // Write the ledger if a syncBook() changed it. Call once when the run ends.
  // Deliberately NOT written per book: the first run touches every entry, and a
  // rewrite each time would be O(books^2) card writes to save a hash the run is
  // already paying for. A run that dies before this costs the next run one more
  // hashing pass, which is exactly the old behaviour.
  void flushSyncRecords();

 private:
  // What a book looked like when its digest was last verified. Persisted to
  // /.crosspoint/library_sync.json; see LibrarySyncPlan.h for the verdict this
  // feeds and why every uncertainty hashes.
  struct StoredRecord {
    std::string file;
    std::string sha;
    size_t bytes = 0;
    uint16_t fatDate = 0;
    uint16_t fatTime = 0;
  };

  std::vector<Book> books;
  std::vector<StoredRecord> records;
  bool recordsLoaded = false;
  bool recordsDirty = false;
  size_t processedSize = 0;
  size_t totalSize = 0;

  bool computeCardSha256(const std::string& path, char outHex[65]);
  void loadSyncRecords();
  const StoredRecord* findRecord(const std::string& file) const;
  void putRecord(const std::string& file, const librarysync::CardStamp& stamp, const std::string& sha);
};
