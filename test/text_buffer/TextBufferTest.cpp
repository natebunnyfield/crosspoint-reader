#include <gtest/gtest.h>

#include <string>

#include "spike/TextBuffer.h"

namespace {

struct Buf {
  char storage[256] = {0};
  textbuf::TextBuffer tb{storage, sizeof(storage)};
  void type(const std::string& s) {
    for (char c : s) tb.insert(c);
  }
  std::string str() const { return std::string(tb.data(), tb.size()); }
};

}  // namespace

TEST(TextBuffer, TypeAppends) {
  Buf b;
  b.type("hello");
  EXPECT_EQ(b.str(), "hello");
  EXPECT_EQ(b.tb.cursor(), 5u);
}

TEST(TextBuffer, InsertAtCursorNotAtEnd) {
  Buf b;
  b.type("helo");
  b.tb.cursorLeft();  // between 'l' and 'o'
  b.tb.insert('l');
  EXPECT_EQ(b.str(), "hello");
}

TEST(TextBuffer, BackspaceDeletesBeforeCursor) {
  Buf b;
  b.type("hex");
  b.tb.backspace();
  EXPECT_EQ(b.str(), "he");
  b.tb.cursorLeft();
  b.tb.backspace();  // removes 'h'
  EXPECT_EQ(b.str(), "e");
  EXPECT_EQ(b.tb.cursor(), 0u);
}

TEST(TextBuffer, BackspaceAtStartIsNoOp) {
  Buf b;
  b.type("a");
  b.tb.cursorHome();
  EXPECT_FALSE(b.tb.backspace());
  EXPECT_EQ(b.str(), "a");
}

TEST(TextBuffer, DeleteRemovesAtCursor) {
  Buf b;
  b.type("abc");
  b.tb.cursorTo(0);
  EXPECT_TRUE(b.tb.del());
  EXPECT_EQ(b.str(), "bc");
  EXPECT_EQ(b.tb.cursor(), 0u);
}

TEST(TextBuffer, CursorClampsAtBothEnds) {
  Buf b;
  b.type("ab");
  b.tb.cursorRight();
  b.tb.cursorRight();
  EXPECT_EQ(b.tb.cursor(), 2u);
  b.tb.cursorLeft();
  b.tb.cursorLeft();
  b.tb.cursorLeft();
  EXPECT_EQ(b.tb.cursor(), 0u);
}

TEST(TextBuffer, HomeAndEndWorkPerLine) {
  Buf b;
  b.type("one\ntwo");
  b.tb.cursorHome();
  EXPECT_EQ(b.tb.cursor(), 4u);  // start of "two", not of the buffer
  b.tb.cursorEnd();
  EXPECT_EQ(b.tb.cursor(), 7u);
}

TEST(TextBuffer, UpDownKeepColumn) {
  Buf b;
  b.type("hello\nworld");
  b.tb.cursorTo(8);  // "wo|rld", column 2
  b.tb.cursorUp();
  EXPECT_EQ(b.tb.cursor(), 2u);  // "he|llo"
  b.tb.cursorDown();
  EXPECT_EQ(b.tb.cursor(), 8u);
}

// Moving up into a SHORTER line must clamp to that line's end, not overshoot
// into the following line.
TEST(TextBuffer, UpIntoShorterLineClamps) {
  Buf b;
  b.type("ab\nlongline");
  b.tb.cursorTo(10);  // deep into line 2
  b.tb.cursorUp();
  EXPECT_EQ(b.tb.cursor(), 2u);  // end of "ab"
}

TEST(TextBuffer, DownFromLastLineGoesToEnd) {
  Buf b;
  b.type("only");
  b.tb.cursorTo(1);
  b.tb.cursorDown();
  EXPECT_EQ(b.tb.cursor(), 4u);
}

TEST(TextBuffer, UpFromFirstLineGoesToStart) {
  Buf b;
  b.type("only");
  b.tb.cursorTo(3);
  b.tb.cursorUp();
  EXPECT_EQ(b.tb.cursor(), 0u);
}

// The cap must refuse rather than overrun — this is the C3, and the buffer is
// the activity's single allocation.
TEST(TextBuffer, RefusesWhenFull) {
  char small[4] = {0};
  textbuf::TextBuffer tb(small, sizeof(small));
  EXPECT_TRUE(tb.insert('a'));
  EXPECT_TRUE(tb.insert('b'));
  EXPECT_TRUE(tb.insert('c'));
  EXPECT_FALSE(tb.insert('d'));
  EXPECT_EQ(tb.size(), 3u);
}

TEST(TextBuffer, ClearResetsCursor) {
  Buf b;
  b.type("stuff");
  b.tb.clear();
  EXPECT_EQ(b.tb.size(), 0u);
  EXPECT_EQ(b.tb.cursor(), 0u);
}
