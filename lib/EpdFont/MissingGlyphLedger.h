#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>

// Which of a book's characters the reading font has no shape for.
//
// EpdFont::getGlyph has substituted for a missing codepoint since B-009: U+FFFD
// if the face carries one, else '?'. That keeps the metrics honest, and it is
// why sweep item #38's "draws NOTHING" is only true of a face carrying neither
// -- but it also means the reader is shown a plain question mark where a
// character was, which reads as something the author wrote. Nothing counted
// them and nothing said so.
//
// This is the counting half. It is a LEDGER and not a callback because the
// substitution happens in lib/EpdFont, three libraries below the one that owns
// book notes, and inverting that dependency for a feature that records a uint16
// is not worth a header.
//
// DISTINCT codepoints, not occurrences: a page repaints, and an occurrence
// count would climb every time the reader turned back to the same page. The set
// is a small open-addressed table -- a full one stops recording, so the figure
// is a FLOOR and the note says "at least".
//
// ARMED, and disarmed by default. Every UI screen draws through the same
// renderer, and a filename or a model's answer carrying an emoji is not a fact
// about the book. Only the chapter parse arms it.
//
// NOT synchronised, deliberately, and the same reasoning as booknotes::Notes.
// The section build and the render task both reach getGlyph (the reader paints
// page N while page N+1 paginates), so a task switch can land between the
// "is this slot free" read and the write that claims it. On a single-core
// ESP32-C3 with aligned uint32 stores nothing tears; what can happen is a count
// off by one, in EITHER direction -- two tasks claiming one slot for the same
// codepoint counts it twice, and one overwriting the other's entry loses a
// codepoint until it is met again. Both need the two tasks inside the same few
// instructions for the same character, and the figure is already a floor for a
// much larger reason (the table saturates). A mutex here would sit inside the
// per-codepoint measure loop to protect a statistic.
namespace missingglyphs {

class Ledger {
 public:
  void arm() { armed = true; }
  void disarm() { armed = false; }
  bool isArmed() const { return armed; }

  // Called from the substitution site. Must stay cheap: it sits inside the
  // per-codepoint measure loop, and the common case is a font that covers the
  // text, where this is one branch on `armed`.
  void note(const uint32_t cp) {
    if (!armed || cp == 0) return;
    if (distinctCount >= SLOTS) {
      saturated = true;
      return;
    }
    size_t i = static_cast<size_t>(cp * 2654435761u) % SLOTS;  // Knuth's multiplicative hash
    for (size_t probe = 0; probe < SLOTS; ++probe) {
      if (slots[i] == cp) return;  // already counted
      if (slots[i] == 0) {
        slots[i] = cp;
        distinctCount++;
        return;
      }
      i = (i + 1) % SLOTS;
    }
  }

  uint16_t distinct() const { return distinctCount; }
  bool isSaturated() const { return saturated; }

  // Not "clear on arm": the count accumulates across the chapters paginated so
  // far, exactly like the layout-scope notes it feeds. booknotes::Notes owns
  // both lifetimes so there is one place that can get it wrong.
  void reset() {
    std::fill(std::begin(slots), std::end(slots), 0);
    distinctCount = 0;
    saturated = false;
  }

 private:
  // 64 slots is 256 bytes of DRAM and a load factor no worse than 1.0 by
  // construction. A book with more than 64 distinct uncoverable characters is
  // in a script this font does not have, which the note already says.
  static constexpr size_t SLOTS = 64;
  uint32_t slots[SLOTS] = {0};  // 0 is "empty"; U+0000 is not a codepoint that reaches here
  uint16_t distinctCount = 0;
  bool saturated = false;
  bool armed = false;
};

inline Ledger& current() {
  static Ledger instance;
  return instance;
}

}  // namespace missingglyphs
