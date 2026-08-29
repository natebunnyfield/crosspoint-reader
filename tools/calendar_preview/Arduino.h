#pragma once
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
inline unsigned long millis() {
  using namespace std::chrono;
  static auto t0 = steady_clock::now();
  return (unsigned long)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}
inline unsigned long micros() { return millis() * 1000UL; }
inline void delay(unsigned long) {}
struct EspShim {
  uint32_t getFreeHeap() const { return 200000; }
  uint32_t getMaxAllocHeap() const { return 100000; }
  void restart() { abort(); }
};
static EspShim ESP;
