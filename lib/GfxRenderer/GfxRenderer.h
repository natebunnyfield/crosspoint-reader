#pragma once

#include <EpdFontFamily.h>
#include <HalDisplay.h>

#include "GlyphAaPlanes.h"

namespace BidiUtils {
// Paragraph base direction for the Unicode BiDi algorithm (UAX#9).
// AUTO: scan text for first strong directional character (P2/P3 rules)
// LTR:  force left-to-right paragraph embedding level
// RTL:  force right-to-left paragraph embedding level
enum class BidiBaseDir : signed char { AUTO = -1, LTR = 0, RTL = 1 };
}  // namespace BidiUtils

class FontCacheManager;
class SdCardFont;

#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "Bitmap.h"

// Color representation: uint8_t mapped to 4x4 Bayer matrix dithering levels
// 0 = transparent, 1-16 = gray levels (white to black)
enum Color : uint8_t { Clear = 0x00, White = 0x01, LightGray = 0x05, DarkGray = 0x0A, Black = 0x10 };

class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };

  // How the 2-bit glyph edge levels (1 = dark gray, 2 = light gray) map onto
  // the panel's two grayscale planes during the GRAYSCALE_LSB/MSB text passes.
  // The panel offers exactly two gray targets (light via MSB-only, dark via
  // MSB+LSB); an unflagged glyph pixel keeps the black laid down by the BW base
  // pass, so "stronger" AA means flagging fewer/darker targets. Text-only:
  // image rendering (drawBitmap) keeps its fixed mapping.
  enum GrayscaleAaStrength : uint8_t {
    AA_STANDARD,  // glyph dark -> panel dark, glyph light -> panel light
    AA_CRISP,     // glyph dark -> black, glyph light -> panel light
    AA_DARK       // glyph dark -> black, glyph light -> panel dark
  };
  static_assert(static_cast<uint8_t>(AA_STANDARD) == static_cast<uint8_t>(GlyphAa::Standard) &&
                    static_cast<uint8_t>(AA_CRISP) == static_cast<uint8_t>(GlyphAa::Crisp) &&
                    static_cast<uint8_t>(AA_DARK) == static_cast<uint8_t>(GlyphAa::Dark),
                "GrayscaleAaStrength and GlyphAa::Strength must stay in step");

  // Logical screen orientation from the perspective of callers
  enum Orientation {
    Portrait,                  // 480x800 logical coordinates (current default)
    LandscapeClockwise,        // 800x480 logical coordinates, rotated 180° (swap top/bottom)
    PortraitInverted,          // 480x800 logical coordinates, inverted
    LandscapeCounterClockwise  // 800x480 logical coordinates, native panel orientation
  };

 private:
  static constexpr size_t BW_BUFFER_CHUNK_SIZE = 8000;  // 8KB chunks to allow for non-contiguous memory

  HalDisplay& display;
  RenderMode renderMode;
  GrayscaleAaStrength grayscaleAaStrength = AA_STANDARD;
  // Set for the duration of a render that is BOTH inverted and going to run the
  // grayscale overlay. Flips which glyph levels the BW base pass paints and
  // which gray target each antialiased level takes; see GlyphAaPlanes.h. Off by
  // default so every screen that draws text without a grayscale pass — all UI
  // chrome — keeps painting solid glyphs.
  bool darkModeAa = false;
  // The panel is physically 800x480; portrait reading IS the 90 degree rotation.
  // Constant rather than a field so every non-Portrait branch folds out at compile time.
  static constexpr Orientation orientation = Portrait;
  bool fadingFix;
  uint8_t* frameBuffer = nullptr;
  uint16_t panelWidth = HalDisplay::DISPLAY_WIDTH;
  uint16_t panelHeight = HalDisplay::DISPLAY_HEIGHT;
  uint16_t panelWidthBytes = HalDisplay::DISPLAY_WIDTH_BYTES;
  uint32_t frameBufferSize = HalDisplay::BUFFER_SIZE;
  std::vector<uint8_t*> bwBufferChunks;
  std::map<int, EpdFontFamily> fontMap;
  // Mutable because ensureSdCardFontReady() is const (called from layout code
  // that holds a const GfxRenderer&) but triggers SD card reads and heap
  // allocation inside the SdCardFont objects. Same pragmatic compromise as
  // fontCacheManager_ below.
  mutable std::map<int, SdCardFont*> sdCardFonts_;
  mutable std::map<int, uint16_t> sdCardFontScales_;  // fontId -> 8.8 fixed point scale (256=1.0x)
