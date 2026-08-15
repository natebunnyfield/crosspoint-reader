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
// in a plane pass clears a bit in a plane that starts cleared.
//
// Every font in this firmware is now 2-bit. The UI chrome cuts were the last
// 1-bit faces and were rebuilt on 2026-08-14 -- 2-bit but deliberately
// UNCOMPRESSED, at +122,528 B of flash, because compressing them would have
// been 25 KB cheaper still and 6.1x slower on the colophon (chrome has no
// PrewarmScope, so every font/style switch re-inflates a group on the render
// path). docs/two-bit-chrome.md has the tables.
//
// So the font-depth blocker that made chrome AA a no-op is gone. What remains
// is the COST above, and it still rules out most of the chrome: measured, the
// overlay makes a settings-list repaint 10x and a Home repaint 20x, so only the
// colophon and the file viewer take it -- the two chrome surfaces you page
// through rather than move a selection around. test/text_antialiasing pins both
// directions of the depth rule, so a face that regresses to 1-bit is caught
// here rather than on a panel.
//
// The TEXT-ONLY rule (TextAntiAliasingPass.h) is enforced, not just documented:
// wrap the callback body in a GfxRenderer::TextOnlyScope and every non-glyph
// primitive it calls becomes a no-op for the duration, so an existing render
// body can be handed to the overlay without a hand-maintained text-only copy
// that drifts out of step with it.
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

// UI chrome draws at 8, 10 and 12 pt, and at those sizes AA_STANDARD is a
// REGRESSION rather than an improvement, so the chrome overlay pins AA_CRISP
// instead of following the reader's setting.
//
// Standard sends glyph level 1 (high coverage) to dark gray. In reading sizes
// that level is edge pixels; at 8-12 pt it is a large share of the STEM, so the
// whole face lifts off black and reads washed out and thinner rather than
// smoother -- measured on the settings header and the button hints, and visible
// at any zoom. Crisp leaves level 1 painted black by the base pass and lifts
// only level 2, the faintest coverage, which is the only part that was ever
// aliasing. See docs/two-bit-chrome.md for the crops.
//
// It is a pin, not a second setting: SETTINGS.textAntiAliasing is itself
// pinned to TEXT_AA_STANDARD by normalizeRetiredSettings() and has no device
// control, so following it would mean every chrome surface always taking the
// one strength that hurts. enabled() is still honoured, so Off is Off.
template <typename F>
inline void overlayChromeIfEnabled(GfxRenderer& renderer, F&& draw) {
  if (!enabled()) return;
  using Fn = std::remove_reference_t<F>;
  overlay(renderer, GfxRenderer::AA_CRISP, [](void* ctx) { (*static_cast<Fn*>(ctx))(); }, static_cast<void*>(&draw));
}

}  // namespace TextAa
