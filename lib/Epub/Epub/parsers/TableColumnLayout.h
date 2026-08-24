#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class GfxRenderer;

// Column geometry for a table rendered as columns rather than flattened into
// per-cell paragraphs (T-012, ruled 2026-08-18: columns + a header rule).
//
// Everything here is measurement and arithmetic, deliberately separated from
// ChapterHtmlSlimParser's emission: the decision "can this table be columns at
// all, and where do they sit" is the part that can be wrong in a way no
// screenshot shows, so it is a pure function with its own tests. The parser
// owns the emission, because that needs its whole text-block machinery.
namespace tablecolumns {

// One styled fragment of a cell. Cells keep their bold/italic runs so a
// buffered table renders the same emphasis the flattened path would have.
struct Run {
  std::string text;
  bool bold = false;
  bool italic = false;
};

using Cell = std::vector<Run>;
using Row = std::vector<Cell>;

// Ceilings, all of them about RAM rather than taste. A table past any of them
// is flattened instead -- the old path, which streams and buffers nothing.
inline constexpr size_t kMaxColumns = 5;
inline constexpr size_t kMaxRows = 64;
inline constexpr size_t kMaxBufferedBytes = 3072;

// A table narrower than this per column is unreadable at reading sizes; the
// flattened form is genuinely better than four-character-wide columns.
inline constexpr int kMinColumnWidth = 48;

// Measured width is what the text needs EXACTLY, and the line breaker works in
// whole words against the width it is given -- so a column sized to its widest
// cell can still wrap that cell, which is what shipped in the first run of this
// code ("Col 3" broke after "Col"). Two pixels of slack per column costs
// nothing and removes the whole class.
inline constexpr int kColumnSlack = 2;

struct Plan {
  bool usable = false;  // false -> caller must flatten
  size_t columnCount = 0;
  int x[kMaxColumns] = {};  // left edge of each column, in viewport pixels
  int w[kMaxColumns] = {};  // width available to the cell's text
  bool rightAlign[kMaxColumns] = {};
  int gutter = 0;
};

// Concatenated plain text of a cell, runs joined in order.
std::string cellText(const Cell& cell);

// True when every non-empty cell in the column reads as a figure: digits with
// optional separators, sign and currency. Those columns are set flush right so
// the digits stack; anything else stays flush left.
bool columnIsNumeric(const std::vector<Row>& rows, size_t col);

// Widths are natural (widest cell) where they fit. When they do not, only the
// WIDEST column is squeezed and allowed to wrap -- a wrapped year or a wrapped
// figure is worse than a wrapped phrase, and squeezing everything proportionally
// makes every column slightly too narrow instead of one column honestly narrow.
//
// measureText is injected so the tests can run without a renderer; the parser
// passes a lambda over GfxRenderer::getTextWidth.
Plan planColumns(const std::vector<Row>& rows, int viewportWidth, int spaceWidth,
                 int (*measureText)(void* ctx, const char* text, bool bold), void* ctx);

// Where one column of a table row starts, vertically.
//
// Columns share their row's top edge -- that is what makes them columns -- and
// the parser gets that by REWINDING its page cursor to `rowTop` before each
// cell. That rewind is the only way this layout can print text over text, so
// the rule lives here as a pure function with its preconditions named, rather
// than as an unwritten assumption at the rewind site. planColumns above makes
// horizontal overlap impossible (the widestWord floor); this makes VERTICAL
// overlap impossible, which is the half that was missing.
//
// The rewind is sound exactly when BOTH hold:
//
//   rowTopIsClear       Nothing outside the row loop is still owed a place at
//                       rowTop. A <caption> -- or any text inside <table> but
//                       outside a cell -- sits UNLAID in the streaming path's
//                       pending block and has consumed no vertical space; the
//                       first cell's block flush lays it out AT rowTop, and
//                       every later column then rewinds on top of it. That is
//                       the bug reported 2026-08-23: a three-column phrasebook
//                       whose caption, "English" and "Say it" printed on one
//                       line with "Catalan" pushed to the next.
//   samePageAsRowStart  The row is still on the page it began on. A cell that
//                       overflows and completes a page leaves rowTop naming a y
//                       on a page that is already finished, and rewinding to it
//                       prints this column over whatever spilled onto the new
//                       one.
//
// When either fails the answer is `cursorY` -- carry on below whatever is
// already there. A column out of line reads badly; a column printed on top of
// another cannot be read at all.
inline int columnStartY(const int rowTop, const int cursorY, const bool rowTopIsClear,
                        const bool samePageAsRowStart) {
  return (rowTopIsClear && samePageAsRowStart) ? rowTop : cursorY;
}

}  // namespace tablecolumns
