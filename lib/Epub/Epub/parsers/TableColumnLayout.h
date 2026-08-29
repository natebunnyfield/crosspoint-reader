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
inline int columnStartY(const int rowTop, const int cursorY, const bool rowTopIsClear, const bool samePageAsRowStart) {
  return (rowTopIsClear && samePageAsRowStart) ? rowTop : cursorY;
}

// How many body rows a table's header row must keep with it (owner 2026-08-26,
// from two screenshots of his own reading: "don't split up table header or
// caption from rest (when possible). intact is best"). The reported page ended
// with the caption, the header row and its rule, then half a page of nothing,
// and the first body row opened the page after -- so the reader turned the page
// carrying three column names in their head.
//
// Two is the typographic convention and it is the same number the prose
// widow/orphan control already uses (keep-2/2,
// ChapterHtmlSlimParser::flushPendingLines).
inline constexpr int kKeepBodyRows = 2;

// Whether the caller must complete the page BEFORE emitting a table's leading
// group, so that group travels intact onto the next one.
//
// The group is, in order: an optional LEAD (a <caption>, or whatever short
// block is still unlaid immediately above the table -- `leadHeight`, 0 when
// there is none or when it is too tall to be worth carrying), the HEADER row
// with its rule, and the first `kKeepBodyRows` body rows.
//
// "(when possible)" is the whole of the owner's ruling that this function
// encodes. A table can be taller than a page, and a header that cannot be kept
// with any row at all must NOT be allowed to blank the page it is on and then
// strand itself again on the next one. So the constraint DEGRADES and then
// YIELDS, in this order:
//
//   header + lead + 2 rows   the wanted group
//   header + lead + 1 row    a header with one row under it still reads
//   nothing                  it does not fit a whole empty page either; place
//                            it where it stands, which is today's behaviour
//
// and at every rung the answer is "break" only when breaking actually HELPS --
// the group must fail to fit here AND fit on an empty page. That pair of tests
// is what makes this terminate: a break can never leave the group in a worse
// place than it started.
//
// `hasContent` is the same statement one step earlier -- an empty page has
// nothing above the header to strand it against, and breaking it would publish
// a blank one. The parser checks that itself before it does any measuring, so
// in the shipping build this parameter always arrives true; it is the tests'
// lever on the base case and it belongs here rather than only at the call site,
// because a second caller would otherwise have to remember it.
//
// Heights are whole vertical consumption including gaps, rules and any
// line-grid rounding: the caller measures, this decides. Passing them slightly
// HIGH is safe (an early break costs some white space at the foot of a page);
// passing them low is not, because the row loop's own per-row break would then
// split the group after all.
bool breakBeforeHeaderKeep(int cursorY, int freshPageStartY, int viewportHeight, bool hasContent, int leadHeight,
                           int headerHeight, const int* bodyRowHeights, int bodyRowCount);

}  // namespace tablecolumns
