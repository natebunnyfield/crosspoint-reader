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

#include <EpdFontFamily.h>

#include <cstddef>

class GfxRenderer;

namespace mdrender {

// Advance of `piece` in `style`, including a trailing space.
//
// getTextWidth() does not count a TRAILING space, so measuring each styled span
// on its own loses the gap before the next one -- "Plain **bold** and" rendered
// as "Plainbold and". Measuring with a sentinel appended keeps the space's
// advance, then subtracts the sentinel back off. Measured with LibreFranklin
// 12: "ab" is 28, "ab " is 29, "ab  " is 34, so an interior space is 5 px and a
// trailing one counts 1.
int advanceOf(GfxRenderer& renderer, int fontId, const char* piece, EpdFontFamily::Style style);

// Draw one display line of markdown at `y`, with its block marker in the gutter
// at `originX` and its body indented from there.
//
// `indentStep` is the width of one hanging-indent step -- the editor passes its
// line height. Returns the x the body ended at, which the caller needs only if
// it is drawing something after the text (the editor's caret does not: it
// measures from the raw source instead, because that is what the typist is
// editing).
//
// `maxX` bounds the right edge: a span that would cross it is cut to what fits
// (on a UTF-8 boundary) and drawing stops. The editor pre-wraps its lines and
// never needs this, but the Editor Font specimen is one fixed string measured
// against five faces of different widths -- the widest overran the pane and the
// last word was sliced by the panel edge. Pass 0 for no bound.
int drawLine(GfxRenderer& renderer, int fontId, int indentStep, int originX, int y, const char* text, size_t len,
             int maxX = 0);

}  // namespace mdrender
