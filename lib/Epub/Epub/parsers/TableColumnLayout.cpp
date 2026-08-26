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
  // The widest single WORD a column contains. This is the real floor: the line
  // breaker cannot split a word, so a column narrower than its longest word does
  // not wrap -- it OVERFLOWS, and the overflow lands on top of the next column.
  // That shipped once, and the render is unmistakable: five columns of a wide
  // table printed over each other. A generic minimum width cannot catch it,
  // because the offending word is different in every table.
  int widestWord[kMaxColumns] = {};
  for (size_t c = 0; c < columnCount; c++) {
    for (size_t r = 0; r < rows.size(); r++) {
      const std::string text = cellText(rows[r][c]);
      if (text.empty()) continue;
      const bool bold = (r == 0);  // the header row is bold, and bold is wider
      natural[c] = std::max(natural[c], measureText(ctx, text.c_str(), bold) + kColumnSlack);
      size_t start = 0;
      while (start <= text.size()) {
        const size_t space = text.find(' ', start);
        const std::string word = text.substr(start, space == std::string::npos ? std::string::npos : space - start);
        if (!word.empty()) {
          widestWord[c] = std::max(widestWord[c], measureText(ctx, word.c_str(), bold) + kColumnSlack);
        }
        if (space == std::string::npos) break;
        start = space + 1;
      }
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
      // The widest column WITH ROOM LEFT, not simply the widest. Giving up the
      // moment the widest column reaches its floor refuses tables that fit
      // comfortably once a different column gives ground -- which is how a
      // five-column table that fits the rotated page was still being sent to
      // the fallback.
      size_t widest = columnCount;
      int widestWidth = 0;
      for (size_t c = 0; c < columnCount; c++) {
        const int floor = std::max(kMinColumnWidth, widestWord[c]);
        if (width[c] <= floor) continue;
        if (widest == columnCount || width[c] > widestWidth) {
          widest = c;
          widestWidth = width[c];
        }
      }
      if (widest == columnCount) return plan;  // every column is at its floor
      const int floor = std::max(kMinColumnWidth, widestWord[widest]);
      const int take = std::min(width[widest] - floor, over);
      width[widest] -= take;
      over -= take;
    }
  }

  for (size_t c = 0; c < columnCount; c++) {
    if (width[c] < kMinColumnWidth) return plan;
    if (width[c] < widestWord[c]) return plan;  // would overflow into its neighbour
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

bool breakBeforeHeaderKeep(const int cursorY, const int freshPageStartY, const int viewportHeight,
                           const bool hasContent, const int leadHeight, const int headerHeight,
                           const int* bodyRowHeights, const int bodyRowCount) {
  // An empty page has nothing above the header to strand it against, and
  // breaking it would publish a blank page. This is also the base case that
  // makes the caller's second, post-lead call a no-op after the first one has
  // already broken.
  if (!hasContent) return false;

  const int maxKeep = bodyRowCount < kKeepBodyRows ? bodyRowCount : kKeepBodyRows;
  // A table with no body rows at all still has a header worth keeping with its
  // caption, so the ladder's bottom rung is 0 rows in that case and 1 otherwise
  // -- a header alone on a page is only acceptable when there is nothing that
  // could have followed it.
  const int minKeep = bodyRowCount > 0 ? 1 : 0;

  int group = leadHeight + headerHeight;
  for (int k = 0; k < maxKeep; k++) group += bodyRowHeights[k];

  for (int k = maxKeep; k >= minKeep; k--) {
    if (cursorY + group <= viewportHeight) return false;        // intact where it stands
    if (freshPageStartY + group <= viewportHeight) return true;  // breaking helps
    if (k > minKeep) group -= bodyRowHeights[k - 1];             // ask for one row less
  }
  // Not even the smallest group fits an empty page: the constraint yields and
  // the table paginates as it always did.
  return false;
}

}  // namespace tablecolumns
