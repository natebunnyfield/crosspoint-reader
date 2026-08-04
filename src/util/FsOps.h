#pragma once

#include <string>

// File-management operations shared by FileBrowserActivity (long-press delete)
// and FileManagerActivity (rename/move/delete). All SD access goes through
// HalStorage; tree walks use explicit stacks, never recursion (small task
// stacks on ESP32-C3).
namespace FsOps {

// Delete a file or directory tree, clearing each book's `.crosspoint` cache in
// the same pass. `nameBuffer` is the caller's scratch buffer for SdFat
// getName() (avoids a per-call allocation). Returns false on the first
// failure, leaving the remainder in place.
bool removeRecursiveWithCacheClear(const std::string& fullPath, char* nameBuffer, size_t nameBufferSize);

// Cache directory for a book file (`/.crosspoint/<prefix>_<hash(path)>`), or
// "" when the path is not a cacheable book type (epub/xtc/txt).
std::string bookCacheDirFor(const std::string& path);

// After a successful rename/move of a single file: re-key its cache dir,
// repoint its Recent Books entry, and repoint APP_STATE.openEpubPath. No-op
// for non-book files. Cache-dir rename failure is logged and non-fatal.
void migrateBookRefs(const std::string& oldPath, const std::string& newPath);

// Same, after a directory was renamed/moved: walks the tree at newDirPath and
// migrates every book file found inside (old path derived by prefix swap).
void migrateBookRefsRecursive(const std::string& oldDirPath, const std::string& newDirPath, char* nameBuffer,
                              size_t nameBufferSize);

}  // namespace FsOps
