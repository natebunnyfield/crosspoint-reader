#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

struct RecentBook {
  std::string path;
  std::string title;
  std::string author;
  std::string coverBmpPath;

  bool operator==(const RecentBook& other) const { return path == other.path; }
};

class RecentBooksStore : public PersistableStore<RecentBooksStore> {
 private:
  std::vector<RecentBook> recentBooks;

  static constexpr int MAX_RECENT_BOOKS = 10;

  RecentBooksStore() = default;
  ~RecentBooksStore() = default;

  friend class PersistableStore<RecentBooksStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/recent.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Add a book to the recent list (moves to front if already exists)
  void addBook(const std::string& path, const std::string& title, const std::string& author,
               const std::string& coverBmpPath);

  void updateBook(const std::string& path, const std::string& title, const std::string& author,
                  const std::string& coverBmpPath);

  // Un-stick the "this book has no cover" sentinel after the book's content changed underneath
  // it. generateThumbBmp() writes coverBmpPath = "" the first time it fails (no cover in the
  // OPF, or a format the device can't decode), and nothing normally clears that -- it is a
  // deliberate cache, so a coverless book is not re-decoded on every Home render. But a Library
  // sync CAN change what generateThumbBmp would find (a book re-published with a cover it did
  // not have before, or in a format it can now decode), and syncing already clears the sibling
  // on-disk /.crosspoint/epub_<hash> cache for exactly this reason. This does the same job for
  // this JSON-backed ledger: restores coverBmpPath to newCoverBmpPath (the fresh
  // epub.getThumbBmpPath() template) so the next Home render gives generateThumbBmp() one more
  // try, WITHOUT touching title or author. No-op if no entry matches path (a book that has
  // never been opened has no recent-list entry to fix, and needs none). Persists on success.
  void resetCoverForPath(const std::string& path, const std::string& newCoverBmpPath);

  // Remove the entry whose path matches (used when a book is removed from recents or finished/read).
  // Returns true if an entry was found and removed (no-op + false otherwise).
  // Persistence is best-effort: a failed save is logged, not reflected in the return.
  bool removeByPath(const std::string& path);

  // Repoint an entry's path (and coverBmpPath, if it lived under the old cache dir) after the
  // backing file and cache dir were moved on disk. No-op if no entry matches oldPath.
  // Persists on success. Keeps the entry's list position (does not reorder).
  void updatePath(const std::string& oldPath, const std::string& newPath, const std::string& oldCachePath,
                  const std::string& newCachePath);

  // True if the book's backing file is no longer present on the SD card.
  static bool isMissing(const RecentBook& book);

  // Remove entries whose backing file is no longer on the SD card.
  // Returns true if any entry was removed. Does not persist — caller decides.
  bool pruneMissing();

  // Get the list of recent books (most recent first)
  const std::vector<RecentBook>& getBooks() const { return recentBooks; }

  // Get the count of recent books
  int getCount() const { return static_cast<int>(recentBooks.size()); }

  RecentBook getDataFromBook(std::string path) const;
};

// Helper macro to access recent books store
#define RECENT_BOOKS RecentBooksStore::getInstance()
