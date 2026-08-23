// Round-trip and refusal tests for the sparse ruby record
// (lib/Epub/Epub/blocks/RubySerialization.h, SECTION_FILE_VERSION 45).
//
// Every failure mode this guards is SILENT. A format that drops furigana still
// paginates, still renders, and still looks correct in English -- the loss is
// visible only in a Japanese book, which nobody opens between one refactor and
// the next. And the saving being chased here (13% of every section file) is
// itself invisible, so an encoding that quietly grew back to a string per word
// would never be noticed either.

#include <BufferedFile.h>
#include <Serialization.h>
#include <gtest/gtest.h>

#include "Epub/blocks/RubySerialization.h"

namespace {

std::vector<uint8_t> encode(const std::vector<std::string>& ruby, const uint16_t wordCount) {
  HalFile file;
  {
    serialization::BufferedFileWriter out(file, 1024);
    rubyserial::write(out, ruby, wordCount);
  }  // flushes
  return file.raw();
}

// Encode, then decode through the same HalFile the page loader uses.
bool roundTrip(const std::vector<std::string>& ruby, const uint16_t wordCount, std::vector<std::string>& decoded) {
  HalFile file;
  {
    serialization::BufferedFileWriter out(file, 1024);
    rubyserial::write(out, ruby, wordCount);
  }
  file.rewind();
  return rubyserial::read(file, wordCount, decoded);
}

// The furigana in these fixtures is real: 日本語 read にほんご, 漢字 read かんじ.
// Multibyte on both sides of the pairing, which is what makes an offset bug
// show up as mojibake instead of as a clean miss.
const std::string kNihongo = "にほんご";
const std::string kKanji = "かんじ";

}  // namespace

// ---------------------------------------------------------------------------
// The common case: a book with no ruby anywhere. This is the whole point.
// ---------------------------------------------------------------------------

TEST(RubySerialization, NoRubyCostsOneByteWhateverTheWordCount) {
  EXPECT_EQ(encode({}, 1).size(), 1u);
  EXPECT_EQ(encode({}, 12).size(), 1u);
  EXPECT_EQ(encode({}, 300).size(), 1u);
  // An all-empty vector sized to the words is the same record as no vector:
  // the layout engine hands one over for every line, ruby or not.
  EXPECT_EQ(encode(std::vector<std::string>(300), 300).size(), 1u);
}

TEST(RubySerialization, NoRubyIsCheaperThanTheStringPerWordItReplaces) {
  // v44 wrote a uint32 length for every word. That is the 13% of a section
  // file docs/performance-indexing-2026-08-23.md measured.
  constexpr uint16_t kWords = 300;
  EXPECT_EQ(encode({}, kWords).size(), 1u);
  EXPECT_EQ(static_cast<size_t>(kWords) * sizeof(uint32_t), 1200u);
}

TEST(RubySerialization, NoRubyDecodesToAnEmptyVectorNotWordCountEmptyStrings) {
  // TextBlock's in-RAM "no ruby" representation. Resizing to wordCount here
  // would cost 24 bytes per word of DRAM for the life of the resident page.
  std::vector<std::string> decoded{"stale", "values"};
  ASSERT_TRUE(roundTrip({}, 40, decoded));
  EXPECT_TRUE(decoded.empty());
}

TEST(RubySerialization, ZeroWordBlockRoundTrips) {
  std::vector<std::string> decoded;
  ASSERT_TRUE(roundTrip({}, 0, decoded));
  EXPECT_TRUE(decoded.empty());
}

// ---------------------------------------------------------------------------
// Ruby still works. A format change that silently drops furigana is worse than
// the 13% it saves.
// ---------------------------------------------------------------------------

TEST(RubySerialization, SingleAnnotationRoundTrips) {
  std::vector<std::string> ruby(3);
  ruby[1] = kNihongo;
  std::vector<std::string> decoded;
  ASSERT_TRUE(roundTrip(ruby, 3, decoded));
  ASSERT_EQ(decoded.size(), 3u);
  EXPECT_TRUE(decoded[0].empty());
  EXPECT_EQ(decoded[1], kNihongo);
  EXPECT_TRUE(decoded[2].empty());
}

TEST(RubySerialization, SparseAnnotationsKeepTheirWordIndices) {
  // The realistic shape: ruby attaches to a GROUP LEADER and the continuation
  // words carry nothing. Losing the index is the bug that would silently move
  // every annotation onto the wrong word.
  std::vector<std::string> ruby(8);
  ruby[0] = kKanji;
  ruby[5] = kNihongo;
  std::vector<std::string> decoded;
  ASSERT_TRUE(roundTrip(ruby, 8, decoded));
  ASSERT_EQ(decoded.size(), 8u);
  EXPECT_EQ(decoded[0], kKanji);
  EXPECT_EQ(decoded[5], kNihongo);
  for (size_t i : {1u, 2u, 3u, 4u, 6u, 7u}) EXPECT_TRUE(decoded[i].empty()) << "word " << i;
}

TEST(RubySerialization, EveryWordAnnotatedRoundTrips) {
  std::vector<std::string> ruby{"a", kKanji, "c"};
  std::vector<std::string> decoded;
  ASSERT_TRUE(roundTrip(ruby, 3, decoded));
  EXPECT_EQ(decoded, ruby);
}

TEST(RubySerialization, LastWordAnnotatedRoundTrips) {
  // Off-by-one at the far edge: an index bound written as `<=` would let this
  // through on write and refuse it on read, or vice versa.
  std::vector<std::string> ruby(4);
  ruby[3] = kNihongo;
  std::vector<std::string> decoded;
  ASSERT_TRUE(roundTrip(ruby, 4, decoded));
  ASSERT_EQ(decoded.size(), 4u);
  EXPECT_EQ(decoded[3], kNihongo);
}

