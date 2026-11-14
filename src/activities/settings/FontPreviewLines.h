#pragma once

// How the Reader Font preview spaces its specimen lines. Pure, so the host test
// (test/font_preview_lines) can hold it without a renderer.
//
// THE BUG THIS EXISTS FOR (2026-11-13): the preview stepped its lines by the
// font's ASCENDER + 2 px (getTextHeight() returns the ascender), so the step had
// no room for the descenders of one line or the accents of the next. In a face
// whose accented capitals rise well above its ascender, the tilde on Ñ and the
// acutes on Á and É ran into the descenders of the line above, at every size.
// The reader never did this: it advances by the font's own line height
// (getLineHeight(), advanceY), and the preview now does the same, so the
// specimen is set the way a page of the book is.
//
// A line occupies its top (y, as drawText takes it) down to the foot of its
// descenders: ascender + descender. That INK height, not the step, is what has
// to fit above the label, and what the LAST line of a centered block adds.

namespace fontpreview {

struct LineMetrics {
  int step;       // line top to next line top: the font's advanceY
  int inkHeight;  // line top to the foot of its descenders
};

// `descender` is a depth (positive). A font that reports no advanceY falls back
// to its ink height, so lines can still never overlap.
inline LineMetrics lineMetrics(int ascender, int descender, int advanceY) {
  const int ink = ascender + (descender > 0 ? descender : 0);
  return {advanceY > 0 ? advanceY : ink, ink};
}

// How many lines fit in `innerHeight`: the first takes its ink height, every
// further one a whole step. Always at least one, as before.
inline int maxLines(int innerHeight, const LineMetrics& m) {
  if (m.step <= 0 || innerHeight < m.inkHeight) return 1;
  return 1 + (innerHeight - m.inkHeight) / m.step;
}

// Height of a block whose lines advanced by a total of `advanced` (each line's
// gap-before plus one step, summed): the last line contributes its ink height,
// not a full step.
inline int blockHeight(int advanced, int lineCount, const LineMetrics& m) {
  if (lineCount <= 0) return 0;
  const int h = advanced - m.step + m.inkHeight;
  return h > 0 ? h : 0;
}

}  // namespace fontpreview
