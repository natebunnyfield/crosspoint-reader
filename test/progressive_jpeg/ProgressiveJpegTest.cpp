// Progressive JPEGs whose DC coefficients are split into one scan PER COMPONENT
// (SOS with a single component, Ss=0 Se=0) instead of one interleaved scan.
// MozJPEG and most web optimizers emit that layout, so it reaches this firmware
// through EPUB covers and illustrations. JPEGDEC's MCU loop reads the chroma
// blocks of each MCU unconditionally; in such a scan those bits are not there,
// so the reads eat the following Y blocks and the bitstream desynchronizes.
//
// The failure has two faces, and the quiet one is worse:
//   * large image -> the next Huffman lookup fails, rc=0, JPEG_DECODE_ERROR
//   * small image -> no error at all, and a silently scrambled preview
//
// scripts/jpegdec_patches/0003-decode-non-interleaved-dc-scans.patch re-derives
// the traversal from the luma component's own block grid, which is what T.81
// A.2.2 prescribes for a non-interleaved scan. These tests pin both that the
// broken layouts now decode and that every other layout is untouched.
//
// The strongest assertion available here needs no golden file: a non-interleaved
// encoding and an interleaved encoding OF THE SAME SOURCE IMAGE must produce
// byte-identical DC previews, because the DC coefficients are the same numbers
// in a different container. See fixtures/README for how the pair is generated.

#include <JPEGDEC.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

const char* fixtureDir() { return CROSSPOINT_JPEG_FIXTURES; }

std::vector<uint8_t> readFixture(const std::string& name) {
  const std::string path = std::string(fixtureDir()) + "/" + name;
  std::vector<uint8_t> bytes;
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return bytes;
  fseek(f, 0, SEEK_END);
  const long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (len > 0) {
    bytes.resize(static_cast<size_t>(len));
    if (fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) bytes.clear();
  }
  fclose(f);
  return bytes;
}

// The decoded grayscale preview, assembled from the draw callback exactly as
// JpegToFramebufferConverter assembles it (EIGHT_BIT_GRAYSCALE, and
// JPEG_SCALE_EIGHTH for progressive, which JPEGDEC forces internally anyway).
struct Preview {
  int width{0};
  int height{0};
  int rc{0};
  int lastError{0};
  std::vector<uint8_t> pixels;

  bool operator==(const Preview& o) const { return width == o.width && height == o.height && pixels == o.pixels; }
};

int drawCallback(JPEGDRAW* pDraw) {
  auto* out = static_cast<Preview*>(pDraw->pUser);
  const auto* src = reinterpret_cast<const uint8_t*>(pDraw->pPixels);
  for (int row = 0; row < pDraw->iHeight; row++) {
    const int y = pDraw->y + row;
    if (y < 0 || y >= out->height) continue;
    for (int col = 0; col < pDraw->iWidth; col++) {
      const int x = pDraw->x + col;
      if (x < 0 || x >= out->width) continue;
      out->pixels[static_cast<size_t>(y) * out->width + x] = src[row * pDraw->iWidth + col];
    }
  }
  return 1;
}

Preview decode(const std::string& name, int pixelType = EIGHT_BIT_GRAYSCALE) {
  Preview out;
  std::vector<uint8_t> bytes = readFixture(name);
  if (bytes.empty()) return out;

  JPEGDEC jpeg;
  if (jpeg.openRAM(bytes.data(), static_cast<int>(bytes.size()), drawCallback) != 1) {
    out.lastError = jpeg.getLastError();
    return out;
  }
  const bool progressive = jpeg.getJPEGType() == JPEG_MODE_PROGRESSIVE;
  const int denom = progressive ? 8 : 1;
  out.width = (jpeg.getWidth() + denom - 1) / denom;
  out.height = (jpeg.getHeight() + denom - 1) / denom;
  out.pixels.assign(static_cast<size_t>(out.width) * out.height, 0);
  jpeg.setPixelType(pixelType);
  jpeg.setUserPointer(&out);
  out.rc = jpeg.decode(0, 0, progressive ? JPEG_SCALE_EIGHTH : 0);
  out.lastError = jpeg.getLastError();
  jpeg.close();
  return out;
}

// A preview that is one flat value carries no image, and every scrambled decode
// seen while writing these tests still varied. Guards against a "fix" that
// merely stops the decoder early.
bool hasDetail(const Preview& p) {
  if (p.pixels.size() < 2) return false;
  for (size_t i = 1; i < p.pixels.size(); i++) {
    if (p.pixels[i] != p.pixels[0]) return true;
  }
  return false;
}

}  // namespace

