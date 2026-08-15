#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>

#include <type_traits>

#include "TextAntiAliasingPass.h"

// Grayscale text anti-aliasing for a screen whose 1-bit frame has ALREADY been
// drawn and displayed.
//
// Mechanism, and the one rule that governs where this can be used at all: the
// panel's grayscale overlay is a ONE-WAY waveform. It lifts a pixel the panel
// is holding BLACK part of the way toward white, and it cannot darken a white
// one (X3: the OEM gray bank's white->black cell is deliberately dead,
// freeink-sdk .../lut/Uc8253X3Luts.h:117-120; X4: the leave-alone group is all
// zeroes and no emitted group darkens, .../lut/Ssd1677Luts.h:11-20). So this
// improves BLACK-ON-WHITE text and nothing else. Over white-on-black text — an
// inverted "Selected" pill, the keyboard's block cursor, a selected key, the
// black popup body — it is a harmless no-op that costs a re-render for
// nothing: the glyph's soft levels were already knocked out to solid white by
// the base pass, and a plane flag on a white pixel does nothing (the plane
// branch in GfxRenderer::renderCharImpl ignores the black/white argument
// entirely, so a white glyph flags its own edges and the waveform declines).
//
// Cost, which is why this is NOT applied to every screen. Each call adds one
// grayscale panel refresh on top of the BW one the caller already ran, plus
// three framebuffer-sized SPI transfers (two planes in, one RED-RAM resync
// out), plus re-rendering the callback's text once per plane per band. That is
// the same cost the reader has always paid per page turn — right on a surface
// you READ and page through, wrong on one you move a selection around or type
// into.
//
// The HARDER limit, and the one to check first before wiring a new screen:
// only a 2-BIT font can be antialiased at all. Plane flags come solely from
// the 2-bit branch of GfxRenderer::renderCharImpl, which reads a glyph's four
// coverage levels; a 1-bit glyph has no partial coverage, and its single write
// in a plane pass clears a bit in a plane that starts cleared. Every UI chrome
// font in this firmware is 1-bit — lib/EpdFont/builtinFonts holds exactly
// twelve 1-bit faces and they are the Libre Franklin 8/10/12 cuts main.cpp
// binds to SMALL_FONT_ID / UI_10_FONT_ID / UI_12_FONT_ID. So headers, list
// rows, button hints, popups, the keyboard, the colophon and the file viewer
// cannot be antialiased no matter what is plumbed to them; the pass over them
// buys a second panel refresh and zero pixels. Only the SD .cpfont reading
// cuts, the librefranklin_reader_* fallbacks and the built-in editor
// monospaces are 2-bit. test/text_antialiasing pins both directions.
namespace TextAa {

inline bool enabled() { return SETTINGS.textAntiAliasing != CrossPointSettings::TEXT_AA_OFF; }

// Map the persisted TEXT_ANTIALIASING value onto the renderer's plane-mapping
// strength. TEXT_AA_OFF has no grayscale pass, so it maps to the default.
inline GfxRenderer::GrayscaleAaStrength strength() {
  switch (SETTINGS.textAntiAliasing) {
    case CrossPointSettings::TEXT_AA_CRISP:
      return GfxRenderer::AA_CRISP;
    case CrossPointSettings::TEXT_AA_DARK:
      return GfxRenderer::AA_DARK;
    default:
      return GfxRenderer::AA_STANDARD;
  }
}

// The call sites' entry point: does nothing at all when the user has Text
// Anti-Aliasing set to Off, so the render stays byte-identical to pure BW.
//
// A template only to convert the caller's lambda into the plain function
// pointer + context pair the pass takes; the pipeline itself lives in the
// .cpp, so a new call site costs a trampoline rather than another copy of it
// (CLAUDE.md, "Template and std::function Bloat").
template <typename F>
inline void overlayIfEnabled(GfxRenderer& renderer, F&& draw) {
  if (!enabled()) return;
  using Fn = std::remove_reference_t<F>;
  overlay(renderer, strength(), [](void* ctx) { (*static_cast<Fn*>(ctx))(); }, static_cast<void*>(&draw));
}

}  // namespace TextAa