#if CROSSPOINT_RENDER_SCALE > 1
  // Glyph-blit-only companions; see registerHiResFont(). Not present on device.
  std::map<int, EpdFontFamily> hiResFontMap_;
  mutable std::map<int, SdCardFont*> hiResSdFonts_;
#endif

  // Mutable because drawText() is const but needs to delegate scan-mode
  // recording to the (non-const) FontCacheManager. Same pragmatic compromise
  // as before, concentrated in a single pointer instead of four fields.
  mutable FontCacheManager* fontCacheManager_ = nullptr;

  // Tiled grayscale strip target. When active, drawPixel()/clearScreen()
  // operate on a caller-owned scratch holding one horizontal band of physical
  // rows [_stripY0, _stripY0 + _stripRows) (panelWidthBytes wide) instead of
  // the shared framebuffer, clipping pixels outside the band. Lets grayscale
  // planes render band-by-band straight to the controller without destroying
  // the BW framebuffer (no storeBwBuffer). Mutable because the render path is
  // const. See beginStripTarget()/endStripTarget().
  mutable uint8_t* _stripBuf = nullptr;
  mutable int _stripY0 = 0;
  mutable int _stripRows = 0;
  mutable bool _stripActive = false;

  // Text-only mode. While set, every NON-GLYPH drawing primitive returns
  // without touching the buffer; drawText and drawTextRotated90CW are the only
  // things that still put pixels down. See TextOnlyScope for why.
  mutable bool _textOnly = false;

  // CJK UI font fallback map: primary (built-in, Latin-only) UI font id -> a
  // size-matched SD-card font id that carries CJK glyphs. When a string drawn
  // or measured with a mapped primary font contains a CJK codepoint the primary
  // cannot render, the whole string is routed to the mapped fallback so it
  // appears at the same point size as the surrounding UI text. Populated by the
  // app-level SD font setup when an SD family is loaded. See resolveTextFontId().
  std::map<int, int> fallbackFontMap_;

  // If `text` contains a CJK codepoint that `fontId` cannot render and `fontId`
  // has a registered fallback, returns the fallback id; otherwise returns
  // fontId unchanged. The whole string is routed as a unit so each draw/measure
  // call stays single-font (consistent bit depth, metrics, wrapping).
  int resolveTextFontId(int fontId, const char* text, EpdFontFamily::Style style) const;

  void renderChar(const EpdFontFamily& fontFamily, uint32_t cp, int* x, int* y, bool pixelState,
                  EpdFontFamily::Style style) const;
  void freeBwBufferChunks();
  template <Color color>
  void drawPixelDither(int x, int y) const;
  template <Color color>
  void fillArc(int maxRadius, int cx, int cy, int xDir, int yDir) const;
  // Byte-aligned, orientation-specialized rectangle fill. Rotates the rect's
  // two opposing corners into physical-framebuffer space once, then walks each
  // physical row with head-mask / middle memset / tail-mask byte writes — no
  // per-pixel rotation, no per-pixel RMW.
  template <Color color>
  void fillRectImpl(int x, int y, int width, int height) const;

 public:
  explicit GfxRenderer(HalDisplay& halDisplay) : display(halDisplay), renderMode(BW), fadingFix(false) {}
  ~GfxRenderer() { freeBwBufferChunks(); }

  static constexpr int VIEWABLE_MARGIN_TOP = 9;
  static constexpr int VIEWABLE_MARGIN_RIGHT = 3;
  static constexpr int VIEWABLE_MARGIN_BOTTOM = 3;
  static constexpr int VIEWABLE_MARGIN_LEFT = 3;

