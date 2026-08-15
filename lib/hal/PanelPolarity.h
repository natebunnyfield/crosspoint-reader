#pragma once

#include <stdint.h>

// Dark mode's byte flip, kept on THIS side of the SDK boundary.
//
// FreeInkDisplay has its own setInverted() that performs the identical flip
// inside displayBuffer() (freeink-sdk FreeInkDisplay.cpp:577-579), and that is
// what dark mode originally shipped on. The problem is everything else that
// flag switches off while it is set:
//
//   * every grayscale write is dropped   (FreeInkDisplay.cpp:779, :797, :826-839)
//   * supportsStripGrayscale() -> false  (:858-860)
//   * async refresh refused              (:557-559, :609-612)
//   * window diffs become full refreshes (:757-762)
//
// That is where dark mode's antialiasing went. Doing the same flip here leaves
// the SDK in its normal, fully featured state, so the grayscale planes, the
// overlapped page turn and the window diff all keep working in dark mode.
//
// What is lost: the panel drivers' dark-background hint, which re-drives every
// pixel on a differential refresh so the light residue of a white->black
// transition does not park in the black background (Ssd1677Driver.cpp:427-439,
// Uc8253X3Driver.cpp:195-210). setBackgroundHint() is reachable only through
// FreeInkDisplay::setInverted(), so this trade is forced by the SDK's API
// surface, not chosen. See docs/dark-mode.md.
//
// Invariant this class exists to hold: the framebuffer is LOGICAL (1 = white)
// whenever a caller may draw into it, and carries PANEL polarity only for the
// duration of a call into the SDK -- extended across an async refresh, during
// which the caller has already promised not to touch it.
class PanelPolarity {
 public:
  bool darkMode() const { return dark_; }
  bool framebufferIsPanelPolarity() const { return flipped_; }

  // Returns true when the mode actually changed.
  bool setDarkMode(const bool on) {
    if (dark_ == on) return false;
    dark_ = on;
    promotionPending_ = true;
    return true;
  }

  // A polarity change cannot be carried by a differential refresh: the panel
  // would be diffing opposite polarities against a baseline drawn in the other
  // one. One-shot -- the caller promotes that single refresh off FAST. Mirrors
  // what FreeInkDisplay.cpp:573-575 did with _inversionDirty.
  bool consumeRefreshPromotion() {
    const bool pending = promotionPending_;
    promotionPending_ = false;
    return pending;
  }

  // Both transforms are idempotent, and both compile down to one predictable
  // branch on the light-mode path.
  void toPanel(uint8_t* frameBuffer, const uint32_t size) {
    if (!dark_ || flipped_) return;
    flip(frameBuffer, size);
    flipped_ = true;
  }

  // Gated on flipped_ rather than dark_ so that turning dark mode off while an
  // async refresh is in flight still restores the buffer the caller drew.
  void toLogical(uint8_t* frameBuffer, const uint32_t size) {
    if (!flipped_) return;
    flip(frameBuffer, size);
    flipped_ = false;
  }

 private:
  // Byte-wise, exactly like the SDK's own invertBytes (FreeInkDisplay.cpp:67-72).
  // A word-wise version would need the buffer 4-byte aligned, and an unaligned
  // multi-byte load faults on RISC-V; the win over ~48 KB is not worth the
  // hazard next to a refresh measured in hundreds of milliseconds.
  static void flip(uint8_t* buffer, const uint32_t size) {
    if (!buffer) return;
    for (uint32_t i = 0; i < size; ++i) {
      buffer[i] = static_cast<uint8_t>(~buffer[i]);
    }
  }

  bool dark_ = false;
  bool flipped_ = false;
  bool promotionPending_ = false;
};
