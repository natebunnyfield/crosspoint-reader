// Host Print stub — Arduino's output-stream base class.
//
// Reached only through lib/Epub/Epub.h, which EpubReaderMenuActivity.h includes
// for the book handle it never dereferences in the input path
// (readItemContentsToStream takes a Print&). Nothing in this suite writes to a
// Print, so the pure virtual is left pure: an accidental use fails to compile.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* buffer, size_t size) {
    size_t n = 0;
    while (size--) n += write(*buffer++);
    return n;
  }
  virtual void flush() {}

  size_t print(const char* s) { return write(reinterpret_cast<const uint8_t*>(s), strlen(s)); }
};
