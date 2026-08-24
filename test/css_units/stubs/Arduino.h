// The three Arduino/ESP facilities CssParser.cpp reaches for, and nothing else.
// The real headers pull in ESP-IDF, which is why the CSS engine had no host
// coverage of its declaration path at all before this suite.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

// The parser refuses work when free heap runs low. A host has no such ceiling,
// so report a number comfortably above every threshold in CssParser.cpp; the
// low-heap refusals are a separate concern with their own note.
inline uint32_t ESP_getFreeHeap() { return 4u * 1024u * 1024u; }

struct EspStub {
  static uint32_t getFreeHeap() { return ESP_getFreeHeap(); }
  static uint32_t getMaxAllocHeap() { return ESP_getFreeHeap(); }
};
inline EspStub ESP;
