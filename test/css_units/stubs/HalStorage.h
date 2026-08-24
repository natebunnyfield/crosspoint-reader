// CssParser.cpp's cache methods take a HalFile; this suite drives only the
// declaration path, so the file type has to exist and never has to work.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class HalFile {
 public:
  size_t write(const void*, const size_t len) { return len; }
  size_t write(const uint8_t) { return 1; }
  int read(void*, const size_t) { return 0; }
  int read() { return -1; }
  size_t position() const { return 0; }
  size_t size() const { return 0; }
  bool seek(const size_t) { return false; }
  void close() {}
  bool available() const { return false; }
  explicit operator bool() const { return false; }
};

class HalStorageStub {
 public:
  bool openFileForRead(const char*, const std::string&, HalFile&) { return false; }
  bool openFileForWrite(const char*, const std::string&, HalFile&) { return false; }
  bool exists(const char*) { return false; }
  bool remove(const char*) { return false; }
};
inline HalStorageStub Storage;
