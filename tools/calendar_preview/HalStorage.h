#pragma once
#include <cstdio>
#include <cstdint>
#include <string>
#include <sys/stat.h>
class HalFile {
  FILE* f = nullptr;
 public:
  HalFile() = default;
  explicit HalFile(FILE* fp) : f(fp) {}
  ~HalFile() { if (f) fclose(f); }
  HalFile(HalFile&& o) noexcept : f(o.f) { o.f = nullptr; }
  HalFile& operator=(HalFile&& o) noexcept { if (f) fclose(f); f = o.f; o.f = nullptr; return *this; }
  HalFile(const HalFile&) = delete;
  size_t write(const void* buf, size_t n) { return f ? fwrite(buf, 1, n, f) : 0; }
  int read(void* buf, size_t n) { return f ? (int)fread(buf, 1, n, f) : -1; }
  int read() { return f ? fgetc(f) : -1; }
  bool seek(size_t p) { return f && fseek(f, (long)p, SEEK_SET) == 0; }
  bool seekCur(long off) { return f && fseek(f, off, SEEK_CUR) == 0; }
  bool seekSet(size_t p) { return seek(p); }
  size_t size() { if (!f) return 0; long c=ftell(f); fseek(f,0,SEEK_END); long s=ftell(f); fseek(f,c,SEEK_SET); return (size_t)s; }
  size_t position() const { return f ? (size_t)ftell(f) : 0; }
  int available() const { return f ? 1 : 0; }
  bool close() { if (f) { fclose(f); f = nullptr; } return true; }
  explicit operator bool() const { return f != nullptr; }
};
class HalStorage {
 public:
  static HalStorage& getInstance() { static HalStorage s; return s; }
  bool openFileForWrite(const char*, const char* p, HalFile& out) {
    std::string path = std::string("./fs_") + p;
    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp) return false;
    out = HalFile(fp);
    return true;
  }
  bool openFileForRead(const char*, const char* p, HalFile& out) {
    std::string path = std::string("./fs_") + p;
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return false;
    out = HalFile(fp);
    return true;
  }
  bool mkdir(const char* p, bool = true) { ::mkdir((std::string("./fs_") + p).c_str(), 0755); return true; }
  bool exists(const char* p) { struct stat st; return ::stat((std::string("./fs_") + p).c_str(), &st) == 0; }
  bool remove(const char* p) { return ::remove((std::string("./fs_") + p).c_str()) == 0; }
};
#define Storage HalStorage::getInstance()
