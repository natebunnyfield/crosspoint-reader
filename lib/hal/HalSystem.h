#pragma once

#include <cstdint>
#include <string>

namespace HalSystem {
struct StackFrame {
  uint32_t sp;
  uint32_t spp[8];
};

void begin();

// Dump panic info to SD card if necessary
void checkPanic();
void clearPanic();

/// Record a heap sample into RTC memory, for the NEXT crash report to print.
///
/// The device crashes in daily use on heap exhaustion, and until now nothing
/// recorded how the heap got there: the periodic MEM line in main.cpp is gated
/// on `if (Serial && ...)`, so it exists only with a USB cable attached -- and a
/// crash report is precisely the channel you use when you do not have one.
///
/// This costs a handful of RTC_NOINIT bytes rather than a line of the 16-line
/// log ring, so it cannot be pushed out by a retry storm. Call it periodically
/// from the main loop; the cost is three ESP heap queries.
void recordHeapSample();

std::string getPanicInfo(bool full = false);
bool isRebootFromPanic();
}  // namespace HalSystem
