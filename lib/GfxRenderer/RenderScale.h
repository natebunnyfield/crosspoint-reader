#pragma once

// The supersampling factor between the LOGICAL coordinate space the firmware
// lays out in and the PHYSICAL framebuffer it paints into.
//
// THIS HEADER EXISTS BECAUSE THE FACTOR USED TO BE A CONSTANT AND IS NOW,
// ON HOST BUILDS ONLY, A VALUE LATCHED ONCE AT STARTUP. The distinction
// matters more than it looks:
//
//   * `kRenderScaleMax` is the compile-time CEILING. Everything sized at
//     compile time -- the simulator's four framebuffer arrays, which builtin
//     hi-res font tiers get compiled in, which `#if` blocks exist at all --
//     is sized and gated on this. It is `CROSSPOINT_RENDER_SCALE`, i.e.
//     exactly what the whole codebase used to mean by that macro.
//
//   * `renderScale()` is the ACTIVE factor. Every arithmetic use -- the
//     drawPixel block, the logical<->device conversions, the SD hi-res tier
//     directory name -- reads this.
//
// On device the two are the same thing and `renderScale()` is `constexpr 1`,
// so every expression folds exactly as it did before this header existed.
// `CROSSPOINT_RENDER_SCALE_RUNTIME` is defined only by the simulator and iOS
// builds, and it is the ONLY thing that turns the active factor into storage.
//
// WHY LATCHED AND NOT LIVE. The ceiling gates preprocessor conditionals and
// sizes static arrays; those cannot move after the compiler has run. What the
// latch buys is that the ACTIVE factor no longer has to equal the ceiling, so
// one binary compiled at the ceiling can render at any factor at or below it.
// It must be set before HalDisplay::begin() allocates the panel texture and
// before setupDisplayAndFonts() registers the hi-res companions; after that
// point the framebuffer geometry and the glyph tier are committed for the
// life of the process. Changing it therefore takes an app relaunch, which is
// stated in the owner-facing Settings footer rather than left to be
// discovered. See docs/ios-render-scale.md.

#ifndef CROSSPOINT_RENDER_SCALE
#define CROSSPOINT_RENDER_SCALE 1
#endif

namespace cp {

// Compile-time ceiling. Static allocations and `#if` gating use this.
inline constexpr int kRenderScaleMax = CROSSPOINT_RENDER_SCALE;

#if defined(CROSSPOINT_RENDER_SCALE_RUNTIME) && CROSSPOINT_RENDER_SCALE_RUNTIME

// Defaults to the ceiling, so a host build that never calls setRenderScale()
// behaves byte-for-byte like the compile-time-constant build it replaced.
// That is deliberate: it makes "did the latch run?" a question about the
// owner's setting rather than about whether rendering works at all.
inline int g_renderScale = kRenderScaleMax;

inline int renderScale() { return g_renderScale; }

// Clamped to [1, kRenderScaleMax]. Out-of-range input is a stale preference or
// a hand-edited plist, not a reason to render into a buffer that is too small;
// clamping DOWN to the ceiling is the safe direction because every static
// allocation is sized for the ceiling.
//
// Call exactly once, before HalDisplay::begin() and before fonts are
// registered. Calling it later does not crash but leaves the framebuffer and
// the glyph tier disagreeing with the arithmetic, which is the mixed-resolution
// bug in its worst form.
inline void setRenderScale(const int scale) {
  g_renderScale = scale < 1 ? 1 : (scale > kRenderScaleMax ? kRenderScaleMax : scale);
}

#else

// Device, and any host build that did not opt in: a constant, and every
// `const int S = cp::renderScale();` in the tree folds to a literal.
inline constexpr int renderScale() { return kRenderScaleMax; }

#endif

}  // namespace cp
