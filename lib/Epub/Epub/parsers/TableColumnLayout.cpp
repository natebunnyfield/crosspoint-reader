#include "TableColumnLayout.h"

#include <algorithm>
#include <cctype>

namespace tablecolumns {

std::string cellText(const Cell& cell) {
  size_t total = 0;
  for (const Run& r : cell) total += r.text.size();
  std::string out;
  out.reserve(total);
  for (const Run& r : cell) out += r.text;
  return out;
}

bool columnIsNumeric(const std::vector<Row>& rows, const size_t col) {
  bool sawOne = false;
  // Row 0 is the header; "Days" is not a figure and must not veto the column.
  for (size_t r = 1; r < rows.size(); r++) {
    if (col >= rows[r].size()) continue;
    const std::string text = cellText(rows[r][col]);
    bool sawDigit = false;
    for (const unsigned char c : text) {
      if (std::isdigit(c)) {
        sawDigit = true;
        continue;
      }
      if (std::isspace(c) || c == ',' || c == '.' || c == '-' || c == '+' || c == '%' || c == '$') continue;
      return false;  // a letter anywhere: not a figures column
    }
    if (sawDigit) sawOne = true;
  }
  return sawOne;
}

Plan planColumns(const std::vector<Row>& rows, const int viewportWidth, const int spaceWidth,
                 int (*measureText)(void*, const char*, bool), void* ctx) {
  Plan plan;
  if (rows.size() < 2 || viewportWidth <= 0 || !measureText) return plan;
  if (rows.size() > kMaxRows) return plan;

  size_t columnCount = 0;
  for (const Row& row : rows) columnCount = std::max(columnCount, row.size());
  if (columnCount < 2 || columnCount > kMaxColumns) return plan;

  // A ragged table -- some row with fewer cells than the header -- is usually a
  // layout table or one using colspan, which is not handled. Columns would put
  // that row's text under the wrong heading, which is worse than flattening it.
  for (const Row& row : rows) {
    if (row.size() != columnCount) return plan;
  }

  int natural[kMaxColumns] = {};
  for (size_t c = 0; c < columnCount; c++) {
    for (size_t r = 0; r < rows.size(); r++) {
      const std::string text = cellText(rows[r][c]);
      if (text.empty()) continue;
      const int w = measureText(ctx, text.c_str(), r == 0);  // header row is bold, and bold is wider
      natural[c] = std::max(natural[c], w + kColumnSlack);
    }
  }

  const int gutter = std::max(spaceWidth * 3, 12);
  const int avail = viewportWidth - gutter * static_cast<int>(columnCount - 1);
  if (avail <= 0) return plan;

  int total = 0;
  for (size_t c = 0; c < columnCount; c++) total += natural[c];

  int width[kMaxColumns] = {};
  if (total <= avail) {
    for (size_t c = 0; c < columnCount; c++) width[c] = natural[c];
  } else {
    // Squeeze the widest column only. Repeat, because taking the overflow out of
    // the widest can make a different column the widest.
    for (size_t c = 0; c < columnCount; c++) width[c] = natural[c];
    int over = total - avail;
    while (over > 0) {
      size_t widest = 0;
      for (size_t c = 1; c < columnCount; c++) {
        if (width[c] > width[widest]) widest = c;
      }
      const int room = width[widest] - kMinColumnWidth;
      if (room <= 0) return plan;  // nothing left to give: flatten instead
      const int take = std::min(room, over);
      width[widest] -= take;
      over -= take;
    }
  }

  for (size_t c = 0; c < columnCount; c++) {
    if (width[c] < kMinColumnWidth) return plan;
  }

  plan.usable = true;
  plan.columnCount = columnCount;
  plan.gutter = gutter;
  int x = 0;
  for (size_t c = 0; c < columnCount; c++) {
    plan.x[c] = x;
    plan.w[c] = width[c];
    plan.rightAlign[c] = columnIsNumeric(rows, c);
    x += width[c] + gutter;
  }
  return plan;
}

}  // namespace tablecolumns
