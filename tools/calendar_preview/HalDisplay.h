#pragma once
#include "Arduino.h"
#include <cstdint>
#include <cstring>
#include <cstdlib>
// Host stubs render at 1:1 with the panel; see lib/hal/HalDisplay.h.
#ifndef CROSSPOINT_RENDER_SCALE
#define CROSSPOINT_RENDER_SCALE 1
#endif

class HalDisplay {
 public:
  static constexpr int RENDER_SCALE = CROSSPOINT_RENDER_SCALE;
  static constexpr uint16_t DISPLAY_WIDTH = 792;   // X3 landscape panel
  static constexpr uint16_t DISPLAY_HEIGHT = 528;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };
  void begin(bool = false) {}
  uint16_t getDisplayWidth() const { return DISPLAY_WIDTH; }
  uint16_t getDisplayHeight() const { return DISPLAY_HEIGHT; }
  uint16_t getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
  uint32_t getBufferSize() const { return BUFFER_SIZE; }
  uint8_t* getFrameBuffer() const { static uint8_t buf[BUFFER_SIZE]; return buf; }
  uint8_t* lendFrameBufferStorage(uint32_t* outSize) { if (outSize) *outSize = BUFFER_SIZE; return getFrameBuffer(); }
  void returnFrameBufferStorage() {}
  void clearScreen(uint8_t c) { memset(getFrameBuffer(), c, BUFFER_SIZE); }
  void displayBuffer(RefreshMode = FAST_REFRESH, bool = false) {}
  void displayBufferAsync(RefreshMode = FAST_REFRESH) {}
  void waitRefreshComplete() {}
  bool supportsAsyncRefresh() const { return false; }
  void refreshDisplay(RefreshMode = FAST_REFRESH, bool = false) {}
  void displayGrayscaleBase(RefreshMode = HALF_REFRESH, bool = false) {}
  void copyGrayscaleBuffers(const uint8_t*, const uint8_t*) {}
  void copyGrayscaleLsbBuffers(const uint8_t*) {}
  void copyGrayscaleMsbBuffers(const uint8_t*) {}
  void cleanupGrayscaleBuffers(const uint8_t*) {}
  void displayGrayBuffer(bool = false, const unsigned char* = nullptr, bool = false) {}
  void drawImage(const uint8_t*, uint16_t, uint16_t, uint16_t, uint16_t, bool = false) {}
  void drawImageTransparent(const uint8_t*, uint16_t, uint16_t, uint16_t, uint16_t, bool = false) {}
  void writeGrayscalePlaneStrip(bool, const uint8_t*, int, int) {}
  bool supportsStripGrayscale() const { return false; }
  void preconditionGrayscale(int = 0, int = 0, int = 0, int = 0) {}
  void deepSleep() {}
};
extern HalDisplay display;
