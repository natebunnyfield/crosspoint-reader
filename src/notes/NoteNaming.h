// Filenames for newly created notes: YYYYMMDDHHmmss.md.
//
// Pure so it can be unit-tested (test/note_naming). The caller supplies the
// clock reading and an "does this path exist" predicate; nothing here touches
// the RTC or the SD card.
#pragma once

#include <stdint.h>
#include <stdio.h>

#include <string>

namespace notenaming {

// "20260806T..." without separators, per the owner's format.
inline std::string formatStamp(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute,
                               uint8_t second) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%04u%02u%02u%02u%02u%02u", static_cast<unsigned>(year), static_cast<unsigned>(month),
           static_cast<unsigned>(day), static_cast<unsigned>(hour), static_cast<unsigned>(minute),
           static_cast<unsigned>(second));
  return buf;
}

// Create Note must produce a NEW note every run. A same-second double-tap, or a
// device with no synced clock (every stamp identical), must still get its own
// file rather than reopening someone else's — so collisions take a -1, -2 …
// suffix instead of silently overwriting.
template <typename ExistsFn>
inline std::string uniqueNotePath(const std::string& dir, const std::string& stamp, ExistsFn exists) {
  const std::string base = (dir == "/" ? std::string("/") : dir + "/") + stamp;
  std::string candidate = base + ".md";
  for (int n = 1; exists(candidate.c_str()) && n < 1000; ++n) {
    candidate = base + "-" + std::to_string(n) + ".md";
  }
  return candidate;
}

}  // namespace notenaming
