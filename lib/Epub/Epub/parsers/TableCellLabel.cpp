#include "TableCellLabel.h"

namespace TableCellLabel {

namespace {

bool isSpace(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

// A UTF-8 continuation byte is 10xxxxxx. Clipping in the middle of a sequence
// would leave a broken glyph in front of every cell in the column.
bool isContinuation(const char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

}  // namespace

std::string normalize(const std::string& raw) {
  std::string out;
  out.reserve(raw.size() < kMaxLabelLen ? raw.size() : kMaxLabelLen);

  bool pendingSpace = false;
  for (const char c : raw) {
    if (isSpace(c)) {
      // Leading whitespace never opens a label; interior runs collapse to one
      // space, emitted only once something follows.
      pendingSpace = !out.empty();
      continue;
    }
    if (pendingSpace) {
      out.push_back(' ');
      pendingSpace = false;
    }
    out.push_back(c);
  }

  // "Space:" would otherwise build "Space:: 1".
  while (!out.empty() && (out.back() == ':' || isSpace(out.back()))) {
    out.pop_back();
  }

  if (out.size() > kMaxLabelLen) {
    size_t cut = kMaxLabelLen;
    while (cut > 0 && isContinuation(out[cut])) {
      cut--;
    }
    out.resize(cut);
    // Clipping mid-word can strand a trailing space.
    while (!out.empty() && isSpace(out.back())) {
      out.pop_back();
    }
  }

  return out;
}

std::string forCell(const std::vector<std::string>& colLabels, const std::string& rowLabel, const size_t colIndex,
                    const size_t colCount) {
  // Two columns are a name and its value. Prefixing either just says the name
  // twice; the parser emits the first cell bold so the pair reads as a list.
  if (colCount == 2) {
    return "";
  }

  // The first cell is the row's name, so its own text finishes the label:
  // "Space " + "1" reads "Space 1".
  if (colIndex <= 1) {
    if (!colLabels.empty() && !colLabels[0].empty()) {
      return colLabels[0] + " ";
    }
    return "";
  }

  const std::string col = (colIndex <= colLabels.size()) ? colLabels[colIndex - 1] : std::string();

  if (!rowLabel.empty() && !col.empty()) {
    return rowLabel + " \xc2\xb7 " + col + ": ";  // U+00B7 MIDDLE DOT
  }
  if (!col.empty()) {
    return col + ": ";
  }
  if (!rowLabel.empty()) {
    return rowLabel + ": ";
  }
  return "";
}

}  // namespace TableCellLabel
