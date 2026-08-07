#pragma once
#include <HalStorage.h>

#include <iostream>

namespace serialization {
template <typename T>
void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
void writePod(HalFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
void readPod(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
}

template <typename T>
void readPod(HalFile& file, T& value) {
  file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T));
}

inline void writeString(std::ostream& os, const std::string& s) {
  const uint32_t len = s.size();
  writePod(os, len);
  os.write(s.data(), len);
}

inline void writeString(HalFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

// Both readString overloads size an allocation from a number that came out of a
// file, so both bound it against the bytes actually left to read. On a device
// with 380 KB and no PSRAM, a corrupted cache header claiming a 4 GB string is
// not a bad read -- it is an abort. `len` is also initialised, because readPod
// returns void: a short read leaves it holding whatever was on the stack.
//
// A refusal yields an EMPTY string rather than an error, which every caller
// already handles: these are cache reads, and an empty field fails the cache's
// own validation and rebuilds it.

inline void readString(std::istream& is, std::string& s) {
  s.clear();
  uint32_t len = 0;
  readPod(is, len);
  if (!is.good()) return;
  if (len != 0) {
    const std::streampos here = is.tellg();
    if (here < 0) return;
    is.seekg(0, std::ios::end);
    const std::streampos end = is.tellg();
    is.seekg(here);
    if (end < here || static_cast<uint64_t>(end - here) < len) return;
  }
  s.resize(len);
  if (len != 0) is.read(&s[0], len);
}

inline void readString(HalFile& file, std::string& s) {
  s.clear();
  uint32_t len = 0;
  readPod(file, len);
  if (len != 0) {
    const size_t total = file.size();
    const size_t here = file.position();
    if (here > total || total - here < len) return;
  }
  s.resize(len);
  if (len != 0) file.read(&s[0], len);
}
}  // namespace serialization
