// Host HalStorage for the font-commit suite: a real filesystem under a
// per-test temp root, not the read-only stand-in the other suites use.
//
// This suite exists to exercise FontUpdater's STAGING AND COMMIT for real --
// mkdir, write, the two directory renames, the rollback and the recursive
// delete -- so the storage layer has to actually do those things. The other
// stubs in test/ implement only what their suite reads; this one implements
// what a commit writes, including the pieces nothing else needed:
// rename() over DIRECTORIES, removeDir() recursively, and getModifyDateTime().
//
// Device paths are rebased onto the root that halStorageRoot() names, the same
// contract as test/font_switch_churn/stubs/HalStorage.h, except the root is
// settable per test rather than fixed at ./fs_ -- a test that swaps directories
// around must not be able to touch the repo's own card tree.
#pragma once

#include <sys/stat.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

// src/network/HttpDownloader.h declares fetchUrl(..., Stream&) and reaches
// Stream through the real lib/hal/HalStorage.h's <Print.h>. That header is
// bound to directly here rather than shadowed -- a quoted include resolves to
// the including file's own directory first, so FontUpdater.cpp would find the
// real one whatever this suite did, and binding to it means a signature change
// breaks the suite loudly instead of silently linking a stale double.
class Stream;

std::string& halStorageRootRef();
inline std::string halStorageRoot() { return halStorageRootRef(); }
inline std::string halStoragePath(const char* devicePath) { return halStorageRoot() + devicePath; }
inline std::string halStoragePath(const std::string& devicePath) { return halStorageRoot() + devicePath; }

class HalFile {
  FILE* f = nullptr;
  bool isDir_ = false;
  std::string name_;
  std::string full_;
  std::vector<std::string> children_;
  size_t childIndex_ = 0;

 public:
  HalFile() = default;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile(HalFile&& o) noexcept { *this = std::move(o); }
  HalFile& operator=(HalFile&& o) noexcept {
    if (this != &o) {
      close();
      f = o.f;
      isDir_ = o.isDir_;
      name_ = std::move(o.name_);
      full_ = std::move(o.full_);
      children_ = std::move(o.children_);
      childIndex_ = o.childIndex_;
      o.f = nullptr;
      o.isDir_ = false;
      o.childIndex_ = 0;
    }
    return *this;
  }
  ~HalFile() { close(); }

  void openFile(const std::string& full, const char* mode) {
    close();
    full_ = full;
    f = std::fopen(full.c_str(), mode);
    isDir_ = false;
    name_ = std::filesystem::path(full).filename().string();
  }
  void openDir(const std::string& full) {
    close();
    full_ = full;
    isDir_ = true;
    name_ = std::filesystem::path(full).filename().string();
    children_.clear();
    childIndex_ = 0;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(full, ec)) {
      children_.push_back(e.path().string());
    }
  }

  bool isOpen() const { return f != nullptr || isDir_; }
  explicit operator bool() const { return isOpen(); }
  bool isDirectory() const { return isDir_; }

  size_t getName(char* out, size_t len) {
    const size_t n = name_.size() < len - 1 ? name_.size() : len - 1;
    std::memcpy(out, name_.data(), n);
    out[n] = '\0';
    return n;
  }

  HalFile openNextFile() {
    HalFile child;
    if (!isDir_ || childIndex_ >= children_.size()) return child;
    const std::string path = children_[childIndex_++];
    if (std::filesystem::is_directory(path)) {
      child.openDir(path);
    } else {
      child.openFile(path, "rb");
    }
    return child;
  }

  size_t size() {
    if (!f) return 0;
    const long here = std::ftell(f);
    std::fseek(f, 0, SEEK_END);
    const long end = std::ftell(f);
    std::fseek(f, here, SEEK_SET);
    return static_cast<size_t>(end < 0 ? 0 : end);
  }

  // FAT-encoded, derived from the real mtime so the ledger's size+mtime skip is
  // exercised rather than short-circuited. Second resolution is halved exactly
  // as FAT does it, which is also why a test that rewrites a file inside one
  // second must change its SIZE to be seen as different -- the device has the
  // same limitation, and pretending otherwise here would hide it.
  bool getModifyDateTime(uint16_t* pdate, uint16_t* ptime) {
    struct stat st{};
    if (::stat(full_.c_str(), &st) != 0) return false;
    const std::time_t t = st.st_mtime;
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    const int year = tm.tm_year + 1900;
    *pdate = static_cast<uint16_t>(((year - 1980) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
    *ptime = static_cast<uint16_t>((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
    return true;
  }

  int read(void* buf, size_t count) {
    if (!f) return -1;
    return static_cast<int>(std::fread(buf, 1, count, f));
  }
  size_t write(const void* buf, size_t count) { return f ? std::fwrite(buf, 1, count, f) : 0; }

  bool close() {
    if (f) {
      std::fclose(f);
      f = nullptr;
    }
    isDir_ = false;
    children_.clear();
    childIndex_ = 0;
    return true;
  }
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage s;
    return s;
  }

  bool exists(const char* p) {
    std::error_code ec;
    return std::filesystem::exists(halStoragePath(p), ec);
  }
  bool mkdir(const char* p, bool = true) {
    std::error_code ec;
    std::filesystem::create_directories(halStoragePath(p), ec);
    return !ec;
  }
  bool ensureDirectoryExists(const char* p) { return mkdir(p, true); }
  bool remove(const char* p) {
    std::error_code ec;
    return std::filesystem::remove(halStoragePath(p), ec);
  }
  // Recursive, exactly as SDCardManager::removeDir is (SDCardManager.cpp:380-411).
  // The font commit relies on that: it is what takes the outgoing family's
  // hi-res <N>x subtrees with it, by owner ruling 2026-09-07.
  bool removeDir(const char* p) {
    std::error_code ec;
    return std::filesystem::remove_all(halStoragePath(p), ec) > 0;
  }
  // Must work on DIRECTORIES, and must FAIL when the destination exists --
  // both are what SdFat and POSIX rename do, and the commit's correctness
  // rests on the second.
  bool rename(const char* oldPath, const char* newPath) {
    std::error_code ec;
    const std::string to = halStoragePath(newPath);
    if (std::filesystem::exists(to, ec)) return false;
    std::filesystem::rename(halStoragePath(oldPath), to, ec);
    return !ec;
  }

  HalFile open(const char* p) {
    HalFile h;
    const std::string full = halStoragePath(p);
    std::error_code ec;
    if (!std::filesystem::exists(full, ec)) return h;
    if (std::filesystem::is_directory(full, ec)) {
      h.openDir(full);
    } else {
      h.openFile(full, "rb");
    }
    return h;
  }

  bool openFileForRead(const char*, const char* p, HalFile& out) {
    std::error_code ec;
    const std::string full = halStoragePath(p);
    if (!std::filesystem::is_regular_file(full, ec)) return false;
    out.openFile(full, "rb");
    return out.isOpen();
  }
  bool openFileForRead(const char* tag, const std::string& p, HalFile& out) {
    return openFileForRead(tag, p.c_str(), out);
  }
  bool openFileForWrite(const char*, const char* p, HalFile& out) {
    const std::string full = halStoragePath(p);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(full).parent_path(), ec);
    out.openFile(full, "wb");
    return out.isOpen();
  }
  bool openFileForWrite(const char* tag, const std::string& p, HalFile& out) {
    return openFileForWrite(tag, p.c_str(), out);
  }
};

#define Storage HalStorage::getInstance()
