#pragma once

#include <Serialization.h>

#include <cstdint>
#include <string>
#include <vector>

// On-disk encoding for a TextBlock's ruby annotations.
//
// WHY THIS IS NOT A STRING PER WORD ANY MORE
//
// It was, until SECTION_FILE_VERSION 45. `TextBlock::serialize` wrote a
// `writeString` for every word, and `writeString` is a uint32 length followed
// by the bytes -- so a book with no furigana in it anywhere paid 4 bytes per
// word, forever, to record "nothing here". Measured on
// docs/performance-indexing-2026-08-23.md's `giant.epub`: 1.0 MB of a 7.84 MB
// section file, 13% of the bytes written during indexing and read back on
// every page turn. Ruby is a CJK feature; the overwhelming majority of books
// on this device have none.
//
// THE ENCODING, AND WHY THIS ONE
//
//   uint8_t  present      0 = this block has no ruby; NOTHING follows
//   -- present == 1 only:
//   uint16_t count        annotated words, 1..wordCount
//   count x:
//     uint16_t index      word index, strictly increasing, < wordCount
//     writeString text    never empty
//
// Two properties were wanted and both are load-bearing:
//
// 1. **Zero bytes for the empty case, near enough.** One byte per block, not
//    per word. A page holds ~25-30 blocks and hundreds of words, so the cost
//    of the flag rounds to nothing against the 4-bytes-per-word it replaces.
//
// 2. **Sparse, not dense, WITHIN a ruby block.** A presence flag alone would
//    have meant "then write all wordCount strings", and even in a Japanese
//    book most words on a ruby line carry no annotation of their own: ruby
//    attaches to the GROUP LEADER and the continuation words are marked with
//    EpdFontFamily::RUBY_CONTINUE and hold an empty string (TextBlock.cpp's
//    render(), the `RUBY_CONTINUE` scan). Index-prefixing the entries costs 2
//    bytes on a word that has ruby and saves 4 on every word that does not.
//
// The reader rejects rather than repairs: a count past wordCount, an index out
// of order or out of range, or an empty annotation means the file is corrupt,
// and a rejected block fails the block decode, which is already the path that
// discards and rebuilds the section cache. Repairing silently would leave a
// half-decoded page on screen with no way to tell.
//
// Templated on the stream so the same definition serves BufferedFileWriter
// (the build), HalFile (page load) and std::ostream/std::istream (the host
// test in test/ruby_serialization). serialization:: overloads all three.
namespace rubyserial {

// True when any entry is a non-empty annotation. `rubyTexts` is allowed to be
// empty (the no-ruby representation TextBlock keeps in RAM) or shorter than
// the word count.
inline bool hasAnnotations(const std::vector<std::string>& rubyTexts) {
  for (const auto& rt : rubyTexts) {
    if (!rt.empty()) return true;
  }
  return false;
}

// Entries at or beyond `wordCount` are not written: the block only knows how
// to address wordCount words, so anything past that is unreachable on load.
inline uint16_t annotationCount(const std::vector<std::string>& rubyTexts, const uint16_t wordCount) {
  uint16_t n = 0;
  const size_t limit = rubyTexts.size() < wordCount ? rubyTexts.size() : wordCount;
  for (size_t i = 0; i < limit; i++) {
    if (!rubyTexts[i].empty()) n++;
  }
  return n;
}

template <typename Writer>
void write(Writer& out, const std::vector<std::string>& rubyTexts, const uint16_t wordCount) {
  const uint16_t count = annotationCount(rubyTexts, wordCount);
  serialization::writePod(out, static_cast<uint8_t>(count > 0 ? 1 : 0));
  if (count == 0) return;
  serialization::writePod(out, count);
  const size_t limit = rubyTexts.size() < wordCount ? rubyTexts.size() : wordCount;
  for (size_t i = 0; i < limit; i++) {
    if (rubyTexts[i].empty()) continue;
    serialization::writePod(out, static_cast<uint16_t>(i));
    serialization::writeString(out, rubyTexts[i]);
  }
}

// Fills `rubyTexts` with wordCount entries when the block carries ruby, and
// leaves it EMPTY when it does not -- that is TextBlock's in-RAM
// representation of "no ruby" (hasRuby() false, every other reader guarded by
// `i < rubyTexts.size()`), and materializing wordCount empty std::strings for
// every line of a Latin book is the DRAM cost the lazy path in
// TextBlock::deserialize was written to avoid.
//
// Returns false on a malformed record; the caller must fail the block.
template <typename Reader>
bool read(Reader& in, const uint16_t wordCount, std::vector<std::string>& rubyTexts) {
  rubyTexts.clear();
  uint8_t present = 0;
  serialization::readPod(in, present);
  if (present == 0) return true;
  if (present != 1) return false;

  uint16_t count = 0;
  serialization::readPod(in, count);
  if (count == 0 || count > wordCount) return false;

  std::string scratch;
  int32_t previous = -1;
  for (uint16_t n = 0; n < count; n++) {
    uint16_t index = 0;
    serialization::readPod(in, index);
    if (index >= wordCount || static_cast<int32_t>(index) <= previous) return false;
    previous = index;
    serialization::readString(in, scratch);
    // readString yields an empty string when the length field outruns the file,
    // which is the corrupt case; a genuinely empty annotation is never written.
    if (scratch.empty()) return false;
    if (rubyTexts.empty()) rubyTexts.resize(wordCount);
    rubyTexts[index] = std::move(scratch);
  }
  return true;
}

}  // namespace rubyserial
