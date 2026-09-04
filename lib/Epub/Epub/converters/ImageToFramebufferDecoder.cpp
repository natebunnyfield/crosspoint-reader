#include "ImageToFramebufferDecoder.h"

#include <Logging.h>

#include <cstdint>

bool ImageToFramebufferDecoder::validateImageDimensions(int width, int height, const std::string& format) {
  // Each dimension bounded first, and the product computed in 64 bits. `width`
  // and `height` come straight from the decoder for an image a Wi-Fi peer can
  // PUT onto the card (B-045's threat model), and `width * height` as `int`
  // OVERFLOWS -- 65535x65535 wraps negative and passed the check that exists
  // to reject exactly that image (crafted-input hunt 2026-09-04, UB under
  // UBSan). Same shape the BMP reader already uses (Bitmap.cpp).
  if (width <= 0 || height <= 0) {
    LOG_ERR("IMG", "Image has a non-positive dimension (%dx%d %s)", width, height, format.c_str());
    return false;
  }
  const int64_t pixels = static_cast<int64_t>(width) * height;
  if (pixels > MAX_SOURCE_PIXELS) {
    LOG_ERR("IMG", "Image too large (%dx%d = %lld pixels %s), max supported: %d pixels", width, height,
            static_cast<long long>(pixels), format.c_str(), MAX_SOURCE_PIXELS);
    return false;
  }
  return true;
}

void ImageToFramebufferDecoder::warnUnsupportedFeature(const std::string& feature, const std::string& imagePath) {
  LOG_ERR("IMG", "Warning: Unsupported feature '%s' in image '%s'. Image may not display correctly.", feature.c_str(),
          imagePath.c_str());
}
