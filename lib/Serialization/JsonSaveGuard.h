#pragma once

#include <ArduinoJson.h>

#include <cstddef>

// May these serialized bytes replace a store's file on the card?
//
// writeDocToFile() replaces the file atomically, so whatever it is handed
// becomes the store. Two failures used to reach it unnoticed, both under heap
// pressure and both turning good settings into a key-less file (fork survey #16,
// from endqwerty d60b5534d0):
//   * the JsonDocument ran out of pool while toJson() filled it -- ArduinoJson
//     then drops members silently and only overflowed() says so;
//   * the String could not grow while serializeJson() wrote into it, so the text
//     is cut short (measureJson() is what it should have been).
// Either way the save is refused and the old file stays.
namespace jsonsave {

inline bool serializedIsComplete(const JsonDocument& doc, const size_t written, const size_t stringLength) {
  if (doc.overflowed()) return false;
  const size_t expected = measureJson(doc);
  return expected > 0 && written == expected && stringLength == expected;
}

}  // namespace jsonsave
