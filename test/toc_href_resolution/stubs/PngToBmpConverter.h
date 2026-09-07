// Cover conversion is not under test here; the real converter reaches PNGdec.
#pragma once
#include <cstdint>
#include <string>
struct PngToBmpConverter {
  template <typename In, typename Out>
  static bool pngFileToBmpStream(In&, Out&, bool) {
    return false;
  }
  template <typename In, typename Out>
  static bool pngFileTo1BitBmpStreamWithSize(In&, Out&, int, int) {
    return false;
  }
};