// --- the bug: both non-interleaved layouts now decode, and decode correctly ---

TEST(NonInterleavedDcScan, FourFourFourMatchesItsInterleavedTwin) {
  const Preview noninter = decode("prog-noninter-444.jpg");
  const Preview inter = decode("prog-inter-444.jpg");
  ASSERT_EQ(noninter.rc, 1) << "lastError=" << noninter.lastError;
  ASSERT_EQ(inter.rc, 1) << "lastError=" << inter.lastError;
  EXPECT_TRUE(hasDetail(noninter));
  // Before patch 0003 this decode reported success and was scrambled:
  // mean |diff| 107.6 against a Pillow reference.
  EXPECT_TRUE(noninter == inter) << "non-interleaved 4:4:4 preview differs from the interleaved "
                                    "encoding of the same source image";
}

TEST(NonInterleavedDcScan, SubsampledMatchesItsInterleavedTwin) {
  const Preview noninter = decode("prog-noninter-420.jpg");
  const Preview inter = decode("prog-inter-420.jpg");
  ASSERT_EQ(noninter.rc, 1) << "lastError=" << noninter.lastError;
  ASSERT_EQ(inter.rc, 1) << "lastError=" << inter.lastError;
  EXPECT_TRUE(hasDetail(noninter));
  // 4:2:0 is the case a two-MCU-wide luma grid makes tempting to special-case.
  // It needs none: the scan's raster order over the component's own block grid
  // IS the 1:1 traversal. Before patch 0003, mean |diff| 113.5.
  EXPECT_TRUE(noninter == inter) << "non-interleaved 4:2:0 preview differs from the interleaved "
                                    "encoding of the same source image";
}

TEST(NonInterleavedDcScan, RealBookIllustrationDecodes) {
  // 642x800 4:2:0, 8 scans, the first three single-component DC scans. Found by
  // scanning the EPUBs on the owner's machine -- the only real-world specimen in
  // ~1900 images, and the one that showed the loud half of the failure: before
  // patch 0003 this returned rc=0 with JPEG_DECODE_ERROR, which reads like
  // memory pressure and is why the original investigation went to the heap.
  const Preview real = decode("real-noninter-420.jpg");
  ASSERT_EQ(real.rc, 1) << "lastError=" << real.lastError;
  EXPECT_EQ(real.width, 81);
  EXPECT_EQ(real.height, 100);
  EXPECT_TRUE(hasDetail(real));
}

// --- everything else must be bit-for-bit unchanged ---

TEST(NonInterleavedDcScan, UnaffectedLayoutsStillDecode) {
  for (const char* name : {"baseline-420.jpg", "prog-inter-444.jpg", "prog-inter-420.jpg", "prog-gray.jpg"}) {
    const Preview p = decode(name);
    EXPECT_EQ(p.rc, 1) << name << " lastError=" << p.lastError;
    EXPECT_TRUE(hasDetail(p)) << name;
  }
}

TEST(NonInterleavedDcScan, InterleavedPairAgreesAcrossSubsampling) {
  // Not about the patch: this is the control that makes the two twin tests
  // above meaningful. If 4:4:4 and 4:2:0 encodings of one image did not already
  // agree at 1/8 DC resolution, "matches its twin" would prove nothing.
  const Preview a = decode("prog-inter-444.jpg");
  const Preview b = decode("prog-inter-420.jpg");
  ASSERT_EQ(a.rc, 1);
  ASSERT_EQ(b.rc, 1);
  EXPECT_TRUE(a == b);
}

// --- the refusal path ---

TEST(NonInterleavedDcScan, ColorOutputIsRefusedNotScrambled) {
  // Only luma is read from a non-interleaved DC scan, so a color caller would
  // compose against chroma blocks that were never decoded. Patch 0003 refuses
  // instead, and names the reason through jpegDecodeErrorText().
  const Preview p = decode("prog-noninter-420.jpg", RGB565_LITTLE_ENDIAN);
  EXPECT_NE(p.rc, 1);
  EXPECT_EQ(p.lastError, JPEG_UNSUPPORTED_FEATURE);
}

TEST(NonInterleavedDcScan, ColorOutputOnOrdinaryFilesIsUntouched) {
  // The refusal above must be confined to the layout that needs it.
  const Preview p = decode("prog-inter-420.jpg", RGB565_LITTLE_ENDIAN);
  EXPECT_EQ(p.rc, 1) << "lastError=" << p.lastError;
}
