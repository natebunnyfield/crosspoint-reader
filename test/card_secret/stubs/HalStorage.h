// Host HalStorage stub for the card-secret reader: just enough of the real
// API (lib/hal/HalStorage.h) for openFileForRead() + read(), backed by real
// files under $CROSSPOINT_TEST_SD so the test can write the fixtures it reads.
// Same device-path-rebased-onto-a-root contract as the fuller stubs in
// test/activity_input/stubs and test/font_switch_churn/stubs.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>

inline std::string halStorageRoot() {
  const char* env = std::getenv("CROSSPOINT_TEST_SD");
  return (env && *env) ? std::string(env) : std::string("./fs_");
}
inline std::string halStoragePath(const char* devicePath) { return halStorageRoot() + devicePath; }

class HalFile {
  FILE* f = nullptr;

 public:
  HalFile() = default;
  explicit HalFile(FILE* fp) : f(fp) {}
  ~HalFile() {
    if (f) fclose(f);
  }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile& operator=(HalFile&& o) noexcept {
    if (this != &o) {
      if (f) fclose(f);
      f = o.f;
      o.f = nullptr;
    }
    return *this;
  }
  explicit operator bool() const { return f != nullptr; }
  int read(void* buf, size_t n) { return f ? static_cast<int>(fread(buf, 1, n, f)) : -1; }
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage s;
    return s;
  }
  bool openFileForRead(const char*, const char* p, HalFile& out) {
    FILE* fp = std::fopen(halStoragePath(p).c_str(), "rb");
    if (!fp) return false;
    out = HalFile(fp);
    return true;
  }
};
#define Storage HalStorage::getInstance()