#ifdef GFX_BOUNDS_COUNTER
  // Test-only instrumentation.
  //
  // drawPixel() CLIPS an out-of-range write: it logs "!! Outside range" and
  // returns before touching the framebuffer, so a geometry bug corrupts
  // nothing and no test can observe it — it surfaces only as a log line on a
  // device nobody is watching. That is exactly how the font picker shipped a
  // draw at logical x == screen width (crash_report.txt:
  // "!! Outside range (528, 302) -> (302, -1)").
  //
  // Host test builds define GFX_BOUNDS_COUNTER to turn that silent clip into
  // an assertable counter. Not compiled on device: no flash, no DRAM, and no
  // branch added to the per-pixel hot path.
  struct OutOfRange {
    uint32_t count = 0;
    int lastX = 0, lastY = 0, lastPhyX = 0, lastPhyY = 0;
  };
  static OutOfRange outOfRange;
  static void resetOutOfRange() { outOfRange = OutOfRange{}; }
  static uint32_t outOfRangeCount() { return outOfRange.count; }
#endif

  // Setup
  void begin();  // must be called right after display.begin()
  void insertFont(int fontId, EpdFontFamily font);
  // Rebinds a font id that is ALREADY registered. insertFont() deliberately
  // refuses to overwrite -- that refusal is the guard against two SD families
  // hashing to the same id -- so a caller that means to swap a face has to say
  // so. Used by applySystemFont() when the System font setting changes: the
  // chrome ids are registered at boot, and re-inserting them silently did
  // nothing but log "already registered", leaving the old face on screen while
  // the setting read as changed. Worse on a 2x build, where the hi-res half of
  // the swap DID take (it uses insert_or_assign), so the UI briefly blitted the
  // new family's glyphs against the outgoing family's metrics.
  void replaceFont(int fontId, EpdFontFamily font) { fontMap.insert_or_assign(fontId, font); }
  // Clears both the flash-font map and any SD-font registration for fontId.
  // Coupled to avoid dangling SdCardFont* in sdCardFonts_ when callers free
  // the underlying SdCardFont and forget the SD-side unregister.
  void removeFont(int fontId) {
    fontMap.erase(fontId);
    sdCardFonts_.erase(fontId);
    sdCardFontScales_.erase(fontId);
#if CROSSPOINT_RENDER_SCALE > 1
    removeHiResFont(fontId);
#endif
  }
  void setFontCacheManager(FontCacheManager* m) { fontCacheManager_ = m; }
  FontCacheManager* getFontCacheManager() const { return fontCacheManager_; }
  bool isFontCacheScanning() const;
  const std::map<int, EpdFontFamily>& getFontMap() const { return fontMap; }
  void registerSdCardFont(int fontId, SdCardFont* font) { sdCardFonts_[fontId] = font; }
  void unregisterSdCardFont(int fontId) { removeFont(fontId); }
  void clearSdCardFonts() {
    sdCardFonts_.clear();
    sdCardFontScales_.clear();
#if CROSSPOINT_RENDER_SCALE > 1
    clearSdCardHiResFonts();
#endif
  }
