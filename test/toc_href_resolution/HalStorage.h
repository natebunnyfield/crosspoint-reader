// Host storage stub for this suite only -- a real filesystem under HalStorage's
// API, because this suite makes Epub and Section do real work on a real card:
// unzip a chapter, write book.bin and section .bin files, rename a tmp into
// place, and read pages back out of a build that is still writing.
//
// Grown from test/toc_anchor_page/HalStorage.h, which explains why
// tools/calendar_preview's stub is not reused (it prefixes "./fs_", its
// available() never reports end-of-file, and it has no isOpen()). Three
// additions this suite needs and that one does not:
//
//   * rename()/removeDir(), which Section's commit path and Epub's cache
//     teardown call;
//   * openFileForWrite opens "w+b", not "wb". SDCardManager opens
//     O_RDWR | O_CREAT | O_TRUNC and Section::loadPageDuringBuild reads pages
//     back through the same handle it writes them with -- a write-only stub
//     makes every mid-build page read fail, which looks like a corrupt cache.

#pragma once

#include <Print.h>
#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

// Derives from Print for the same reason the real HalFile does: the parser
// hands a file straight to Epub::readItemContentsToStream, whose sink is a
// Print&.
class HalFile : public Print {
  FILE* f = nullptr;

 public:
  HalFile() = default;
  explicit HalFile(FILE* fp) : f(fp) {}
  ~HalFile() override {
    if (f) fclose(f);
  }
  HalFile(HalFile&& o) noexcept : f(o.f) { o.f = nullptr; }
  HalFile& operator=(HalFile&& o) noexcept {
    if (f) fclose(f);
    f = o.f;
    o.f = nullptr;
    return *this;
  }
  HalFile(const HalFile&) = delete;
  size_t write(const void* buf, size_t n) { return f ? fwrite(buf, 1, n, f) : 0; }
  size_t write(uint8_t b) override { return write(&b, static_cast<size_t>(1)); }
  size_t write(const uint8_t* buf, size_t n) override { return write(static_cast<const void*>(buf), n); }
  void flush() override {
    if (f) fflush(f);
  }
  int read(void* buf, size_t n) { return f ? static_cast<int>(fread(buf, 1, n, f)) : -1; }
  int read() { return f ? fgetc(f) : -1; }
  bool seek(size_t p) { return f && fseek(f, static_cast<long>(p), SEEK_SET) == 0; }
  bool seekCur(long off) { return f && fseek(f, off, SEEK_CUR) == 0; }
  bool seekSet(size_t p) { return seek(p); }
  size_t size() {
    if (!f) return 0;
    const long c = ftell(f);
    fseek(f, 0, SEEK_END);
    const long s = ftell(f);
    fseek(f, c, SEEK_SET);
    return static_cast<size_t>(s);
  }
  size_t position() const { return f ? static_cast<size_t>(ftell(f)) : 0; }
  int available() {
    if (!f) return 0;
    const long c = ftell(f);
    fseek(f, 0, SEEK_END);
    const long s = ftell(f);
    fseek(f, c, SEEK_SET);
    return static_cast<int>(s - c);
  }
  bool isOpen() const { return f != nullptr; }
  bool close() {
    if (f) {
      fclose(f);
      f = nullptr;
    }
    return true;
  }
  explicit operator bool() const { return f != nullptr; }
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage s;
    return s;
  }
  // "w+b", not "wb": SDCardManager opens O_RDWR | O_CREAT | O_TRUNC, and
  // Section::loadPageDuringBuild reads back from the same handle it writes
  // through. A write-only stub makes every mid-build page read fail.
  bool openFileForWrite(const char*, const std::string& p, HalFile& out) {
    FILE* fp = fopen(p.c_str(), "w+b");
    if (!fp) return false;
    out = HalFile(fp);
    return true;
  }
  bool openFileForWrite(const char* m, const char* p, HalFile& out) { return openFileForWrite(m, std::string(p), out); }
  bool openFileForRead(const char*, const std::string& p, HalFile& out) {
    FILE* fp = fopen(p.c_str(), "rb");
    if (!fp) return false;
    out = HalFile(fp);
    return true;
  }
  bool openFileForRead(const char* m, const char* p, HalFile& out) { return openFileForRead(m, std::string(p), out); }
  bool mkdir(const char* p, bool = true) {
    ::mkdir(p, 0755);
    return true;
  }
  bool exists(const char* p) {
    struct stat st;
    return ::stat(p, &st) == 0;
  }
  bool remove(const char* p) { return ::remove(p) == 0; }
  bool rename(const char* from, const char* to) { return ::rename(from, to) == 0; }
  bool rename(const std::string& from, const std::string& to) { return ::rename(from.c_str(), to.c_str()) == 0; }
  bool removeDir(const char* p, bool = true) {
    // Good enough for a fixture cache tree: shell out to rm -rf.
    const std::string cmd = std::string("rm -rf '") + p + "'";
    return system(cmd.c_str()) == 0;
  }
  bool removeDir(const std::string& p, bool r = true) { return removeDir(p.c_str(), r); }
};

#define Storage HalStorage::getInstance()
