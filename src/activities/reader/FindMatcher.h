#pragma once

// Whole-book Find: the matcher. Pure and header-only so it is host-testable on
// its own, in the same pattern as ReadAloudCapture.h, whose page text it reads.
//
// WHAT IT MATCHES. A simple substring, no regex, over each page's text AS
// RENDERED: the string readaloud::buildCapture builds from a laid-out page,
// which has already stripped soft hyphens and rejoined words the layout split
// with a line-break hyphen. On top of that text:
//   * case-insensitive, through the hyphenation tables' own folding
//     (toLowerLatin covers ASCII, Latin-1 Supplement and Latin Extended-A;
//     toLowerCyrillic the basic Cyrillic block). No other script folds.
//   * whitespace-normalized: every run of whitespace (NBSP and the Unicode
//     spaces included) is one space, in the page text and in the query alike,
//     and the query is trimmed.
//   * HYPHENS ARE IGNORED on both sides (U+002D, U+2010, U+2011, and the soft
//     hyphen). buildCapture's line-break join drops a line-final '-' whether
//     the layout inserted it or the book did, so "well-known" broken at its own
//     hyphen reads back as "wellknown"; ignoring hyphens is what lets the query
//     "well-known" find it. Upstream PR #2451 made the same choice for the same
//     reason. The cost is that "co-op" also matches "coop".
//
// HOW IT STREAMS. Pages are fed one at a time, in order, through a window of the
// last N folded codepoints, so a phrase that runs off one page onto the next is
// found without holding two pages: the window IS the carried tail. A page
// boundary is a word boundary (one space) unless the previous page ended in a
// joinable line-break hyphen, in which case the word continues -- the same rule
// buildCapture applies between two lines, carried across the page edge.
//
// WHERE A MATCH IS. Every codepoint in the window remembers where it came from,
// as (page, byte offset into that page's text). A match is reported at its
// FIRST codepoint's origin, and only when that origin lies inside the bounds the
// caller set: strictly after `lower`, and at or before `upper`. That is how the
// caller searches forward from the reading position, wraps from the start of
// the chapter back up to it, and asks for the NEXT instance on the same page.

#include <Epub/hyphenation/HyphenationCommon.h>
#include <Utf8.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace findtext {

// Longest query, in folded codepoints after whitespace collapse. The text entry
// caps the query at this many BYTES, so an ASCII query can never exceed it and a
// longer multi-byte one never gets close. It also sizes the window: 128 x 8 bytes.
constexpr size_t kMaxQueryCodepoints = 128;

// Offset sentinel: "before the first byte of the page". A lower bound at
// (page, kPageStart) accepts everything on that page.
constexpr int32_t kPageStart = -1;

// A position in one chapter's stream of pages.
struct TextPos {
  uint16_t page = 0;
  int32_t offset = kPageStart;  // byte offset into that page's capture text
};

inline bool operator<(const TextPos& a, const TextPos& b) {
  return a.page != b.page ? a.page < b.page : a.offset < b.offset;
}
inline bool operator<=(const TextPos& a, const TextPos& b) { return !(b < a); }

inline bool isFindSpace(const uint32_t cp) {
  return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0x00A0 || (cp >= 0x2000 && cp <= 0x200A) ||
         cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

// Codepoints that never take part in a match: hyphens (see the header comment)
// and the zero-width format characters a book may carry inside a word.
inline bool isFindIgnorable(const uint32_t cp) {
  return cp == '-' || cp == 0x00AD || cp == 0x2010 || cp == 0x2011 || cp == 0x200B || cp == 0x200C || cp == 0x200D ||
         cp == 0x2060 || cp == 0xFEFF;
}

inline uint32_t foldCodepoint(const uint32_t cp) { return toLowerCyrillic(toLowerLatin(cp)); }

// True when `raw` holds at least one codepoint a Query keeps (not a space, not
// ignorable). A cheap pre-check for callers that cannot afford a Query on the
// stack (it is 512 bytes).
inline bool hasSearchableText(const std::string& raw) {
  const auto* p = reinterpret_cast<const unsigned char*>(raw.c_str());
  while (*p) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (!isFindSpace(cp) && !isFindIgnorable(cp)) return true;
  }
  return false;
}

// The query, folded and normalized once.
class Query {
  uint32_t cps[kMaxQueryCodepoints] = {};
  size_t len = 0;

