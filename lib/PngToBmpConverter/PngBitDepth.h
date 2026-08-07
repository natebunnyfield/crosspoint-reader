#pragma once
#include <cstdint>

// PNG bit-depth validation, extracted so it can be tested without a HalFile,
// a decoder context, or a real PNG on disk.
//
// The row decoders in PngToBmpConverter.cpp compute `8 / bitDepth` for the
// sub-byte depths. A declared depth of 0 is therefore a division by zero, and
// 3/5/6/7 produce a packing that reads crooked -- both reachable from a
// malformed cover on the SD card. The spec allows only 1, 2, 4, 8 and 16, and
// restricts the sub-byte depths to grayscale and palette images.
//
// The in-book decoder (lib/Epub/Epub/converters/PngToFramebufferConverter.cpp,
// isSupportedBitDepth) has always had this check. This one is the same rule for
// the BMP path, which never had it.
namespace pngbitdepth {

// Colour-type values from the PNG spec, matching PngToBmpConverter's enum.
constexpr uint8_t kGrayscale = 0;
constexpr uint8_t kPalette = 3;

inline bool isValid(uint8_t bitDepth, uint8_t colorType) {
  if (bitDepth != 1 && bitDepth != 2 && bitDepth != 4 && bitDepth != 8 && bitDepth != 16) return false;
  if (bitDepth < 8 && colorType != kGrayscale && colorType != kPalette) return false;
  return true;
}

}  // namespace pngbitdepth