#if CROSSPOINT_RENDER_SCALE > 1
  // Hi-res companion faces: the SAME family/point size rasterised at
  // RENDER_SCALE x the ppem. Used ONLY to blit glyphs onto the device pixel
  // grid. Never consulted for advances, kerning, ascender, line height or any
  // other measurement -- those must keep coming from the 1x tables or layout
  // would move. Registration is optional; a font with no companion falls back to
  // the 1x glyph replicated RENDER_SCALE x RENDER_SCALE, i.e. today's output.
  void registerHiResFont(int fontId, SdCardFont* font, EpdFontFamily family) {
    hiResSdFonts_[fontId] = font;
    hiResFontMap_.insert_or_assign(fontId, family);
  }
  // Same thing for a font compiled into the binary. Deliberately NOT the call
  // above with a null SdCardFont*: hiResSdFonts_ exists so FontCacheManager can
  // prewarm glyphs off the card before a render pass, and it dereferences every
  // entry it holds. A built-in has nothing to prewarm -- its bitmaps are already
  // addressable -- so it belongs in the family map only.
  void registerHiResBuiltinFont(int fontId, EpdFontFamily family) { hiResFontMap_.insert_or_assign(fontId, family); }
  void removeHiResFont(int fontId) {
    hiResFontMap_.erase(fontId);
    hiResSdFonts_.erase(fontId);
  }
  // Drops the SD-backed hi-res registrations ONLY. hiResSdFonts_ holds exactly
  // the companions that came off the card, so its keys are the set to erase;
  // a built-in companion is registered into hiResFontMap_ alone (see
  // registerHiResBuiltinFont) and is deliberately left standing.
  //
  // It used to clear both maps outright, and that silently broke the chrome the
  // moment built-ins gained companions: every reader font or size change routes
  // through SdCardFontManager::unloadAll -> clearSdCardFonts -> here, so the UI
  // faces lost their 2x bitmaps on the first switch and fell back to replicated
  // 1x pixels until the next reboot. loadFile re-registers the reader font
  // afterwards, which is why the reader looked fine and only the chrome broke.
  void clearSdCardHiResFonts() {
    for (const auto& entry : hiResSdFonts_) hiResFontMap_.erase(entry.first);
    hiResSdFonts_.clear();
  }
  const std::map<int, SdCardFont*>& getHiResSdCardFonts() const { return hiResSdFonts_; }
  const EpdFontFamily* getHiResFamily(int fontId) const {
    const auto it = hiResFontMap_.find(fontId);
    return it != hiResFontMap_.end() ? &it->second : nullptr;
  }