 public:
  // False when nothing searchable is left (empty, or only spaces and hyphens),
  // or when the query is longer than kMaxQueryCodepoints.
  bool set(const std::string& raw) {
    len = 0;
    bool pendingSpace = false;
    const auto* p = reinterpret_cast<const unsigned char*>(raw.c_str());
    while (*p) {
      const uint32_t cp = utf8NextCodepoint(&p);
      if (isFindIgnorable(cp)) continue;
      if (isFindSpace(cp)) {
        pendingSpace = len > 0;  // leading spaces are dropped
        continue;
      }
      // A space still to write plus this codepoint must both fit.
      if (len + (pendingSpace ? 2 : 1) > kMaxQueryCodepoints) {
        len = 0;
        return false;
      }
      if (pendingSpace) {
        cps[len++] = ' ';
        pendingSpace = false;
      }
      cps[len++] = foldCodepoint(cp);
    }
    return len > 0;  // trailing spaces were never written
  }
  size_t size() const { return len; }
  uint32_t at(const size_t i) const { return cps[i]; }
};

class Matcher {
  const Query* query = nullptr;
  // The window: a ring of the last kMaxQueryCodepoints folded codepoints.
  uint32_t ring[kMaxQueryCodepoints] = {};
  TextPos origin[kMaxQueryCodepoints] = {};
  size_t head = 0;   // next write slot
  size_t count = 0;  // valid entries, <= kMaxQueryCodepoints
  bool lastWasSpace = true;
  bool anyText = false;      // the stream has emitted something since resetStream()
  bool joinPending = false;  // the previous page ended in a joinable line-break hyphen
  bool hasLower = false;
  bool hasUpper = false;
  TextPos lower;
  TextPos upper;

  bool accept(const TextPos& p) const { return (!hasLower || lower < p) && (!hasUpper || p <= upper); }

  // Append one folded codepoint; true when the window now ends in an accepted match.
  bool push(const uint32_t cp, const TextPos& at, TextPos& hit) {
    ring[head] = cp;
    origin[head] = at;
    head = (head + 1) % kMaxQueryCodepoints;
    if (count < kMaxQueryCodepoints) count++;
    const size_t n = query->size();
    if (count < n || cp != query->at(n - 1)) return false;
    const size_t start = (head + kMaxQueryCodepoints - n) % kMaxQueryCodepoints;
    for (size_t i = 0; i < n; i++) {
      if (ring[(start + i) % kMaxQueryCodepoints] != query->at(i)) return false;
    }
    if (!accept(origin[start])) return false;
    hit = origin[start];
    return true;
  }

  bool pushSpace(const TextPos& at, TextPos& hit) {
    if (lastWasSpace) return false;
    lastWasSpace = true;
    return push(' ', at, hit);
  }

 public:
  void setQuery(const Query& q) { query = &q; }

  // Start a new stream: a chapter boundary is a hard break, nothing carries.
  void resetStream() {
    head = 0;
    count = 0;
    lastWasSpace = true;
    anyText = false;
    joinPending = false;
  }

  // Bounds for the current stream. A match is accepted only when its first
  // codepoint lies strictly after *lowerExclusive and at or before *upperInclusive.
  void setBounds(const TextPos* lowerExclusive, const TextPos* upperInclusive) {
    hasLower = lowerExclusive != nullptr;
    hasUpper = upperInclusive != nullptr;
    if (hasLower) lower = *lowerExclusive;
    if (hasUpper) upper = *upperInclusive;
  }

  // Feed one page's capture text. `barrierBefore`: a non-text element (image,
  // rule) opens the page, which stops a line-break hyphen join reaching across it.
  // `joinableHyphenAtEnd`: the text ends in a line-break hyphen with no non-text
  // element after it, so the page's last word may continue on the next page.
  // Returns true at the first accepted match, with its start in `hit`.
  bool feedPage(const uint16_t page, const std::string& text, const bool barrierBefore, const bool joinableHyphenAtEnd,
                TextPos& hit) {
    if (!query || query->size() == 0) return false;
    const TextPos pageStart{page, 0};
    if (text.empty()) {
      // A page with no text (an image) is a word break and ends any join.
      joinPending = false;
      return anyText && pushSpace(pageStart, hit);
    }
    // Page boundary: a word break, unless the last word was hyphen-split across it.
    if (anyText && !(joinPending && !barrierBefore) && pushSpace(pageStart, hit)) return true;
    joinPending = joinableHyphenAtEnd;
    const auto* base = reinterpret_cast<const unsigned char*>(text.c_str());
    const auto* p = base;
    while (*p) {
      const TextPos at{page, static_cast<int32_t>(p - base)};
      const uint32_t cp = utf8NextCodepoint(&p);
      if (isFindIgnorable(cp)) continue;
      if (isFindSpace(cp)) {
        if (pushSpace(at, hit)) return true;
        continue;
      }
      lastWasSpace = false;
      anyText = true;
      if (push(foldCodepoint(cp), at, hit)) return true;
    }
    return false;
  }
};

}  // namespace findtext
