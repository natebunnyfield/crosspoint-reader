#pragma once

// Which part of a one-line text field's text is on screen (owner 2026-11-26,
// "as before, text field needs to be one line", extending "do not wrap long
// secret inputs or outputs"). A field never wraps; when its text is wider than
// the field, a window of it is shown, with an ellipsis at each end that is cut
// off, and the window always holds the cursor:
//
//   - the TAIL, with a leading ellipsis, whenever the cursor is inside it
//     (typing at the end, the common case);
//   - otherwise a window that starts at the cursor, so the cursor sits at its
//     left edge, with an ellipsis at whichever ends are cut off (cursor mode
//     walked toward the start, a tap that placed the cursor early).
//
// Pure and stateless: the render and the tap that maps a point back to a byte
// ask the same question with the same inputs and get the same window, so the
// two cannot drift apart. Every boundary is a UTF-8 character boundary of the
// SHOWN string (a password's mask is one '*' per byte, so every masked byte is
// a boundary, and the one revealed character stays whole).
//
// `measure(start, end)` is the pixel advance of shown[start, end).

#include <cstddef>
#include <string>

namespace singleline {

struct Window {
  size_t start = 0;
  size_t end = 0;
  bool leftCut = false;   // text hidden before `start`: draw a leading ellipsis
  bool rightCut = false;  // text hidden after `end`: draw a trailing ellipsis
};

inline size_t nextBoundary(const std::string& s, size_t i) {
  if (i >= s.size()) return s.size();
  ++i;
  while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
  return i;
}

inline size_t prevBoundary(const std::string& s, size_t i) {
  if (i == 0) return 0;
  if (i > s.size()) i = s.size();
  --i;
  while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
  return i;
}

// `width`: the field's text width. `caretReserve`: room kept after the last
// character for the caret when the window is the tail. `ellipsisWidth`: the
// advance of the ellipsis glyph.
template <class Measure>
Window fit(const std::string& s, size_t cursor, const int width, const int caretReserve, const int ellipsisWidth,
           Measure measure) {
  const size_t len = s.size();
  if (cursor > len) cursor = len;
  Window w;
  w.end = len;
  if (measure(static_cast<size_t>(0), len) <= width) return w;  // it all fits: no window

  // The tail: the earliest boundary whose run to the end fits beside a
  // leading ellipsis, with the caret's room after it.
  const int tailRoom = width - ellipsisWidth - caretReserve;
  size_t tailStart = len;
  while (tailStart > 0) {
    const size_t prev = prevBoundary(s, tailStart);
    if (measure(prev, len) > tailRoom) break;
    tailStart = prev;
  }
  if (cursor >= tailStart) {
    w.start = tailStart;
    w.leftCut = tailStart > 0;
    return w;
  }

  // The cursor is left of the tail: the window starts at it, and runs as far
  // right as fits before a trailing ellipsis (none if it reaches the end).
  size_t start = cursor;
  while (start > 0 && (static_cast<unsigned char>(s[start]) & 0xC0) == 0x80) --start;
  w.start = start;
  w.leftCut = start > 0;
  const int room = width - (w.leftCut ? ellipsisWidth : 0) - ellipsisWidth;
  size_t end = start;
  while (end < len) {
    const size_t next = nextBoundary(s, end);
    if (measure(start, next) > room) break;
    end = next;
  }
  w.end = end;
  w.rightCut = end < len;
  return w;
}

}  // namespace singleline