#endif
  void registerSdCardFontScale(int fontId, uint16_t scale) { sdCardFontScales_[fontId] = scale; }
  void clearSdCardFontScales() { sdCardFontScales_.clear(); }
  uint16_t getSdCardFontScale(int fontId) const {
    auto it = sdCardFontScales_.find(fontId);
    return (it != sdCardFontScales_.end()) ? it->second : 256;
  }
  const std::map<int, SdCardFont*>& getSdCardFonts() const { return sdCardFonts_; }
  bool isSdCardFont(int fontId) const { return sdCardFonts_.count(fontId) > 0; }
  // Register/clear size-matched CJK UI fallbacks (see fallbackFontMap_).
  // setFallbackFont maps a primary UI font id to an SD font id of the same size.
  void setFallbackFont(int primaryFontId, int fallbackFontId) { fallbackFontMap_[primaryFontId] = fallbackFontId; }
  void clearFallbackFonts() { fallbackFontMap_.clear(); }
  // Ensure SD card font glyph data is loaded for the given text. Called from layout code
  // (which holds a const GfxRenderer&) before measuring word widths. Safe to call on non-SD fonts (no-op).
  // styleMask: bitmask of styles to prepare (bit 0=regular, 1=bold, 2=italic, 3=bold-italic).
  void ensureSdCardFontReady(int fontId, const char* utf8Text, uint8_t styleMask = 0x0F) const;
  void ensureSdCardFontReady(int fontId, const std::deque<std::string>& words, bool includeHyphen,
                             uint8_t styleMask = 0x0F) const;

  // Orientation is fixed at Portrait; the getter stays so call sites read as
  // coordinate-space queries rather than hardcoded assumptions.
  static constexpr Orientation getOrientation() { return orientation; }

  // Fading fix control
  void setFadingFix(const bool enabled) { fadingFix = enabled; }

  // Screen ops
  int getScreenWidth() const;
  int getScreenHeight() const;
  void tapToLogical(float nx, float ny, int& outX, int& outY) const;
  void displayBuffer(HalDisplay::RefreshMode refreshMode = HalDisplay::FAST_REFRESH) const;
  // Non-blocking refresh: starts the waveform and returns so CPU work (e.g.
  // grayscale strip rendering) can overlap the panel's refresh time. The
  // framebuffer must stay untouched until waitRefreshComplete(). Falls back to
  // a blocking refresh when fadingFix is enabled or the panel lacks deferral
  // support. See HalDisplay::displayBufferAsync for the baseline contract.
  void displayBufferAsync(HalDisplay::RefreshMode refreshMode = HalDisplay::FAST_REFRESH) const;
  void waitRefreshComplete() const;
  // True when displayBufferAsync() genuinely overlaps: panel defers and
  // fadingFix isn't forcing the blocking path. Callers can skip overlap
  // scaffolding (e.g. whole-plane grayscale buffers) when false.
  bool supportsAsyncRefresh() const;
  // True when the panel output is being inverted (dark mode). Grayscale still
  // runs in that state — the flip lives in HalDisplay, not in the SDK's
  // setInverted() — so this is NOT a reason to skip the AA passes. Render paths
  // pair it with setDarkModeAntiAliasing() to pick the right level split.
  bool isDisplayInverted() const;
  // EXPERIMENTAL: Windowed update - display only a rectangular region
  // void displayWindow(int x, int y, int width, int height) const;
  void invertScreen() const;
  void clearScreen(uint8_t color = 0xFF) const;
  void getOrientedViewableTRBL(int* outTop, int* outRight, int* outBottom, int* outLeft) const;

  // Tiled grayscale strip target. While active, drawPixel() and clearScreen()
  // operate on `scratch` (panelWidthBytes * stripRows bytes, holding physical
  // rows [stripY0, stripY0 + stripRows)) instead of the framebuffer; pixels
  // whose physical row falls outside the band are clipped. The clip is applied
  // after the orientation rotate, so it is orientation-agnostic. Used to render
  // grayscale planes band-by-band without a full second buffer.
  void beginStripTarget(uint8_t* scratch, int stripY0, int stripRows) const;
  void endStripTarget() const;

  // Text-only mode, for the grayscale anti-aliasing pass.
  //
  // In a grayscale plane pass every write means "lift this pixel toward
  // white", so a fill, a rule, an icon or a bitmap re-drawn during the pass
  // comes out GRAY on the panel even though the base frame drew it solid.
  // TextAntiAliasingPass.h states that as a rule the caller has to obey; this
  // enforces it, so a whole existing render body can be handed to the overlay
  // instead of a hand-maintained text-only copy of it that drifts.
  //
  // Suppressed: every non-glyph primitive (lines, rects, rounded rects, arcs,
  // polygons, dithered fills, images, icons, bitmaps, invertScreen,
  // writeFramebufferRegion). NOT suppressed: drawText / drawCenteredText /
  // drawTextRotated90CW, clearScreen (the pass clears each band itself), and
  // every measurement call -- layout must come out identical to the base pass
  // or the overlay lands on the wrong pixels.
  bool isTextOnly() const { return _textOnly; }
  class TextOnlyScope {
   public:
    explicit TextOnlyScope(const GfxRenderer& r) : r_(r), prev_(r._textOnly) { r._textOnly = true; }
    ~TextOnlyScope() { r_._textOnly = prev_; }
    TextOnlyScope(const TextOnlyScope&) = delete;
    TextOnlyScope& operator=(const TextOnlyScope&) = delete;

   private:
    const GfxRenderer& r_;
    bool prev_;
  };

  // Band culling for tiled grayscale. Takes a glyph bounding box in logical
  // screen coords and returns false only when a strip is active AND the box's
  // physical y-extent lies entirely outside the active band, letting callers
  // skip an expensive bitmap decode. Returns true when no strip is active.
  // Corners are rotated to physical, so it is orientation-aware.
  bool glyphIntersectsStrip(int x0, int y0, int x1, int y1) const;

  // Active pixel-write target for raw writers (DirectPixelWriter) that bypass
  // drawPixel for speed. When a strip target is active these return the band
  // scratch plus its physical-row origin and extent; otherwise the full
  // framebuffer ([0, panelHeight)). Writers subtract the origin and clip to the
  // extent, so they honor tiled-grayscale banding without per-pixel method calls.
  uint8_t* getWriteTarget() const { return _stripActive ? _stripBuf : frameBuffer; }
  int getWriteOriginY() const { return _stripActive ? _stripY0 : 0; }
  int getWriteRows() const { return _stripActive ? _stripRows : panelHeight; }

  // Supersampling: how many framebuffer pixels one logical pixel covers on each
  // axis. 1 on device (see HalDisplay.h). Everything above the pixel-write layer
  // -- coordinates, metrics, layout -- is in LOGICAL units regardless.
  static constexpr int RENDER_SCALE = CROSSPOINT_RENDER_SCALE;

  // Drawing
  void drawPixel(int x, int y, bool state = true) const;
