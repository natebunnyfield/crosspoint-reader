#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Labels for the cells of a flattened table.
//
// The reader has no grid layout: a <table> is flattened into one text block per
// cell. That is only readable if each cell says what it is, and until this
// existed each one was prefixed with its coordinates ("Tab Row 3, Cell 2:"),
// which told the reader nothing a page number wouldn't. Instead the parser
// captures the <thead> row as column names and the first cell of each body row
// as that row's name, and labels every cell from those.
//
// Split out of ChapterHtmlSlimParser because the parser needs a GfxRenderer, a
// font id and a live Epub to construct, so none of it is reachable from a host
// test — while this, the part that decides what the reader actually sees, is
// pure and testable on its own.
namespace TableCellLabel {

// Longest label kept from any one cell. Headers are short by nature, and a
// runaway one (a whole sentence in a <th>) would otherwise be repeated in front
// of every cell in its column.
inline constexpr size_t kMaxLabelLen = 32;

// Columns whose headers are retained. Past this a table is not being read as a
// grid anyway, and this vector is per-parse state on a device with ~400KB of
// RAM.
inline constexpr size_t kMaxCols = 8;

// Trim, collapse internal whitespace runs to one space, drop a trailing colon
// (headers that already read "Space:" must not become "Space:: 1"), and clip to
// kMaxLabelLen without splitting a UTF-8 sequence.
std::string normalize(const std::string& raw);

// The prefix a data cell is emitted with. `colIndex` is 1-based; `colCount` is
// the number of cells in the table's first row, which the parser counts.
//
//   two columns   ""              name then value already reads as a pair
//   first column  "Space "        the cell's own text completes it: "Space 1"
//   later columns "1 · Grassland: "
//
// Every piece is optional. A table with no <thead> still labels by row ("1: "),
// one with headers but no usable row name labels by column ("Grassland: "), and
// a table with neither gets "" — never a coordinate.
std::string forCell(const std::vector<std::string>& colLabels, const std::string& rowLabel, size_t colIndex,
                    size_t colCount);

}  // namespace TableCellLabel
