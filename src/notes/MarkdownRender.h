#pragma once

// Turning MarkdownSpans output into draw calls.
//
// Lifted out of NoteEditorActivity so the Editor Font picker can draw its
// specimen through the SAME code the editor draws with. A picker that
// reimplements the editor's styling is a picker that eventually lies about it:
// the whole point of previewing a writing face is seeing what a heading, a
// bullet marker and a bold run will really look like while typing.
//
// mdspans::analyze() decides WHAT the styles are and is host-tested on its own
// (test/markdown_spans). This decides only WHERE they land.
//
// MEASURE THROUGH measureLine(), NEVER through getTextWidth() ON THE SOURCE.
// drawLine consumes the markers, so "**bold**" occupies four characters in the
// source and none on screen. Any caller that measures the raw source and draws
// through drawLine disagrees with itself by four pixels-worth-of-characters per
// bold run (two per italic or code run) -- which is a visible gap after every
// styled word, and was a real bug in both the editor's wrap and its caret.

#include <EpdFontFamily.h>

#include <cstddef>
#include <cstdint>

class GfxRenderer;

namespace mdrender {

// One visual line's byte range in the source line wrapLine() was given.
struct Fragment {
  uint16_t start;
  uint16_t end;  // exclusive
};

// Pen advance of `piece` in `style` -- how far drawText moves, spaces at either
// end included.
//
// It must NOT be getTextWidth(): that returns the ink bounding box, so a
// trailing space is worth about a pixel instead of a space (measured with
// LibreFranklin 12: "ab" 28, "ab " 29, "ab  " 34) and every span boundary loses
// its gap. This used to work around that by appending a '|' sentinel and
// subtracting it back off, which recovered the space but bought two new errors
// from the same bounding box: a negative left bearing on the first glyph and
// ink overhang on the last both inflate it. Measured on iA Writer Quattro 12,
// the editor's default face, italic "fix" came out 2 px wide and italic "j"
// 4 px wide -- a gap after every italic run, which is the smaller half of the
// spacing this file's comment header describes.
//
// getTextAdvanceX is the renderer's own answer to the same question, and it
// snaps its fixed-point arithmetic exactly the way drawText does.
int advanceOf(GfxRenderer& renderer, int fontId, const char* piece, EpdFontFamily::Style style);

// Width the line OCCUPIES once drawn: the hanging indent plus the advance of
// every styled span, markers excluded. Equal to what drawLine() returns when
// called with originX 0, by construction -- both walk the same spans in the
// same order -- so a caller that wraps on this wraps on what it will draw.
int measureLine(GfxRenderer& renderer, int fontId, int indentStep, const char* text, size_t len);

// Pen x, relative to originX, at which SOURCE byte `column` is drawn.
//
// A column that lands on a consumed marker ("**", the "# " of a heading) has no
// pixel of its own; it clamps to the start of the run that marker opens, or to
// the end of the run it closes. That is the honest answer for a caret: it sits
// against the text the typist is editing rather than floating in the gap where
// the markers used to be.
int caretX(GfxRenderer& renderer, int fontId, int indentStep, const char* text, size_t len, size_t column);

// Greedy wrap of one markdown SOURCE line into fragments that each render
// within `maxWidth`.
//
// Fragments are byte ranges into `text`, ascending and non-overlapping; the
// space a break was taken at is dropped. Every fragment is re-analysed when it
// is drawn, so breaks are chosen at spaces in the source and a fragment always
// carries its own markers. A styled run wider than the whole line is the one
// case that degrades: the tail fragment opens with an unclosed marker, which
// mdspans deliberately leaves literal, so it renders as typed instead of
// corrupting the rest of the line.
//
// A blank line yields one empty fragment, so callers that lay out one row per
// fragment keep the paragraph break. Returns the fragment count, capped at
// maxFragments. Measurement is measureLine on the candidate substring -- the
// same call the caller will draw with -- so wrap and draw cannot disagree.
size_t wrapLine(GfxRenderer& renderer, int fontId, int indentStep, const char* text, size_t len, int maxWidth,
                Fragment* out, size_t maxFragments);

// Draw one display line of markdown at `y`, with its block marker in the gutter
// at `originX` and its body indented from there.
//
// `indentStep` is the width of one hanging-indent step -- the editor passes its
// line height. Returns the x the body ended at, which the caller needs only if
// it is drawing something after the text.
//
// `maxX` bounds the right edge: a span that would cross it is cut to what fits
// (on a UTF-8 boundary) and drawing stops. Callers that pre-wrapped through
// wrapLine() never need this; the Editor Font specimen is one fixed string
// measured against five faces of different widths -- the widest overran the
// pane and the last word was sliced by the panel edge. Pass 0 for no bound.
int drawLine(GfxRenderer& renderer, int fontId, int indentStep, int originX, int y, const char* text, size_t len,
             int maxX = 0);

}  // namespace mdrender