#if CROSSPOINT_RENDER_SCALE > 1
  // Write ONE framebuffer pixel, in device coordinates (logical * RENDER_SCALE).
  // drawPixel() is this replicated over a RENDER_SCALE x RENDER_SCALE block, so
  // every non-glyph primitive keeps looking exactly as it does at scale 1. Only
  // the hi-res glyph blit addresses device pixels individually.
  void drawPixelDevice(int dx, int dy, bool state) const;
  // Device-coordinate form of glyphIntersectsStrip(), for the hi-res glyph path.
  bool glyphIntersectsStripDevice(int dx0, int dy0, int dx1, int dy1) const;
#endif
  void drawLine(int x1, int y1, int x2, int y2, bool state = true) const;
  void drawLine(int x1, int y1, int x2, int y2, int lineWidth, bool state) const;
  void drawArc(int maxRadius, int cx, int cy, int xDir, int yDir, int lineWidth, bool state) const;
  void drawRect(int x, int y, int width, int height, bool state = true) const;
  void drawRect(int x, int y, int width, int height, int lineWidth, bool state) const;
  void drawRoundedRect(int x, int y, int width, int height, int lineWidth, int cornerRadius, bool state) const;
  void drawRoundedRect(int x, int y, int width, int height, int lineWidth, int cornerRadius, bool roundTopLeft,
                       bool roundTopRight, bool roundBottomLeft, bool roundBottomRight, bool state) const;
  void maskRoundedRectOutsideCorners(int x, int y, int width, int height, int radius, Color color = Color::White) const;
  void fillRect(int x, int y, int width, int height, bool state = true) const;
  void fillRectDither(int x, int y, int width, int height, Color color) const;
  void fillRoundedRect(int x, int y, int width, int height, int cornerRadius, Color color) const;
  void fillRoundedRect(int x, int y, int width, int height, int cornerRadius, bool roundTopLeft, bool roundTopRight,
                       bool roundBottomLeft, bool roundBottomRight, Color color) const;
  void drawImage(const uint8_t bitmap[], int x, int y, int width, int height) const;
  void drawIcon(const uint8_t bitmap[], int x, int y, int size) const;
  void drawBitmap(const Bitmap& bitmap, int x, int y, int maxWidth, int maxHeight, float cropX = 0,
                  float cropY = 0) const;
  void drawBitmap1Bit(const Bitmap& bitmap, int x, int y, int maxWidth, int maxHeight) const;
  // Counter-invert a just-drawn CONTENT image (book cover, EPUB illustration) in
  // the framebuffer so that the output inversion HalDisplay::setInverted()
  // applies on the way to the panel lands on it twice and cancels out. A
  // photograph in dark mode would otherwise read as a negative — faces as
  // ghosts — which is the one thing a whole-screen polarity flip gets wrong.
  //
  // No-op unless the display is actually inverted, so the light-mode path is a
  // single predictable branch. Deliberately NOT applied to UI chrome
  // (drawImage/drawIcon): icons are line art that belongs to the page and must
  // flip with it.
  //
  // Takes LOGICAL coordinates, like every other public draw call. The rectangle
  // is the image's bounding box; the drawBitmap paths derive it from the source
  // dimensions and the fit scale, so it is exact rather than an estimate.
  void preserveImagePolarity(int x, int y, int width, int height) const;
  void fillPolygon(const int* xPoints, const int* yPoints, int numPoints, bool state = true) const;

  // Snapshot / restore a screen-coordinate framebuffer region (byte-aligned in
  // panel memory). readFramebufferRegion returns the bytes written to dst, or
  // 0 when the region is empty, offscreen, or exceeds dstCapacity. Pass the
  // same rectangle to writeFramebufferRegion to restore the saved pixels.
  // Enables partial-repaint patterns (e.g. moving a selection highlight)
  // without re-rendering the whole page.
  size_t readFramebufferRegion(int x, int y, int w, int h, uint8_t* dst, size_t dstCapacity) const;
  void writeFramebufferRegion(int x, int y, int w, int h, const uint8_t* src);

  // Text
  int getTextWidth(int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                   BidiUtils::BidiBaseDir baseDir = BidiUtils::BidiBaseDir::AUTO) const;
  void drawCenteredText(int fontId, int y, const char* text, bool black = true,
                        EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                        BidiUtils::BidiBaseDir baseDir = BidiUtils::BidiBaseDir::AUTO) const;
  void drawText(int fontId, int x, int y, const char* text, bool black = true,
                EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                BidiUtils::BidiBaseDir baseDir = BidiUtils::BidiBaseDir::AUTO) const;
  int getSpaceWidth(int fontId, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  /// Returns the total inter-word advance: fp4::toPixel(spaceAdvance + kern(leftCp,' ') + kern(' ',rightCp)).
  /// Using a single snap avoids the +/-1 px rounding error that arises when space advance and kern are
  /// snapped separately and then added as integers.
  int getSpaceAdvance(int fontId, uint32_t leftCp, uint32_t rightCp, EpdFontFamily::Style style) const;
  /// Returns the kerning adjustment between two adjacent codepoints.
  int getKerning(int fontId, uint32_t leftCp, uint32_t rightCp, EpdFontFamily::Style style) const;
  int getTextAdvanceX(int fontId, const char* text, EpdFontFamily::Style style) const;
  int getFontAscenderSize(int fontId) const;
  // Depth below the baseline, as a POSITIVE number (EpdFontData stores it
  // negative). Needed to centre text vertically: drawText() takes the top of the
  // ascender box and the glyphs run on down past the baseline, so centring on
  // the ascender alone leaves the text sitting half a descender too low.
  int getFontDescenderSize(int fontId) const;
  // Ink extent of THIS string, as offsets from the y that would be handed to
  // drawText(). Font-wide ascender/descender describe what a face COULD reach,
  // not what a given word does: centring "Selected" on the full box reserves
  // descender space nothing occupies and the label rides high, while centring on
  // the ascender alone leaves it low. Walks the string's glyphs and skips blanks.
  // Returns false (and zeroes both) for empty text or an unknown font.
  //
  // Cheap for the built-in UI faces. An SD-backed font may hit the card for a
  // glyph it has not cached, so do not call this per frame in a hot path.
  bool getTextInkBounds(int fontId, const char* text, int& inkTop, int& inkBottom,
                        EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  int getLineHeight(int fontId) const;
  int getLineHeight(int fontId, float compression) const;
  std::string truncatedText(int fontId, const char* text, int maxWidth,
                            EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  /// Word-wrap \p text into at most \p maxLines lines, each no wider than
  /// \p maxWidth pixels. Overflowing words and excess lines are UTF-8-safely
  /// truncated with an ellipsis (U+2026).
  std::vector<std::string> wrappedText(int fontId, const char* text, int maxWidth, int maxLines,
                                       EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;

  // Helper for drawing rotated text (90 degrees clockwise, for side buttons)
  void drawTextRotated90CW(int fontId, int x, int y, const char* text, bool black = true,
                           EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  int getTextHeight(int fontId) const;

  // Grayscale functions
  void setRenderMode(const RenderMode mode) { this->renderMode = mode; }
  RenderMode getRenderMode() const { return renderMode; }
  // Text AA strength for the grayscale passes. Renderer state (like renderMode)
  // rather than a drawText parameter so reader activities can set it once from
  // SETTINGS.textAntiAliasing without threading it through every call site.
  void setGrayscaleAaStrength(const GrayscaleAaStrength s) { this->grayscaleAaStrength = s; }
  GrayscaleAaStrength getGrayscaleAaStrength() const { return grayscaleAaStrength; }
  // Announce that this render is inverted AND will run the grayscale overlay.
  // Scope it to the render (set on entry, clear on every exit) — leaving it on
  // would render UI chrome with its antialiased levels unpainted and nothing
  // coming to fill them in.
  void setDarkModeAntiAliasing(const bool on) { this->darkModeAa = on; }
  bool getDarkModeAntiAliasing() const { return darkModeAa; }
  // The level -> (base pass, MSB plane, LSB plane) split for the current
  // strength and mode. One place, so the BW pass and the two grayscale passes
  // cannot disagree about which level belongs where.
  GlyphAa::Planes getGlyphAaPlanes() const {
    return GlyphAa::planes(static_cast<GlyphAa::Strength>(grayscaleAaStrength), darkModeAa);
  }
  // Grayscale preconditioning settle pass (no-op on X4). The rect overload
  // takes the gray region in LOGICAL screen coordinates and rotates it to the
  // panel; the no-arg overload settles the full frame. Call after the BW base
  // frame is displayed and before the grayscale planes are written.
  void preconditionGrayscale() const;
  void preconditionGrayscale(int x, int y, int w, int h) const;
  // Display the framebuffer as the base frame for a grayscale overlay that
  // follows (X3: OEM differential base waveform; others: plain display with
  // `fallback`).
  void displayGrayscaleBase(HalDisplay::RefreshMode fallback = HalDisplay::HALF_REFRESH) const;
  void copyGrayscaleLsbBuffers() const;
  void copyGrayscaleMsbBuffers() const;
  void displayGrayBuffer() const;

  // Tiled grayscale (X4): stream one band of a plane straight to controller RAM
  // from `scratch` (panelWidthBytes * numRows, physical rows [yStart, yStart+
  // numRows)), bypassing the framebuffer. supportsStripGrayscale() gates use.
  void writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t* scratch, int yStart, int numRows) const;
  bool supportsStripGrayscale() const;
  bool storeBwBuffer();    // Returns true if buffer was stored successfully
  void restoreBwBuffer();  // Restore and free the stored buffer
  void cleanupGrayscaleWithFrameBuffer() const;

  // Font helpers
  const uint8_t* getGlyphBitmap(const EpdFontData* fontData, const EpdGlyph* glyph) const;

  // Lend the 48 KB framebuffer's bytes to a memory-hungry phase (chapter
  // builds) WITHOUT freeing the allocation, so it never moves and repeated
  // loans cannot fragment the heap. Between release and restore NOTHING may
  // draw or display — the panel keeps showing its last refreshed image. The
  // lent bytes are published via buildscratch::claim() for consumers like
  // InflateStream. restore returns the buffer white, so the caller must
  // redraw the full screen; it cannot fail (no allocation involved).
  void releaseFrameBufferForBuild();
  bool restoreFrameBufferAfterBuild();
  bool hasFrameBuffer() const { return frameBuffer != nullptr; }

  // RAII form of the loan above, for blocking build regions with early-return
  // error paths: restores on scope exit (or explicitly via end()). Display the
  // popup/screen the panel should hold BEFORE constructing one. Constructing
  // while the framebuffer is already lent yields an inert loan (nesting-safe).
  class FrameBufferLoan {
   public:
    explicit FrameBufferLoan(GfxRenderer& renderer);
    ~FrameBufferLoan() { end(); }
    void end();
    FrameBufferLoan(const FrameBufferLoan&) = delete;
    FrameBufferLoan& operator=(const FrameBufferLoan&) = delete;

   private:
    GfxRenderer& renderer_;
    bool active_ = false;
  };

  // Low level functions
  uint8_t* getFrameBuffer() const;
  size_t getBufferSize() const;
  uint16_t getDisplayWidth() const { return panelWidth; }
  uint16_t getDisplayHeight() const { return panelHeight; }
  uint16_t getDisplayWidthBytes() const { return panelWidthBytes; }

  // Region cache: take a logical (orientation-aware) rect, hit the framebuffer
  // bytes that the rect can have touched, and pump them in or out of a caller-
  // supplied buffer. Used by HomeActivity to snapshot just the cover tile
  // (~16 KB in Portrait) instead of cloning the entire 48 KB framebuffer.
  //
  // getRegionByteSize: required buffer length for the rect at current orientation.
  // copyRegionToBuffer / copyBufferToRegion: false if `bufSize` is smaller than that.
  size_t getRegionByteSize(int logicalX, int logicalY, int logicalW, int logicalH) const;
  bool copyRegionToBuffer(int logicalX, int logicalY, int logicalW, int logicalH, uint8_t* buf, size_t bufSize) const;
  bool copyBufferToRegion(int logicalX, int logicalY, int logicalW, int logicalH, const uint8_t* buf,
                          size_t bufSize) const;
};
