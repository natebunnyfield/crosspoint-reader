// Host HalStorage stub with directory iteration.
//
// A superset of tools/calendar_preview/HalStorage.h: that one only needed file
// reads, so it has no Storage.open() / isDirectory() / openNextFile(), and
// SdCardFontRegistry::discover() cannot link against it. This one adds the
// directory API on top of std::filesystem, so the churn test drives the REAL
// registry over the real .cpfont tree instead of hand-faked family metadata.
//
// Paths are device-absolute ("/.fonts/Lora/Lora_14.cpfont") and are rebased
// onto the SD root, which defaults to ./fs_ (matching the simulator's
// CROSSPOINT_SIM_SD default). Tests therefore run with WORKING_DIRECTORY set
// to the repo root; CROSSPOINT_TEST_SD overrides it.
#pragma once

#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

inline std::string halStorageRoot() {
  const char* env = std::getenv("CROSSPOINT_TEST_SD");
  return (env && *env) ? std::string(env) : std::string("./fs_");
}

inline std::string halStoragePath(const char* devicePath) { return halStorageRoot() + devicePath; }

class HalFile {
  FILE* f = nullptr;
  bool isDir_ = false;
  std::string name_;
  std::vector<std::string> entries_;  // absolute host paths, directories only
  size_t nextEntry_ = 0;

 public:
  HalFile() = default;
  explicit HalFile(FILE* fp) : f(fp) {}

  // Directory handle.
  HalFile(const std::string& hostPath, bool asDirectory) : isDir_(asDirectory) {
    std::error_code ec;
    name_ = std::filesystem::path(hostPath).filename().string();
    if (!asDirectory) return;
    for (const auto& e : std::filesystem::directory_iterator(hostPath, ec)) {
      entries_.push_back(e.path().string());
    }
    std::sort(entries_.begin(), entries_.end());
  }

  ~HalFile() {
    if (f) fclose(f);
  }
  HalFile(HalFile&& o) noexcept
      : f(o.f), isDir_(o.isDir_), name_(std::move(o.name_)), entries_(std::move(o.entries_)),
        nextEntry_(o.nextEntry_) {
    o.f = nullptr;
  }
  HalFile& operator=(HalFile&& o) noexcept {
    if (f) fclose(f);
    f = o.f;
    o.f = nullptr;
    isDir_ = o.isDir_;
    name_ = std::move(o.name_);
    entries_ = std::move(o.entries_);
    nextEntry_ = o.nextEntry_;
    return *this;
  }
  HalFile(const HalFile&) = delete;

  size_t write(const void* buf, size_t n) { return f ? fwrite(buf, 1, n, f) : 0; }
  int read(void* buf, size_t n) { return f ? static_cast<int>(fread(buf, 1, n, f)) : -1; }
  int read() { return f ? fgetc(f) : -1; }
  bool seek(size_t p) { return f && fseek(f, static_cast<long>(p), SEEK_SET) == 0; }
  bool seekCur(long off) { return f && fseek(f, off, SEEK_CUR) == 0; }
  bool seekSet(size_t p) { return seek(p); }
  size_t size() {
    if (!f) return 0;
    long c = ftell(f);
    fseek(f, 0, SEEK_END);
    long s = ftell(f);
    fseek(f, c, SEEK_SET);
    return static_cast<size_t>(s);
  }
  size_t position() const { return f ? static_cast<size_t>(ftell(f)) : 0; }
  int available() const { return f ? 1 : 0; }
  bool close() {
    if (f) {
      fclose(f);
      f = nullptr;
    }
    return true;
  }
  explicit operator bool() const { return f != nullptr || isDir_; }

  bool isDirectory() const { return isDir_; }
  void getName(char* out, size_t n) const {
    if (!out || n == 0) return;
    std::snprintf(out, n, "%s", name_.c_str());
  }

  HalFile openNextFile() {
    while (nextEntry_ < entries_.size()) {
      const std::string p = entries_[nextEntry_++];
      std::error_code ec;
      if (std::filesystem::is_directory(p, ec)) return HalFile(p, true);
      FILE* fp = std::fopen(p.c_str(), "rb");
      if (!fp) continue;
      HalFile h(fp);
      h.name_ = std::filesystem::path(p).filename().string();
      return h;
    }
    return HalFile{};
  }
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage s;
    return s;
  }

  bool openFileForWrite(const char*, const char* p, HalFile& out) {
    FILE* fp = std::fopen(halStoragePath(p).c_str(), "wb");
    if (!fp) return false;
    out = HalFile(fp);
    return true;
  }
  bool openFileForRead(const char*, const char* p, HalFile& out) {
    FILE* fp = std::fopen(halStoragePath(p).c_str(), "rb");
    if (!fp) return false;
    out = HalFile(fp);
    return true;
  }
  HalFile open(const char* p) {
    const std::string host = halStoragePath(p);
    std::error_code ec;
    if (std::filesystem::is_directory(host, ec)) return HalFile(host, true);
    FILE* fp = std::fopen(host.c_str(), "rb");
    return fp ? HalFile(fp) : HalFile{};
  }
  bool mkdir(const char* p, bool = true) {
    std::error_code ec;
    std::filesystem::create_directories(halStoragePath(p), ec);
    return true;
  }
  bool exists(const char* p) {
    struct stat st;
    return ::stat(halStoragePath(p).c_str(), &st) == 0;
  }
  bool remove(const char* p) { return ::remove(halStoragePath(p).c_str()) == 0; }
};

#define Storage HalStorage::getInstance()
