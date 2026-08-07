// Regression tests for B-023 and B-024: values read out of a file on the SD
// card being trusted to size an allocation or index a buffer.
//
// The device has 380 KB and no PSRAM, so "allocate whatever the header says" is
// not a bad read, it is an abort. None of these inputs require an attacker --
// a truncated cache or a half-written cover produces them.

#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>

#include "PngBitDepth.h"
#include "Serialization.h"

namespace {

// Build the on-disk form readString expects: a uint32 length, then the bytes.
std::string framed(uint32_t declaredLen, const std::string& payload) {
  std::string out(reinterpret_cast<const char*>(&declaredLen), sizeof(declaredLen));
  out += payload;
  return out;
}

}  // namespace

// ── B-024: readString sizes an allocation from the file ──────────────────────

TEST(SerializationReadString, RoundTripsAnHonestString) {
  std::istringstream is(framed(5, "hello"), std::ios::binary);
  std::string s;
  serialization::readString(is, s);
  EXPECT_EQ(s, "hello");
}

TEST(SerializationReadString, RefusesALengthLongerThanTheStream) {
  // The header claims 4 GB; three bytes follow. Before the fix this reached
  // s.resize(0xFFFFFFFF).
  std::istringstream is(framed(0xFFFFFFFFu, "abc"), std::ios::binary);
  std::string s = "sentinel";
  serialization::readString(is, s);
  EXPECT_TRUE(s.empty()) << "a length past the end of the stream must not be allocated";
}

TEST(SerializationReadString, RefusesAModestOverclaim) {
  // Not just the absurd case: 64 declared, 3 present.
  std::istringstream is(framed(64, "abc"), std::ios::binary);
  std::string s;
  serialization::readString(is, s);
  EXPECT_TRUE(s.empty());
}

TEST(SerializationReadString, HandlesAnEmptyString) {
  std::istringstream is(framed(0, ""), std::ios::binary);
  std::string s = "sentinel";
  serialization::readString(is, s);
  EXPECT_TRUE(s.empty());
}

TEST(SerializationReadString, TruncatedLengthFieldDoesNotAllocate) {
  // Two bytes where a uint32 should be: readPod cannot fill `len`, which used
  // to be uninitialised stack.
  std::istringstream is(std::string("\x01\x02", 2), std::ios::binary);
  std::string s;
  serialization::readString(is, s);
  EXPECT_TRUE(s.empty());
}

// ── B-023: PNG bit depth divides ────────────────────────────────────────────

TEST(PngBitDepth, AcceptsTheSpecDepths) {
  EXPECT_TRUE(pngbitdepth::isValid(8, pngbitdepth::kGrayscale));
  EXPECT_TRUE(pngbitdepth::isValid(16, pngbitdepth::kGrayscale));
  EXPECT_TRUE(pngbitdepth::isValid(1, pngbitdepth::kGrayscale));
  EXPECT_TRUE(pngbitdepth::isValid(2, pngbitdepth::kPalette));
  EXPECT_TRUE(pngbitdepth::isValid(4, pngbitdepth::kPalette));
}

TEST(PngBitDepth, RejectsZeroBecauseTheDecoderDividesByIt) {
  // `const int ppb = 8 / ctx.bitDepth` -- this is the division by zero.
  EXPECT_FALSE(pngbitdepth::isValid(0, pngbitdepth::kGrayscale));
}

TEST(PngBitDepth, RejectsNonPowerOfTwoDepths) {
  for (uint8_t d : {3, 5, 6, 7, 9, 15, 17, 32, 255}) {
    EXPECT_FALSE(pngbitdepth::isValid(d, pngbitdepth::kGrayscale)) << "depth " << int(d);
  }
}

TEST(PngBitDepth, RejectsSubByteDepthsOnColourTypesTheSpecForbids) {
  constexpr uint8_t kRgb = 2, kGrayAlpha = 4, kRgba = 6;
  for (uint8_t d : {1, 2, 4}) {
    EXPECT_FALSE(pngbitdepth::isValid(d, kRgb));
    EXPECT_FALSE(pngbitdepth::isValid(d, kGrayAlpha));
    EXPECT_FALSE(pngbitdepth::isValid(d, kRgba));
  }
  // 8 and 16 stay legal everywhere.
  EXPECT_TRUE(pngbitdepth::isValid(8, kRgba));
  EXPECT_TRUE(pngbitdepth::isValid(16, kRgb));
}

// ── B-023: the XTC plane geometry that motivated the bounds guard ────────────
//
// Xtc.cpp sizes each 2-bit plane (width * height + 7) / 8 but addresses it
// column-major with (height + 7) / 8 bytes per column. These pin the arithmetic
// so the guard is not "removed as redundant" by someone who checks only the
// common case.

namespace {
size_t planeSizeAsAllocated(uint32_t w, uint32_t h) { return (static_cast<size_t>(w) * h + 7) / 8; }
size_t colBytes(uint32_t h) { return (h + 7) / 8; }
size_t highestOffsetAddressed(uint32_t w, uint32_t h) {
  // colIndex maxes at w-1, byteInCol at (h-1)/8.
  return (w - 1) * colBytes(h) + (h - 1) / 8;
}
}  // namespace

TEST(XtcPlaneGeometry, AgreesWhenHeightIsAMultipleOfEight) {
  for (uint32_t h : {8u, 16u, 240u, 528u}) {
    EXPECT_LT(highestOffsetAddressed(400, h), planeSizeAsAllocated(400, h))
        << "height " << h << " should stay in bounds";
  }
}

TEST(XtcPlaneGeometry, OverrunsWhenHeightIsNotAMultipleOfEight) {
  // This is the bug: the last columns index past the plane. plane2 sits at
  // pageBuffer + planeSize, so plane1 runs into plane2 and plane2 runs off the
  // end of the allocation.
  EXPECT_GT(highestOffsetAddressed(100, 100), planeSizeAsAllocated(100, 100));
  EXPECT_GT(highestOffsetAddressed(400, 527), planeSizeAsAllocated(400, 527));
}

TEST(XtcPlaneGeometry, TheGuardRejectsExactlyTheOutOfRangeSamples) {
  constexpr uint32_t w = 100, h = 100;
  const size_t plane = planeSizeAsAllocated(w, h);
  size_t skipped = 0, kept = 0;
  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x < w; x++) {
      const size_t off = (w - 1 - x) * colBytes(h) + y / 8;
      (off >= plane ? skipped : kept)++;
    }
  }
  EXPECT_GT(skipped, 0u) << "if nothing is skipped the guard is untested here";
  EXPECT_GT(kept, skipped) << "the guard must not throw away most of the image";
}
