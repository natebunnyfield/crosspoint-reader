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
  // The PANEL grows with RENDER_SCALE; the logical page must not. GfxRenderer
  // divides the panel by RENDER_SCALE to get logical coordinates, so a fixed
  // 792x528 panel at scale 2 yields a 396x264 logical page — a quarter of the
  // area, a quarter of the words, and nothing like the page iOS draws. Scaling
  // the panel keeps the logical page at 792x528 and puts the extra pixels where
  // they belong: in the framebuffer, which is the whole point of the 2x cut.
  static constexpr uint16_t DISPLAY_WIDTH = 792 * CROSSPOINT_RENDER_SCALE;   // X3 landscape panel
  static constexpr uint16_t DISPLAY_HEIGHT = 528 * CROSSPOINT_RENDER_SCALE;
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
  // Output polarity. Real enough for the host harness: GfxRenderer reads it to
  // decide whether to counter-invert content images, so a test can set it and
  // exercise preserveImagePolarity without a panel.
  void setInverted(bool v) { inverted = v; }
  bool isInverted() const { return inverted; }
  void refreshDisplay(RefreshMode = FAST_REFRESH, bool = false) {}
  void displayGrayscaleBase(RefreshMode = HALF_REFRESH, bool = false) {}
  void copyGrayscaleBuffers(const uint8_t*, const uint8_t*) {}
  void copyGrayscaleLsbBuffers(const uint8_t*) { lsbPlaneCopies++; }
  void copyGrayscaleMsbBuffers(const uint8_t*) { msbPlaneCopies++; }
  void cleanupGrayscaleBuffers(const uint8_t*) { grayCleanups++; }
  void displayGrayBuffer(bool = false, const unsigned char* = nullptr, bool = false) { grayDisplays++; }
  void drawImage(const uint8_t*, uint16_t, uint16_t, uint16_t, uint16_t, bool = false) {}
  void drawImageTransparent(const uint8_t*, uint16_t, uint16_t, uint16_t, uint16_t, bool = false) {}
  void writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t*, int, int numRows) {
    (lsbPlane ? lsbStripRows : msbStripRows) += numRows;
    stripWrites++;
  }
  bool supportsStripGrayscale() const { return stripGrayscale; }
  void preconditionGrayscale(int = 0, int = 0, int = 0, int = 0) {}
  void deepSleep() {}

  // Grayscale instrumentation for the host suite (test/text_antialiasing).
  // Defaults keep the historical behaviour — no strip support, every grayscale
  // call a no-op — so no existing harness sees a change.
  bool stripGrayscale = false;
  int stripWrites = 0;
  int lsbStripRows = 0;
  int msbStripRows = 0;
  int lsbPlaneCopies = 0;
  int msbPlaneCopies = 0;
  int grayDisplays = 0;
  int grayCleanups = 0;
  void resetGrayscaleCounters() {
    stripWrites = lsbStripRows = msbStripRows = 0;
    lsbPlaneCopies = msbPlaneCopies = grayDisplays = grayCleanups = 0;
  }

 private:
  bool inverted = false;
};
extern HalDisplay display;
