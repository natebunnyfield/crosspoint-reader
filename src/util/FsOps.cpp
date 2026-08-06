#include "FsOps.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>

#include <cstring>
#include <utility>
#include <vector>

#include "CrossPointState.h"
#include "RecentBooksStore.h"
#include "util/BookCacheUtils.h"

namespace {
constexpr const char* CACHE_ROOT = "/.crosspoint";
constexpr const char* SUMMARIZE_MARKER = "/summarize.tag";
}  // namespace

namespace FsOps {

bool removeRecursiveWithCacheClear(const std::string& fullPath, char* nameBuffer, const size_t nameBufferSize) {
  if (!nameBuffer) {
    LOG_ERR("FsOps", "nameBuffer not allocated");
    return false;
  }

  auto file = Storage.open(fullPath.c_str());
  if (!file) {
    LOG_ERR("FsOps", "Failed to open for metadata clearing: %s", fullPath.c_str());
    return false;
  }

  if (!file.isDirectory()) {
    file.close();
    clearBookCache(fullPath);
    return Storage.remove(fullPath.c_str());
  }
  file.close();

  // Stack of (dirPath, postOrder): postOrder=true means rmdir this path after children are processed.
  std::vector<std::pair<std::string, bool>> stack;
  stack.reserve(16);
  stack.push_back({fullPath, false});

  while (!stack.empty()) {
    auto [currentPath, postOrder] = std::move(stack.back());
    stack.pop_back();

    if (postOrder) {
      if (!Storage.rmdir(currentPath.c_str())) {
        LOG_ERR("FsOps", "Failed to rmdir: %s", currentPath.c_str());
        return false;
      }
      continue;
    }

    auto dir = Storage.open(currentPath.c_str());
    if (!dir) {
      LOG_ERR("FsOps", "Failed to open dir: %s", currentPath.c_str());
      return false;
    }
    if (!dir.isDirectory()) {
      LOG_ERR("FsOps", "Not a directory: %s", currentPath.c_str());
      return false;
    }

    // Push this dir for post-order rmdir (after all children are processed).
    stack.push_back({currentPath, true});

    dir.rewindDirectory();
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(nameBuffer, nameBufferSize);
      if (strcmp(nameBuffer, ".") == 0 || strcmp(nameBuffer, "..") == 0) {
        continue;
      }
      std::string entryPath = currentPath;
      if (entryPath.back() != '/') {
        entryPath += "/";
      }
      entryPath += nameBuffer;

      const bool isDir = entry.isDirectory();
      entry.close();

      if (isDir) {
        stack.push_back({std::move(entryPath), false});
      } else {
        clearBookCache(entryPath);
        if (!Storage.remove(entryPath.c_str())) {
          LOG_ERR("FsOps", "Failed to remove file: %s", entryPath.c_str());
          return false;
        }
      }
    }
  }

  return true;
}

std::string bookCacheDirFor(const std::string& path) {
  // The book classes are the single source of truth for the cache key
  // (prefix + hash of the full path); their constructors only build strings.
  if (FsHelpers::hasEpubExtension(path)) {
    return Epub(path, CACHE_ROOT).getCachePath();
  }
  if (FsHelpers::hasXtcExtension(path)) {
    return Xtc(path, CACHE_ROOT).getCachePath();
  }
  if (FsHelpers::hasTxtExtension(path)) {
    return Txt(path, CACHE_ROOT).getCachePath();
  }
  return {};
}

std::string summarizeMarkerFor(const std::string& path) {
  const std::string cacheDir = bookCacheDirFor(path);
  if (cacheDir.empty()) {
    return {};
  }
  return cacheDir + SUMMARIZE_MARKER;
}

void migrateBookRefs(const std::string& oldPath, const std::string& newPath) {
  const std::string oldCache = bookCacheDirFor(oldPath);
  if (oldCache.empty()) {
    return;
  }
  const std::string newCache = bookCacheDirFor(newPath);

  if (Storage.exists(oldCache.c_str())) {
    if (!Storage.rename(oldCache.c_str(), newCache.c_str())) {
      // Ruling (docs/manage-files.md): migrate, but on any failure delete the
      // old cache rather than leave a stale orphan under the dead hash — the
      // book then re-parses cleanly at its new path.
      LOG_ERR("FsOps", "Failed to rename cache dir %s -> %s; deleting old cache", oldCache.c_str(), newCache.c_str());
      Storage.removeDir(oldCache.c_str());
    }
  }

  // The summarize marker rode along with the cache-dir rename; its content
  // (the book's card path, read by the host pipeline) must follow too.
  const std::string marker = newCache + SUMMARIZE_MARKER;
  if (Storage.exists(marker.c_str())) {
    HalFile file;
    if (Storage.openFileForWrite("FsOps", marker.c_str(), file)) {
      file.write(newPath.c_str(), newPath.size());
      file.write('\n');
    } else {
      LOG_ERR("FsOps", "Failed to rewrite summarize marker %s", marker.c_str());
    }
  }

  RECENT_BOOKS.updatePath(oldPath, newPath, oldCache, newCache);
  if (APP_STATE.openEpubPath == oldPath) {
    APP_STATE.openEpubPath = newPath;
    APP_STATE.saveToFile();
  }
}

void migrateBookRefsRecursive(const std::string& oldDirPath, const std::string& newDirPath, char* nameBuffer,
                              const size_t nameBufferSize) {
  if (!nameBuffer) {
    LOG_ERR("FsOps", "nameBuffer not allocated");
    return;
  }

  std::vector<std::string> stack;
  stack.reserve(8);
  stack.push_back(newDirPath);

  while (!stack.empty()) {
    const std::string currentPath = std::move(stack.back());
    stack.pop_back();

    auto dir = Storage.open(currentPath.c_str());
    if (!dir || !dir.isDirectory()) {
      continue;
    }

    dir.rewindDirectory();
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(nameBuffer, nameBufferSize);
      if (strcmp(nameBuffer, ".") == 0 || strcmp(nameBuffer, "..") == 0) {
        continue;
      }
      std::string entryPath = currentPath;
      if (entryPath.back() != '/') {
        entryPath += "/";
      }
      entryPath += nameBuffer;

      const bool isDir = entry.isDirectory();
      entry.close();

      if (isDir) {
        stack.push_back(std::move(entryPath));
      } else {
        // Old location of this file = old dir prefix + path suffix under the new dir.
        migrateBookRefs(oldDirPath + entryPath.substr(newDirPath.length()), entryPath);
      }
    }
  }
}

}  // namespace FsOps
