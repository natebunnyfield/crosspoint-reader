#pragma once

#include <GfxRenderer.h>

// The grayscale anti-aliasing pass itself, with no dependency on the settings
// singleton — that half lives in TextAntiAliasing.h, which is what call sites
// include. Split so the pass can be linked host-side against the stub HAL
// (test/text_antialiasing) without dragging in ArduinoJson and the persisted
// settings store.
namespace TextAa {

// Re-renders TEXT ONLY, at the same positions the base frame used.
//
// TEXT ONLY is load-bearing, not advice. In a grayscale pass every write lands
// in a plane, and a plane flag means "lift this pixel toward white". A fill, a
// rule or an icon re-drawn here would therefore come out gray on the panel
// even though the base frame drew it solid black.
using DrawFn = void (*)(void*);

// Runs the two grayscale plane passes over a frame whose 1-bit base has
// ALREADY been drawn and displayed, then pushes the gray overlay.
//
// Prefers the tiled path (8 KB of band scratch) and falls back to the
// whole-frame path (a 48 KB save of the BW frame) only on a controller with no
// strip support or if that scratch will not allocate.
void overlay(GfxRenderer& renderer, GfxRenderer::GrayscaleAaStrength strength, DrawFn draw, void* ctx);

}  // namespace TextAa
