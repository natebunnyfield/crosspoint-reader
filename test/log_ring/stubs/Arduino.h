#pragma once
// Just enough Arduino for lib/Logging/Logging.cpp to compile on the host.
// Logging.h binds `logSerial` to a HardwareSerial reference and derives
// MySerialImpl from Print; none of that is under test here, so it all swallows
// output. Only the RTC ring behaviour matters.
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) { return 1; }
  virtual size_t write(const uint8_t*, size_t size) { return size; }
  virtual void flush() {}
};

class HardwareSerial : public Print {
 public:
  void begin(unsigned long) {}
  operator bool() const { return false; }
  void print(const char*) {}
  void printf(const char*, ...) {}
  void setTxTimeoutMs(uint32_t) {}
};
inline HardwareSerial Serial;

// Advances on every call so each log line gets a distinct timestamp -- which is
// the case the collapsing has to cope with, since a retry storm repeats the same
// text at different milliseconds.
inline unsigned long millis() {
  static unsigned long t = 0;
  return t += 7;
}
