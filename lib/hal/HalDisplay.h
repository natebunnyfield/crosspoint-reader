#pragma once
#include <Arduino.h>
#include <EInkDisplay.h>
#include <PanelPolarity.h>

// Supersampling factor between the LOGICAL screen coordinate space every caller
// draws in and the PHYSICAL framebuffer. On real hardware there is exactly one
// framebuffer pixel per logical pixel, so this is 1 and every scale-aware branch
// compiles out via `#if` -- the device build stays textually the original code.
// The SIMULATOR's own HalDisplay.h defines it >1 to give glyphs a denser raster
// on a Retina/phone panel WITHOUT changing layout: advances, kerning, wrapping
// and pagination keep reading the 1x font tables and keep producing logical
// coordinates; only the pixel-write layer scales. See
// GfxRenderer::drawPixelDevice() and the hi-res font path in drawText().
#ifndef CROSSPOINT_RENDER_SCALE
#define CROSSPOINT_RENDER_SCALE 1
#endif

class HalDisplay {
 public:
  // Constructor with pin configuration
  HalDisplay();

  // Destructor
  ~HalDisplay();

  // Refresh modes
  enum RefreshMode {
    FULL_REFRESH,  // Full refresh with complete waveform
    HALF_REFRESH,  // Half refresh (1720ms) - balanced quality and speed
    FAST_REFRESH   // Fast refresh using custom LUT
  };

  // Pass seamless=true on any path where the panel already shows the
  // content it should after begin() returns (silent reboot's popup,
  // sleep-wake with a restored buffer). Skips the wakeup-gated
  // requestResync() and defuses the SDK's X3 _x3InitialFullSyncsRemaining
  // counter; otherwise the first two paints get promoted to FULL
  // (~770ms each on X3).
  void begin(bool seamless = false);

  // Display dimensions
  static constexpr int RENDER_SCALE = CROSSPOINT_RENDER_SCALE;  // always 1 on device
  static constexpr uint16_t DISPLAY_WIDTH = EInkDisplay::DISPLAY_WIDTH;
  static constexpr uint16_t DISPLAY_HEIGHT = EInkDisplay::DISPLAY_HEIGHT;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;
  // Logical panel geometry. Identical to the physical panel on hardware; the
  // simulator's HalDisplay.h keeps these at the real panel size while
  // DISPLAY_WIDTH/HEIGHT grow by RENDER_SCALE.
  static constexpr uint16_t LOGICAL_WIDTH = DISPLAY_WIDTH;
  static constexpr uint16_t LOGICAL_HEIGHT = DISPLAY_HEIGHT;

  // Frame buffer operations
  void clearScreen(uint8_t color = 0xFF) const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 bool fromProgmem = false) const;
  void drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            bool fromProgmem = false) const;

  // Output polarity ("dark mode"). The framebuffer stays logical — 1 is still
  // white — and the bytes are flipped on their way to the panel. Nothing in the
  // drawing layer has to know, except the image paths, which counter-invert so
  // photographs and covers do not come out as negatives
  // (GfxRenderer::preserveImagePolarity).
  //
  // The flip happens HERE (PanelPolarity), not via FreeInkDisplay::setInverted().
  // The SDK's flag disables every grayscale path, async refresh and window
  // diffs while it is set; doing the same flip on this side keeps all three.
  // See PanelPolarity.h for the citations and for the one thing it costs.
  void setInverted(bool inverted);
  bool isInverted() const;

  void displayBuffer(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);
  // Non-blocking refresh (shadow-free): starts the panel waveform and returns
  // while the panel refreshes on its own. The framebuffer must stay untouched
  // until waitRefreshComplete(), and the caller must rebuild the differential
  // baseline before the next differential update (the tiled grayscale cleanup
  // does). Panels without deferral fall back to a blocking refresh.
  void displayBufferAsync(RefreshMode mode = RefreshMode::FAST_REFRESH);
  // Block until a pending deferred refresh completes (no-op when none is).
  void waitRefreshComplete();
  // True when displayBufferAsync() genuinely overlaps (panel driver defers);
  // false where it falls back to a blocking refresh.
  bool supportsAsyncRefresh() const;
  void refreshDisplay(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);

  // Power management
  void deepSleep();

  // Access to frame buffer
  uint8_t* getFrameBuffer() const;

  // Lend the framebuffer's ~48 KB STORAGE to a memory-hungry phase (chapter
  // builds) without freeing it: the allocation never moves, so repeated loans
  // cannot fragment the heap (free+realloc measurably did). No display calls
  // between lend and return; the panel keeps its last refreshed image. The
  // buffer comes back white — redraw fully. Returns nullptr if already lent.
  uint8_t* lendFrameBufferStorage(uint32_t* sizeOut);
  void returnFrameBufferStorage();

  // X3 grayscale preconditioning (OEM "AA-pre-BW(mid)" settle pass), windowed
  // to the gray region in physical panel coordinates (no-arg = full frame).
  // Call after the BW base frame is displayed and before the grayscale planes
  // are written; no-op on X4. See EInkDisplay::preconditionGrayscale.
  void preconditionGrayscale();
  void preconditionGrayscale(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

  // Display the framebuffer as the base frame for a grayscale overlay that
  // follows. On X3, HALF fallback first requests a resync to match
  // displayBuffer(HALF); FAST fallback keeps the OEM differential base waveform
  // ("AA-pre-BW(mid)"). Other panels display normally with `fallback` mode.
  void displayGrayscaleBase(RefreshMode fallback = HALF_REFRESH, bool turnOffScreen = false);

  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer);

  void displayGrayBuffer(bool turnOffScreen = false);

  // Tiled grayscale: stream one band of a plane (lsbPlane selects LSB/MSB RAM)
  // straight to the controller; supportsStripGrayscale() gates the path. See
  // EInkDisplay::writeGrayscalePlaneStrip.
  void writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t* rows, uint16_t yStart, uint16_t numRows);
  bool supportsStripGrayscale() const;

  // Runtime geometry passthrough
  uint16_t getDisplayWidth() const;
  uint16_t getDisplayHeight() const;
  uint16_t getDisplayWidthBytes() const;
  uint32_t getBufferSize() const;

 private:
  // Ensure the framebuffer carries panel polarity before handing it to the SDK,
  // and logical polarity again once the SDK is done with it. Both are no-ops in
  // light mode. See PanelPolarity.h for the invariant.
  void toPanelPolarity();
  void toLogicalPolarity();
  // Promote a FAST refresh to HALF for the one refresh that follows a polarity
  // change: a differential refresh cannot cross polarities.
  RefreshMode resolvePolarityMode(RefreshMode mode);

  EInkDisplay einkDisplay;
  PanelPolarity polarity;
};

extern HalDisplay display;