TEST(RubySerialization, MultibyteAnnotationSurvivesByteForByte) {
  // Not a length check: a UTF-8 sequence that is truncated or re-split still
  // has a plausible length. Compare the bytes.
  const std::string mixed = "ニホンゴ　かんじ";  // ideographic space in the middle
  std::vector<std::string> ruby(2);
  ruby[1] = mixed;
  std::vector<std::string> decoded;
  ASSERT_TRUE(roundTrip(ruby, 2, decoded));
  ASSERT_EQ(decoded.size(), 2u);
  EXPECT_EQ(decoded[1], mixed);
  EXPECT_EQ(decoded[1].size(), mixed.size());
}

TEST(RubySerialization, AnnotationsPastTheWordCountAreDropped) {
  // The block can only address wordCount words; anything beyond is unreachable
  // on load, so writing it would produce a record the reader must refuse.
  std::vector<std::string> ruby(5);
  ruby[1] = kKanji;
  ruby[4] = kNihongo;
  std::vector<std::string> decoded;
  ASSERT_TRUE(roundTrip(ruby, 3, decoded));
  ASSERT_EQ(decoded.size(), 3u);
  EXPECT_EQ(decoded[1], kKanji);
}

TEST(RubySerialization, ARubyBlockStillCostsLessThanAStringPerWord) {
  std::vector<std::string> ruby(60);
  ruby[0] = kKanji;   // 9 bytes
  ruby[20] = kKanji;  // 9 bytes
  // 1 presence + 2 count + 2 x (2 index + 4 length + 9 text) = 33.
  EXPECT_EQ(encode(ruby, 60).size(), 33u);
  // v44: 60 x 4 length fields + 18 text bytes = 258.
  EXPECT_LT(encode(ruby, 60).size(), 60u * sizeof(uint32_t) + 18u);
}

// ---------------------------------------------------------------------------
// Refusals. A corrupt record must fail the block, not half-decode a page.
// ---------------------------------------------------------------------------

TEST(RubySerialization, RejectsAPresenceByteThatIsNeitherZeroNorOne) {
  HalFile file;
  const uint8_t junk = 0x7F;
  file.write(&junk, 1);
  file.rewind();
  std::vector<std::string> decoded;
  EXPECT_FALSE(rubyserial::read(file, 4, decoded));
}

TEST(RubySerialization, RejectsACountLargerThanTheWordCount) {
  std::vector<std::string> ruby(4);
  ruby[0] = kKanji;
  ruby[1] = kKanji;
  HalFile file;
  {
    serialization::BufferedFileWriter out(file, 1024);
    rubyserial::write(out, ruby, 4);
  }
  file.raw()[1] = 9;  // count low byte: 2 -> 9, past a 4-word block
  file.rewind();
  std::vector<std::string> decoded;
  EXPECT_FALSE(rubyserial::read(file, 4, decoded));
}

TEST(RubySerialization, RejectsAnIndexPastTheWordCount) {
  std::vector<std::string> ruby(4);
  ruby[1] = kKanji;
  HalFile file;
  {
    serialization::BufferedFileWriter out(file, 1024);
    rubyserial::write(out, ruby, 4);
  }
  file.raw()[3] = 40;  // first entry's index low byte: 1 -> 40
  file.rewind();
  std::vector<std::string> decoded;
  EXPECT_FALSE(rubyserial::read(file, 4, decoded));
}

TEST(RubySerialization, RejectsOutOfOrderIndices) {
  // Strictly increasing is what bounds the loop; a repeated or descending index
  // is a record no writer produces.
  std::vector<std::string> ruby(6);
  ruby[1] = kKanji;
  ruby[4] = kNihongo;
  HalFile file;
  {
    serialization::BufferedFileWriter out(file, 1024);
    rubyserial::write(out, ruby, 6);
  }
  file.raw()[3] = 5;  // first index 1 -> 5, so the second (4) now descends
  file.rewind();
  std::vector<std::string> decoded;
  EXPECT_FALSE(rubyserial::read(file, 6, decoded));
}

TEST(RubySerialization, RejectsATruncatedRecord) {
  // readString refuses a length that outruns the file and yields an empty
  // string; an empty annotation is never written, so that is the corrupt signal.
  std::vector<std::string> ruby(4);
  ruby[1] = kNihongo;
  HalFile file;
  {
    serialization::BufferedFileWriter out(file, 1024);
    rubyserial::write(out, ruby, 4);
  }
  file.raw().resize(file.raw().size() - 4);  // eat half the annotation
  file.rewind();
  std::vector<std::string> decoded;
  EXPECT_FALSE(rubyserial::read(file, 4, decoded));
}

TEST(RubySerialization, RejectsAPresentFlagWithNothingAfterIt) {
  HalFile file;
  const uint8_t present = 1;
  file.write(&present, 1);
  file.rewind();
  std::vector<std::string> decoded;
  EXPECT_FALSE(rubyserial::read(file, 4, decoded));
}

// ---------------------------------------------------------------------------
// hasAnnotations is the shared definition of "this block has ruby" -- the
// serializer's presence flag and TextBlock::hasRuby() both come from it, and
// they must not be able to disagree.
// ---------------------------------------------------------------------------

TEST(RubySerialization, HasAnnotationsAgreesWithThePresenceByte) {
  const std::vector<std::vector<std::string>> cases = {
      {}, std::vector<std::string>(5), {"", "", kKanji}, {kKanji}, {"", ""},
  };
  for (const auto& ruby : cases) {
    const auto bytes = encode(ruby, 8);
    EXPECT_EQ(bytes[0] == 1, rubyserial::hasAnnotations(ruby));
  }
}
