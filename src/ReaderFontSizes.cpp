#include "ReaderFontSizes.h"

#include <iterator>

std::vector<uint8_t> readerFontPointSizes(const SdCardFontRegistry* registry, const char* sdFamilyName) {
  if (registry && sdFamilyName && sdFamilyName[0] != '\0') {
    if (const auto* family = registry->findFamily(sdFamilyName)) {
      auto sizes = family->availableSizes();
      if (!sizes.empty()) return sizes;
    }
  }
  return {std::begin(BUILTIN_READER_POINT_SIZES), std::end(BUILTIN_READER_POINT_SIZES)};
}

uint8_t snapToNearestPointSize(const uint8_t* sizes, const size_t count, const uint8_t pt) {
  if (!sizes || count == 0) return pt;

  uint8_t best = sizes[0];
  uint8_t bestDelta = best > pt ? best - pt : pt - best;
  for (size_t i = 1; i < count; i++) {
    const uint8_t delta = sizes[i] > pt ? sizes[i] - pt : pt - sizes[i];
    // Strictly-less keeps the smaller size on a tie, since `sizes` is ascending.
    if (delta < bestDelta) {
      best = sizes[i];
      bestDelta = delta;
    }
  }
  return best;
}

uint8_t pointSizeForSlot(const uint8_t* sizes, const size_t count, const uint8_t slot) {
  if (!sizes || count == 0) return 0;
  return sizes[slot < count ? slot : count - 1];
}

uint8_t slotForPointSize(const uint8_t* sizes, const size_t count, const uint8_t pt) {
  if (!sizes || count == 0) return 0;
  uint8_t best = 0;
  uint8_t bestDelta = sizes[0] > pt ? sizes[0] - pt : pt - sizes[0];
  for (size_t i = 1; i < count; i++) {
    const uint8_t delta = sizes[i] > pt ? sizes[i] - pt : pt - sizes[i];
    // Strictly-less keeps the smaller slot on a tie, matching snapToNearestPointSize().
    if (delta < bestDelta) {
      best = static_cast<uint8_t>(i);
      bestDelta = delta;
    }
  }
  return best;
}
