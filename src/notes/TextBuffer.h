// Editable text buffer with a cursor, sized for the C3.
//
// Flat array + memmove on insert, NOT a gap buffer. At an 8 KB cap a memmove is
// a few microseconds — far below the 570 ms e-ink refresh that dominates every
// keystroke — and it costs no extra RAM, which a gap buffer's spare capacity
// would. The complexity of a gap buffer buys nothing at this size.
//
// Storage is caller-owned so the activity keeps its single makeUniqueNoThrow
// allocation and this stays host-testable.
#pragma once

#include <stddef.h>
#include <string.h>

namespace textbuf {

class TextBuffer {
 public:
  TextBuffer(char* storage, size_t capacity) : buf_(storage), cap_(capacity) {}

  const char* data() const { return buf_; }
  size_t size() const { return len_; }
  size_t cursor() const { return cursor_; }
  bool empty() const { return len_ == 0; }

  void clear() {
    len_ = 0;
    cursor_ = 0;
  }

  // Returns false when the buffer is full — the caller should surface that
  // rather than silently dropping keystrokes.
  bool insert(char c) {
    if (len_ + 1 >= cap_) return false;
    if (cursor_ < len_) memmove(buf_ + cursor_ + 1, buf_ + cursor_, len_ - cursor_);
    buf_[cursor_++] = c;
    ++len_;
    return true;
  }

  // Backspace: deletes BEFORE the cursor, like every editor.
  bool backspace() {
    if (cursor_ == 0) return false;
    if (cursor_ < len_) memmove(buf_ + cursor_ - 1, buf_ + cursor_, len_ - cursor_);
    --cursor_;
    --len_;
    return true;
  }

  // Delete: removes AT the cursor.
  bool del() {
    if (cursor_ >= len_) return false;
    memmove(buf_ + cursor_, buf_ + cursor_ + 1, len_ - cursor_ - 1);
    --len_;
    return true;
  }

  void cursorLeft() {
    if (cursor_ > 0) --cursor_;
  }
  void cursorRight() {
    if (cursor_ < len_) ++cursor_;
  }
  void cursorHome() {  // start of the current line
    while (cursor_ > 0 && buf_[cursor_ - 1] != '\n') --cursor_;
  }
  void cursorEnd() {  // end of the current line
    while (cursor_ < len_ && buf_[cursor_] != '\n') ++cursor_;
  }
  void cursorTo(size_t pos) { cursor_ = pos > len_ ? len_ : pos; }

  // Move a whole display line up/down, keeping the column where possible.
  // Operates on HARD lines; soft-wrap is the renderer's business.
  void cursorUp() {
    const size_t lineStart = startOfLine(cursor_);
    if (lineStart == 0) {
      cursor_ = 0;
      return;
    }
    const size_t col = cursor_ - lineStart;
    const size_t prevStart = startOfLine(lineStart - 1);
    const size_t prevLen = lineStart - 1 - prevStart;
    cursor_ = prevStart + (col < prevLen ? col : prevLen);
  }

  void cursorDown() {
    const size_t lineStart = startOfLine(cursor_);
    const size_t col = cursor_ - lineStart;
    size_t nextStart = cursor_;
    while (nextStart < len_ && buf_[nextStart] != '\n') ++nextStart;
    if (nextStart >= len_) {
      cursor_ = len_;
      return;
    }
    ++nextStart;  // past the newline
    size_t nextEnd = nextStart;
    while (nextEnd < len_ && buf_[nextEnd] != '\n') ++nextEnd;
    const size_t nextLen = nextEnd - nextStart;
    cursor_ = nextStart + (col < nextLen ? col : nextLen);
  }

  size_t startOfLine(size_t pos) const {
    while (pos > 0 && buf_[pos - 1] != '\n') --pos;
    return pos;
  }

 private:
  char* buf_;
  size_t cap_;
  size_t len_ = 0;
  size_t cursor_ = 0;
};

}  // namespace textbuf
