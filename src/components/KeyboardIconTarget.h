// A DrawTarget that swaps the SDK's 16px backspace mask for art at the
// resolution the key is actually drawn at.
//
// Both keyboards need this, which is why it is here rather than inside one of
// them: KeyboardEntryActivity (full screen) and notes::KeyboardPanel (the
// split-screen strip in Create Note and Claude) both call fui::keyboard() with
// FONT_BODY = UI_12_FONT_ID, so both get the same 32px icon box and the same
// 2x-upscaled 16px mask inside it. Only the full-screen one used to correct it,
// and only on a supersampled host build, so on every real X4 and X3 the
// backspace key was half the resolution of the text next to it.
//
// The SDK gives no injection point — KeyboardProps has no icon field — so the
// bitmap is recognized by DATA POINTER. lucideDeleteIcon16()'s array is a
// function-local static inside an inline function, so ODR guarantees one object
// program-wide and the comparison is sound. It is also fragile: if a future
// freeink-sdk bump makes that array non-inline, or the keyboard grows a second
// bitmap, this silently stops matching and the blocky icon returns with no
// compile error. KeyboardIconTest pins the assumption so that failure is loud.
#pragma once

#include <FreeInkUICore.h>
#include <GfxRenderer.h>

#include "components/icons/BackspaceIcon.h"

namespace kbicon {

// GfxRendererTarget is final, so this composes rather than inherits.
class KeyboardIconTarget final : public freeink::ui::DrawTarget {
 public:
  explicit KeyboardIconTarget(const GfxRenderer& renderer) : inner(renderer), renderer(renderer) {}

  void setFont(const freeink::ui::FontId slot, const int gfxFontId) { inner.setFont(slot, gfxFontId); }
  freeink::ui::DeviceContext deviceContext() const { return inner.deviceContext(); }

  freeink::ui::Size measureText(const freeink::ui::FontId font, const char* text,
                                const freeink::ui::TextStyle style) const override {
    return inner.measureText(font, text, style);
  }
  int16_t lineHeight(const freeink::ui::FontId font) const override { return inner.lineHeight(font); }
  void fill(const freeink::ui::Rect rect, const freeink::ui::Paint paint, const uint8_t radius = 0,
            const uint8_t corners = freeink::ui::CornersAll) override {
    inner.fill(rect, paint, radius, corners);
  }
  void stroke(const freeink::ui::Rect rect, const freeink::ui::Paint paint, const uint8_t width,
              const uint8_t radius = 0, const uint8_t corners = freeink::ui::CornersAll) override {
    inner.stroke(rect, paint, width, radius, corners);
  }
  void line(const freeink::ui::Point from, const freeink::ui::Point to, const uint8_t width,
            const freeink::ui::Paint paint) override {
    inner.line(from, to, width, paint);
  }
  void triangle(const freeink::ui::Point a, const freeink::ui::Point b, const freeink::ui::Point c,
                const freeink::ui::Paint paint) override {
    inner.triangle(a, b, c, paint);
  }
  void text(const freeink::ui::Rect rect, const char* text, const freeink::ui::TextStyle style) override {
    inner.text(rect, text, style);
  }

  void bitmap(const freeink::ui::Rect rect, const freeink::ui::BitmapRef bitmap, const freeink::ui::BitmapMode mode,
              const freeink::ui::Paint foreground = freeink::ui::Paint::solid(freeink::ui::Color::Black),
              const freeink::ui::Rotation rotation = freeink::ui::Rotation::None) override {
    if (bitmap.data == freeink::ui::lucideDeleteIcon16().data && !rect.empty()) {
#if CROSSPOINT_RENDER_SCALE > 1
      // The runtime check matters as much as the compile-time one: a host build
      // compiled at ceiling 3 can be rendering at 1, and at 1 the device-pixel
      // blit would downsample the 64px art into a 32px box instead of blitting
      // the 32px art 1:1. Same picture either way, but not the SAME PIXELS, and
      // "scale 1 on the host is what the device draws" is the whole point of
      // offering scale 1.
      if (cp::renderScale() > 1) {
        drawBackspaceDevicePixels(rect, foreground);
        return;
      }
#endif
      // Device path, and the scale-1 host path. Hand the SDK the 32px art and
      // let its own sampler run: the icon rect is 32 logical px, so this is a
      // 1:1 blit and the nearest-neighbor loop reduces to a copy. Same rect,
      // same Contain semantics, so layout and hit rects are untouched.
      inner.bitmap(
          rect,
          freeink::ui::BitmapRef{BACKSPACE_32, BACKSPACE_32_PX, BACKSPACE_32_PX, freeink::ui::BitmapFormat::Mask1},
          mode, foreground, rotation);
      return;
    }
    inner.bitmap(rect, bitmap, mode, foreground, rotation);
  }

 private:
  freeink::ui::GfxRendererTarget inner;
  // Only read by the CROSSPOINT_RENDER_SCALE > 1 device-pixel blit below, so at
  // scale 1 (the device build) it is genuinely unused rather than left over.
  [[maybe_unused]] const GfxRenderer& renderer;

#if CROSSPOINT_RENDER_SCALE > 1
  // Supersampled host build: the logical grid is coarser than the panel, so the
  // 64px art is blitted per DEVICE pixel, exactly like a hi-res font companion.
  void drawBackspaceDevicePixels(const freeink::ui::Rect rect, const freeink::ui::Paint foreground) const {
    const int S = cp::renderScale();
    constexpr int src = BACKSPACE_64_PX;
    constexpr int rowBytes = src / 8;
    const bool black = foreground.color != freeink::ui::Color::White;
    const int devW = rect.width * S;
    const int devH = rect.height * S;
    // Contain into the device rect, preserving the square aspect. keyboard()
    // hands us a square rect sized to a multiple of 16 logical px (32 at the
    // current UI metrics), so the box is 64 device px and this blits 1:1.
    const int box = devW < devH ? devW : devH;
    if (box <= 0) return;
    const int ox = rect.x * S + (devW - box) / 2;
    const int oy = rect.y * S + (devH - box) / 2;
    for (int py = 0; py < box; ++py) {
      const int sy = py * src / box;
      for (int px = 0; px < box; ++px) {
        const int sx = px * src / box;
        // Mask1: MSB-first, bit 0 = ink.
        const bool ink = ((BACKSPACE_64[sy * rowBytes + (sx >> 3)] >> (7 - (sx & 7))) & 1) == 0;
        if (ink) renderer.drawPixelDevice(ox + px, oy + py, black);
      }
    }
  }
#endif
};

}  // namespace kbicon
