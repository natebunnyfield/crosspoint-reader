#pragma once

#include <cstddef>
#include <string>
#include <vector>

/**
 * Update Library: sync /books/ on the card against the epub set published as
 * the `library-latest` release on natebunnyfield/claude-tools.
 *
 * The shape mirrors OtaUpdater (check first, then act, progress by callback),
 * but the source repo is PRIVATE, so every request carries
 * "Authorization: Bearer <token>" — a GitHub fine-grained PAT read from
 * SETTINGS.githubToken — and assets download through the GitHub asset API with
 * "Accept: application/octet-stream" (browser_download_url does not serve a
 * private repo's assets). The token is never logged.
 *
 * Sync semantics, per book in manifest.json:
 *   missing on card, or size/sha256 differ  -> download to <file>.part, verify
 *                                              sha256, atomic rename over dest
 *   identical (size AND sha256)             -> skip
 * Books on the card that the manifest does not mention are NEVER touched —
 * removal is not this feature. Compare logic: LibrarySyncPlan.h (pure).
 */
class LibraryUpdater {
 public:
  using ProgressCallback = void (*)(void* ctx);

  enum LibraryError {
    OK = 0,
    NO_TOKEN,     // SETTINGS.githubToken is empty — say so, do nothing else
    HTTP_ERROR,   // could not reach GitHub (or a non-404 failure)
    NO_RELEASE,   // GitHub answered 404: no library-latest release published
    NO_MANIFEST,  // release exists but carries no manifest.json asset
    JSON_PARSE_ERROR,
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

  // Fetches the release JSON and manifest.json. On OK, getBooks() is the plan.
  LibraryError fetchManifest();

  const std::vector<Book>& getBooks() const { return books; }

  // Compare-and-maybe-download one book. Progress (for the bar) is readable
  // through getProcessedSize/getTotalSize while this runs; the callback fires
  // on whole-percent change only, same reasoning as OtaUpdater.
  BookResult syncBook(size_t index, ProgressCallback onProgress = nullptr, void* ctx = nullptr);

  size_t getProcessedSize() const { return processedSize; }
  size_t getTotalSize() const { return totalSize; }

 private:
  std::vector<Book> books;
  size_t processedSize = 0;
  size_t totalSize = 0;

  bool computeCardSha256(const std::string& path, char outHex[65]);
};
