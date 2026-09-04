// A crafted .cpfont from the card must not make the renderer read past a
// glyph's bitmap allocation. A Wi-Fi peer can PUT a font onto the card over
// WebDAV, and the glyph decode walks width*height pixels out of a
// dataLength-sized buffer without consulting dataLength: a glyph declaring
// 255x255 in 1 byte over-read ~16 KB (ASan, crafted-input hunt 2026-09-04,
// B-045). The build-time verify_compression.py never sees such a file.
//
// The pin is functional and discriminates against the pre-fix tree: before
// the geometry check, load() succeeded AND getGlyph returned a non-null glyph
// carrying the bad geometry (the crash was one decode later); after it, the
// glyph is refused. Built under ASan in the sanitizer job, a regression also
// trips the overflow directly.
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "EpdFont.h"
#include "SdCardFont.h"

namespace {

// EpdGlyph is 16 bytes: w:u8 h:u8 advX:u16 left:i16 top:i16 dataLength:u16
// pad:2 dataOffset:u32. The header is 32 bytes, the style TOC 32.
std::vector<uint8_t> craftedFont(uint8_t w, uint8_t h, uint16_t dataLength) {
  std::vector<uint8_t> f;
  auto u16 = [&](uint16_t v) {
    f.push_back(v & 0xff);
    f.push_back(v >> 8);
  };
  auto u32 = [&](uint32_t v) {
    for (int i = 0; i < 4; i++) f.push_back((v >> (8 * i)) & 0xff);
  };
  // Header (32)
  const char* magic = "CPFONT\0\0";
  for (int i = 0; i < 8; i++) f.push_back(static_cast<uint8_t>(magic[i]));
  u16(4);          // version
  u16(1);          // flags: bit0 => is2Bit
  f.push_back(1);  // styleCount
  while (f.size() < 32) f.push_back(0);
  // Style TOC (32)
  const size_t tocStart = f.size();
  f.push_back(0);  // styleId
  f.push_back(0);
  f.push_back(0);
  f.push_back(0);  // pad to offset 4
  // rewrite: TOC layout is styleId:u8 then intervalCount@4
  f.resize(tocStart);
  f.push_back(0);  // styleId (u8) at 0
  f.push_back(0);
  f.push_back(0);
  f.push_back(0);                   // pad to 4
  u32(1);                           // intervalCount @4
  u32(1);                           // glyphCount @8
  f.push_back(90);                  // advanceY @12
  u16(80);                          // ascender @13 (i16)
  u16(static_cast<uint16_t>(-20));  // descender @15
  u16(0);                           // kernLeftEntryCount @17
  u16(0);                           // kernRightEntryCount @19
  f.push_back(0);                   // kernLeftClassCount @21
  f.push_back(0);                   // kernRightClassCount @22
  f.push_back(0);                   // ligaturePairCount @23
  u32(64);                          // dataOffset @24 (header+toc)
  while (f.size() < 64) f.push_back(0);
  // interval[0]: first,last,offset (u32 each) -> U+0041..U+0041 at glyph 0
  u32(0x41);
  u32(0x41);
  u32(0);
  // glyph
  f.push_back(w);
  f.push_back(h);
  u16(0);  // advanceX
  u16(0);  // left (i16)
  u16(0);  // top (i16)
  u16(dataLength);
  f.push_back(0);
  f.push_back(0);  // pad
  u32(0);          // dataOffset (relative to bitmap base)
  // bitmap
  for (uint16_t i = 0; i < dataLength; i++) f.push_back(0xff);
  return f;
}

std::string writeTempFont(const std::vector<uint8_t>& bytes, const std::string& name) {
  const char* base = std::getenv("CROSSPOINT_TEST_SD");
  std::string root = (base && *base) ? base : std::string("./fs_");
  const std::string path = root + name;
  FILE* fp = std::fopen(path.c_str(), "wb");
  if (fp) {
    std::fwrite(bytes.data(), 1, bytes.size(), fp);
    std::fclose(fp);
  }
  return name;  // SdCardFont paths are card-relative
}

TEST(MalformedFont, AGlyphDeclaringMoreThanItsBitmapHoldsIsRefused) {
  const auto bytes = craftedFont(/*w=*/255, /*h=*/255, /*dataLength=*/1);
  const std::string name = writeTempFont(bytes, "/malformed_big.cpfont");

  SdCardFont font;
  ASSERT_TRUE(font.load(name.c_str())) << "the header is well-formed; only the glyph is a lie";
  EpdFont* ef = font.getEpdFont(0);
  ASSERT_NE(ef, nullptr);
  // Pre-fix this returned a non-null glyph carrying 255x255/1, and the next
  // decode read ~16 KB past a 1-byte buffer.
  EXPECT_EQ(ef->getGlyph(0x41), nullptr)
      << "a glyph whose bitmap is too small for its geometry must be refused, not decoded";
}

TEST(MalformedFont, AWellProportionedGlyphStillLoads) {
  // 2x2 at 2-bit needs ceil(4*2/8)=1 byte; one byte is enough.
  const auto bytes = craftedFont(/*w=*/2, /*h=*/2, /*dataLength=*/1);
  const std::string name = writeTempFont(bytes, "/malformed_ok.cpfont");
  SdCardFont font;
  ASSERT_TRUE(font.load(name.c_str()));
  EpdFont* ef = font.getEpdFont(0);
  ASSERT_NE(ef, nullptr);
  EXPECT_NE(ef->getGlyph(0x41), nullptr) << "a glyph that fits its bitmap must still load";
}

}  // namespace
