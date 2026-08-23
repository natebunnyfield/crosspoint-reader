// In-memory HalFile, just enough of the real one for the section page stream.
//
// The ruby record is written through serialization::BufferedFileWriter and read
// back through the HalFile overloads, so this stub exists to let the test drive
// the PRODUCTION instantiations rather than a std::stringstream lookalike. Only
// the calls those two paths make are here: write/position for the writer, and
// read/seek/position/size for readString's bounds check.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

class HalFile {
  std::vector<uint8_t> bytes;
  size_t cursor = 0;

 public:
  size_t write(const uint8_t* src, const size_t len) {
    if (cursor + len > bytes.size()) bytes.resize(cursor + len);
    memcpy(bytes.data() + cursor, src, len);
    cursor += len;
    return len;
  }
  size_t write(const void* src, const size_t len) { return write(static_cast<const uint8_t*>(src), len); }

  int read(uint8_t* dst, const size_t len) {
    const size_t avail = bytes.size() - (cursor < bytes.size() ? cursor : bytes.size());
    const size_t n = len < avail ? len : avail;
    if (n > 0) memcpy(dst, bytes.data() + cursor, n);
    cursor += n;
    return static_cast<int>(n);
  }
  int read(void* dst, const size_t len) { return read(static_cast<uint8_t*>(dst), len); }

  size_t position() const { return cursor; }
  size_t size() const { return bytes.size(); }
  bool seek(const size_t target) {
    if (target > bytes.size()) return false;
    cursor = target;
    return true;
  }

  // Test-only: rewind for the read pass, and reach in to corrupt a byte.
  void rewind() { cursor = 0; }
  std::vector<uint8_t>& raw() { return bytes; }
};
