// Cover conversion is not under test here; the real converter reaches JPEGDEC.
#pragma once
#include <cstdint>
#include <string>
struct JpegToBmpConverter {
  template <typename In, typename Out>
  static bool jpegFileToBmpStream(In&, Out&, bool) {
    return false;
  }
  template <typename In, typename Out>
  static bool jpegFileTo1BitBmpStreamWithSize(In&, Out&, int, int) {
    return false;
  }
};
