#pragma once

#include <stdint.h>

// How a 2-bit glyph's four coverage levels are split between the black/white
// base pass and the panel's two grayscale planes.
//
// Levels here are the SWAPPED numbering renderCharImpl works in, after
// `3 - raw`:  0 = full ink, 1 = high coverage, 2 = low coverage, 3 = no ink.
//
// ---------------------------------------------------------------------------
// The hardware fact this table exists to respect
// ---------------------------------------------------------------------------
// The grayscale overlay is a ONE-WAY nudge: it can lift a BLACK pixel part of
// the way to white, and it cannot darken a white one.
//
//   * X3 (UC8253): the OEM gray bank's white->black cell is deliberately dead.
//     freeink-sdk .../lut/Uc8253X3Luts.h:117-120 — "stock's gc bank does not
//     drive white-to-black pixels during the gray nudge (its WB table equals
//     the undriven vcom/bb tables)". The two cells that DO drive, `ww_gc`
//     (0x20) and `bw_gc` (0x80), are both single VDL phases, i.e. both drive
//     toward white; only their phase length differs, which is what makes one
//     land on light gray and the other on dark.
//   * X4 (SSD1677): same shape. .../lut/Ssd1677Luts.h:11-20 — group 00 is all
//     zeroes ("leave alone"), and the firmware only ever emits three of the
//     four groups; there is no darkening group in the emitted set.
//   * The simulator states the same rule independently, in
//     crosspoint-simulator/src/GrayscalePreview.h:16-18: "Plane flags only
//     lighten base-black pixels ... the panel waveforms are not driven that
//     way, so a white pixel stays white regardless of plane bits."
//
// So EVERY level that is to end up gray must reach the panel BLACK and then be
// lifted. That is the whole content of the table below.
//
// ---------------------------------------------------------------------------
// Why dark mode needs a different split
// ---------------------------------------------------------------------------
// In light mode the page is white and the ink is black, so the base pass paints
// the antialiased levels black (they are ink) and the overlay lightens them.
//
// In dark mode HalDisplay flips the framebuffer on its way to the panel, so the
// level the base pass LEAVES ALONE is the one that reaches the panel black.
// The base pass must therefore skip the antialiased levels rather than paint
// them, and the two gray targets swap roles, because more ink now means
// lighter: a level-1 pixel (high coverage, nearly ink) wants LIGHT gray in dark
// mode where it wanted DARK gray in light mode.
//
// Both modes end up asking the panel for exactly the same physical transition —
// black lifted toward gray. Nothing here asks for a drive the waveform lacks.
namespace GlyphAa {

// Mirrors GfxRenderer::GrayscaleAaStrength; GfxRenderer static_asserts the two
// agree so this header stays free of the renderer.
enum Strength : uint8_t { Standard = 0, Crisp = 1, Dark = 2 };

// Bit N of each mask corresponds to glyph level N.
struct Planes {
  uint8_t baseInk;  // levels the BW base pass paints as ink
  uint8_t msb;      // levels flagged in the MSB plane
  uint8_t lsb;      // levels flagged in the LSB plane
};

// `darkModeAa` means "the output is inverted AND a grayscale overlay will
// actually run for this render". With no overlay coming, the base pass must
// paint every non-white level or the glyphs come out skeletal, which is why
// this is not simply HalDisplay::isInverted().
constexpr Planes planes(const Strength strength, const bool darkModeAa) {
  constexpr uint8_t L0 = 1u << 0;  // full ink
  constexpr uint8_t L1 = 1u << 1;  // high coverage
  constexpr uint8_t L2 = 1u << 2;  // low coverage

  // Plane combinations, named by the optical result on the panel. Both lift a
  // black pixel; LIGHT lifts it further.
  //   MSB only  -> light gray
  //   MSB + LSB -> dark gray
  // LSB-only is never emitted by either branch: it selects the white->black
  // cell that the OEM bank leaves passive.

  if (!darkModeAa) {
    switch (strength) {
      case Crisp:
        // level 1 hardens to solid black, level 2 keeps light gray
        return {static_cast<uint8_t>(L0 | L1 | L2), L2, 0};
      case Dark:
        // level 1 hardens to solid black, level 2 takes the heavier dark gray
        return {static_cast<uint8_t>(L0 | L1 | L2), L2, L2};
      case Standard:
      default:
        return {static_cast<uint8_t>(L0 | L1 | L2), static_cast<uint8_t>(L1 | L2), L1};
    }
  }

  switch (strength) {
    case Crisp:
      // level 1 hardens to solid ink (white on the panel); level 2 takes the
      // LIGHTER-INK gray, which in dark mode is the dark one.
      return {static_cast<uint8_t>(L0 | L1), L2, L2};
    case Dark:
      // level 1 hardens to solid ink; level 2 takes the HEAVIER-INK gray,
      // which in dark mode is the light one.
      return {static_cast<uint8_t>(L0 | L1), L2, 0};
    case Standard:
    default:
      // level 1 -> light gray (nearly ink), level 2 -> dark gray (nearly page).
      // Exactly the light-mode Standard masks with the two grays swapped.
      return {L0, static_cast<uint8_t>(L1 | L2), L2};
  }
}

}  // namespace GlyphAa
