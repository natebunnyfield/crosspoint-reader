#pragma once

// HOW MUCH TEXT WAS ON THE PAGE — the denominator for the reading-usage
// ledger (crosspoint-simulator/docs/reading-experiments.md).
//
// A page-turn rate is meaningless on its own the moment font size is one of
// the things being compared: a bigger face puts less on a page, so it turns
// more pages per minute while reading the same book at the same speed. The
// experiment needs an amount of TEXT per page, and this is where it is
// counted.
//
// WHY CHARACTERS ARE THE DENOMINATOR AND WORDS ARE NOT.
//
// The stored tokens come from the parsed text, not the layout, so a token
// count is nearly layout-independent — nearly. A word the line breaker SPLITS
// across two lines is two tokens, and which breaker runs (`hyphenationEnabled`,
// lib/Epub/Epub/LineBreakMode.h) is itself one of the settings the experiment
// varies. So a word count is biased in a direction that lines up exactly with
// an arm, which is the worst possible shape for a confound: it would look like
// an effect.
//
// Characters have no such bias. A split word contributes the same characters
// either way, provided the soft hyphens the splitter inserts are not counted —
// and they are excluded here for the same reason ReadAloudCapture.h strips
// them from spoken text: the renderer never draws them, so they are not on the
// page. Codepoints rather than bytes, so a page of accented text is not scored
// as 1.2 pages of English.
//
// `words` is published anyway, because it is free and because it is what a
// person reads a report in. The report's rates are per character; the word
// column is labelled as approximate. See §3 of the design doc.
//
// PURE, AND HOST-TESTED (test/page_text_metrics), for the reason every
// decision like this one is written as a pure header: every failure mode is a
// wrong NUMBER in a log file. Nothing renders differently, nothing crashes,
// no compiler sees it, and the mistake is discovered a year later as a
// conclusion that was never true. A miscount of 5% is larger than any
// typography effect this experiment could hope to detect.
//
// BLANKNESS IS NOT REDEFINED HERE. readaloud::tokenIsBlank() is the one rule
// for "does this token carry ink", and it is reused rather than restated — two
// definitions of what a word is, sixty lines apart, is the drift this repo
// keeps writing tests to prevent.

#include <cstddef>
#include <cstdint>

#include "ReadAloudCapture.h"  // readaloud::tokenIsBlank

namespace pagemetrics {

// U+00AD SOFT HYPHEN, as the two UTF-8 bytes the tokens actually carry.
inline constexpr unsigned char kSoftHyphen0 = 0xC2;
inline constexpr unsigned char kSoftHyphen1 = 0xAD;

struct Counts {
  uint32_t words = 0;  // non-blank tokens; see the bias note above
  uint32_t chars = 0;  // codepoints, soft hyphens excluded
  uint32_t lines = 0;  // lines that carried at least one non-blank token
};

// Codepoints in one NUL-terminated token, skipping soft hyphens.
//
// A UTF-8 continuation byte is 10xxxxxx; every other byte starts a codepoint.
// That is the whole rule, and it is correct for malformed input too — a stray
// continuation byte is simply not counted, rather than running off the end
// looking for the sequence it belongs to.
inline uint32_t codepointsOf(const char* t) {
  if (!t) return 0;
  uint32_t n = 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(t); *p;) {
    if (p[0] == kSoftHyphen0 && p[1] == kSoftHyphen1) {
      p += 2;
      continue;
    }
    if ((p[0] & 0xC0) != 0x80) n++;
    p++;
  }
  return n;
}

// Accumulate a page one line at a time.
//
// A COUNTER RATHER THAN A FUNCTION over the whole page, because the caller
// walks a display list of heterogeneous elements (PageLine, images, rules) and
// this header must not know what a PageElement is — it has to stay compilable
// on a host with no renderer, which is what makes it testable at all.
class Counter {
 public:
  // Start a line. Safe to call without ever adding a token: an empty line
  // contributes nothing, which is why `lines` is incremented at endLine() and
  // only when ink was seen.
  void beginLine() { lineHadInk_ = false; }

  void addToken(const char* text) {
    if (!text || readaloud::tokenIsBlank(text)) return;
    counts_.words++;
    counts_.chars += codepointsOf(text);
    lineHadInk_ = true;
  }

  void endLine() {
    if (lineHadInk_) counts_.lines++;
    lineHadInk_ = false;
  }

  const Counts& result() const { return counts_; }

 private:
  Counts counts_{};
  bool lineHadInk_ = false;
};

}  // namespace pagemetrics
