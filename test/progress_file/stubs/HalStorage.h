// Host HalStorage for the progress-file suite: an in-memory card that COUNTS
// the operations ProgressFile::writeAtomic issues, because what this suite
// asserts is not the bytes (any store gets those right) but WHICH filesystem
// operations a page turn costs -- the in-place rewrite exists to keep the FAT
// out of the hot path, and only a counter can prove that it does.
#pragma once

#include <fcntl.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using oflag_t = int;

struct StubCard {
  std::map<std::string, std::vector<uint8_t>> files;
  int removes = 0;        // successful remove()
  int renames = 0;        // successful rename()
  int truncates = 0;      // an existing file opened with O_TRUNC
  int inPlaceWrites = 0;  // bytes written through a handle opened on an existing file WITHOUT O_TRUNC
  void reset() { *this = StubCard{}; }
};
inline StubCard& stubCard() {
  static StubCard card;
  return card;
}

class HalFile {
  std::vector<uint8_t>* data = nullptr;  // std::map node: stable across inserts
  size_t pos = 0;
  bool open_ = false;
  bool inPlace_ = false;

 public:
  HalFile() = default;
  HalFile(std::vector<uint8_t>* d, bool inPlace) : data(d), open_(d != nullptr), inPlace_(inPlace) {}
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile(HalFile&& o) noexcept { *this = std::move(o); }
  HalFile& operator=(HalFile&& o) noexcept {
    data = o.data;
    pos = o.pos;
    open_ = o.open_;
    inPlace_ = o.inPlace_;
    o.data = nullptr;
    o.open_ = false;
    return *this;
  }
  explicit operator bool() const { return open_; }
  bool isOpen() const { return open_; }
  bool isDirectory() const { return false; }
  size_t fileSize() { return data ? data->size() : 0; }
  size_t write(const void* buf, size_t n) {
    if (!open_) return 0;
    if (inPlace_) stubCard().inPlaceWrites++;
    if (data->size() < pos + n) data->resize(pos + n);
    std::memcpy(data->data() + pos, buf, n);
    pos += n;
    return n;
  }
  int read(void* buf, size_t n) {
    if (!open_) return -1;
    const size_t avail = data->size() > pos ? data->size() - pos : 0;
    const size_t got = n < avail ? n : avail;
    std::memcpy(buf, data->data() + pos, got);
    pos += got;
    return static_cast<int>(got);
  }
  void flush() {}
  bool close() {
    const bool was = open_;
    open_ = false;
    data = nullptr;
    return was;
  }
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage s;
    return s;
  }
  HalFile open(const char* path, const oflag_t oflag = O_RDONLY) {
    auto& files = stubCard().files;
    const auto it = files.find(path);
    const bool existed = it != files.end();
    if (!existed && !(oflag & O_CREAT)) return HalFile();
    if (existed && (oflag & O_TRUNC)) stubCard().truncates++;
    const bool inPlace = existed && (oflag & (O_RDWR | O_WRONLY)) && !(oflag & O_TRUNC);
    auto& vec = files[path];
    if (oflag & O_TRUNC) vec.clear();
    return HalFile(&vec, inPlace);
  }
  bool openFileForWrite(const char*, const std::string& p, HalFile& out) {
    out = open(p.c_str(), O_RDWR | O_CREAT | O_TRUNC);
    return static_cast<bool>(out);
  }
  bool openFileForRead(const char*, const std::string& p, HalFile& out) {
    out = open(p.c_str(), O_RDONLY);
    return static_cast<bool>(out);
  }
  bool exists(const char* p) { return stubCard().files.count(p) != 0; }
  bool remove(const char* p) {
    if (!stubCard().files.erase(p)) return false;
    stubCard().removes++;
    return true;
  }
  bool rename(const char* from, const char* to) {
    auto& files = stubCard().files;
    if (!files.count(from) || files.count(to)) return false;  // SdFat: no overwrite
    files[to] = std::move(files[from]);
    files.erase(from);
    stubCard().renames++;
    return true;
  }
};
#define Storage HalStorage::getInstance()
