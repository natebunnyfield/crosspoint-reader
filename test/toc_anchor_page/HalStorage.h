// Host storage stub for this suite only.
//
// tools/calendar_preview's stub is the one the other layout suites use, and it
// is deliberately not reused here: it prefixes every path with "./fs_" (this
// suite writes its fixtures to a temp directory), its available() answers 1
// while the file is open (ChapterHtmlSlimParser::parseStep reads
// `available() == 0` as "this was the last buffer", so with that stub expat is
// never told the document ended), and it has no isOpen(), which
// abortParse()/finishParse() call. Three small differences, each of which would
// make the parse fail in a way that looks like a bug in the parser.
#pragma once

#include <Print.h>
#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
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
  bool openFileForWrite(const char*, const std::string& p, HalFile& out) {
    FILE* fp = fopen(p.c_str(), "wb");
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
};

#define Storage HalStorage::getInstance()
