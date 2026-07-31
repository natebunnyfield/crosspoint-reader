#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <HalTiltSensor.h>
#include <Logging.h>

#include "MappedInputManager.h"

namespace ReaderUtils {

constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long SKIP_HOLD_MS = 700;
constexpr unsigned long BOOKMARK_HOLD_MS = 400;
constexpr unsigned long BOOKMARK_MESSAGE_DURATION_MS = 2500;

// Where a +1 / -1 font size step lands, given the slot it starts from.
//
// Clamps rather than wraps, matching FontSelectionActivity::changeFontSize: with only four
// slots, wrapping from X Large straight back to Small reads as a glitch, and clamping makes
// the ends of the range perceptible. Returning `current` unchanged at either end is the
// caller's signal that there is nothing to persist and nothing to re-paginate — which is
// what keeps a long-press at the end of the ramp from writing settings.json (Resource
// Protocol 8: guard every write with a value-change check).
//
// Pure and total by construction (no SETTINGS read, no I/O), so the whole decision is
// verifiable host-side without linking an activity.
constexpr uint8_t steppedFontSize(const uint8_t current, const int delta) {
  int next = static_cast<int>(current) + delta;
  constexpr int last = static_cast<int>(CrossPointSettings::FONT_SIZE_COUNT) - 1;
  if (next < 0) next = 0;
  if (next > last) next = last;
  return static_cast<uint8_t>(next);
}
static_assert(steppedFontSize(CrossPointSettings::SMALL, -1) == CrossPointSettings::SMALL,
              "A step below Small must clamp, not wrap to X Large");
static_assert(steppedFontSize(CrossPointSettings::EXTRA_LARGE, +1) == CrossPointSettings::EXTRA_LARGE,
              "A step above X Large must clamp, not wrap to Small");
static_assert(steppedFontSize(CrossPointSettings::MEDIUM, +1) == CrossPointSettings::LARGE, "Medium + 1 == Large");
static_assert(steppedFontSize(CrossPointSettings::MEDIUM, -1) == CrossPointSettings::SMALL, "Medium - 1 == Small");

// Where a +1 / -1 line-spacing step lands, given the slot it starts from.
//
// LINE_COMPRESSION has only THREE slots (TIGHT / NORMAL / WIDE — CrossPointSettings.h:118),
// so this CLAMPS for the same reason steppedFontSize does, only more so: wrapping across a
// three-value range means one step at either end jumps the leading the whole way to the
// opposite extreme, which reads as a malfunction rather than a range end. Clamping makes
// "you are at the tightest setting" perceptible.
//
// The values are persisted indices (CrossPointSettings.cpp:250 validates against
// LINE_COMPRESSION_COUNT), so this deliberately introduces no new slot and changes no
// existing slot's meaning; it only walks the three that already exist.
//
// Returning `current` unchanged at either end is the caller's signal that there is nothing
// to persist and nothing to re-paginate — which is what keeps a repeated press at the end of
// the ramp from writing settings.json (Resource Protocol 8) and from spending a full e-ink
// refresh redrawing an identical page.
//
// Pure and total by construction (no SETTINGS read, no I/O), so the whole decision is
// verifiable at compile time without linking an activity.
constexpr uint8_t steppedLineSpacing(const uint8_t current, const int delta) {
  int next = static_cast<int>(current) + delta;
  constexpr int last = static_cast<int>(CrossPointSettings::LINE_COMPRESSION_COUNT) - 1;
  if (next < 0) next = 0;
  if (next > last) next = last;
  return static_cast<uint8_t>(next);
}
static_assert(CrossPointSettings::LINE_COMPRESSION_COUNT == 3,
              "steppedLineSpacing's clamp-don't-wrap rationale assumes the three TIGHT/NORMAL/WIDE slots");
static_assert(steppedLineSpacing(CrossPointSettings::TIGHT, -1) == CrossPointSettings::TIGHT,
              "A step below Tight must clamp, not wrap to Wide");
static_assert(steppedLineSpacing(CrossPointSettings::WIDE, +1) == CrossPointSettings::WIDE,
              "A step above Wide must clamp, not wrap to Tight");
static_assert(steppedLineSpacing(CrossPointSettings::NORMAL, +1) == CrossPointSettings::WIDE, "Normal + 1 == Wide");
static_assert(steppedLineSpacing(CrossPointSettings::NORMAL, -1) == CrossPointSettings::TIGHT, "Normal - 1 == Tight");

inline void applyOrientation(GfxRenderer& renderer, const uint8_t orientation) {
  switch (orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }
}

struct PageTurnResult {
  bool prev;
  bool next;
  bool fromTilt;
};

inline PageTurnResult detectPageTurn(const MappedInputManager& input) {
  const bool usePress = SETTINGS.longPressButtonBehavior == SETTINGS.OFF;
  const bool tiltNext = SETTINGS.tiltPageTurn && halTiltSensor.wasTiltedForward();
  const bool tiltPrev = SETTINGS.tiltPageTurn && halTiltSensor.wasTiltedBack();
  const bool swapFront = input.isNavDirectionSwapped();
  const auto prevButton = swapFront ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  const auto nextButton = swapFront ? MappedInputManager::Button::Left : MappedInputManager::Button::Right;
  const bool prev =
      tiltPrev ||
      (usePress ? (input.wasPressed(MappedInputManager::Button::PageBack) || input.wasPressed(prevButton))
                : (input.wasReleased(MappedInputManager::Button::PageBack) || input.wasReleased(prevButton)));
  const bool powerTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                         input.wasReleased(MappedInputManager::Button::Power);
  const bool next = tiltNext || (usePress ? (input.wasPressed(MappedInputManager::Button::PageForward) || powerTurn ||
                                             input.wasPressed(nextButton))
                                          : (input.wasReleased(MappedInputManager::Button::PageForward) || powerTurn ||
                                             input.wasReleased(nextButton)));
  return {prev, next, tiltPrev || tiltNext};
}

// Which page-turn direction is being PHYSICALLY HELD right now, as opposed to
// detectPageTurn's edge triggers.
//
// This is what lets a long-press act while the button is still down: paired with
// getHeldTime() it is the `isPressed(X) && getHeldTime()` shape, which — unlike
// `wasReleased(X) && getHeldTime()` — reports a real duration on BOTH the device
// and the host simulator. (The simulator's getHeldTime only counted buttons still
// down, so it returned 0 on the release frame; see
// tools/patches/0001-crosspoint-simulator-halgpio-completed-hold.patch.)
//
// Same button set and the same front-button swap as detectPageTurn, so a
// hold-to-act gesture covers exactly what a page turn covers — including being
// inert when side buttons are Disabled, since PageBack/PageForward then map to
// nothing. Tilt is deliberately excluded: a tilt is an instantaneous event with no
// hold to measure.
struct HeldTurnDirection {
  bool prev;
  bool next;
  bool any() const { return prev || next; }
};

inline HeldTurnDirection detectHeldTurnDirection(const MappedInputManager& input) {
  const bool swapFront = input.isNavDirectionSwapped();
  const auto prevButton = swapFront ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  const auto nextButton = swapFront ? MappedInputManager::Button::Left : MappedInputManager::Button::Right;
  const bool prev =
      input.isPressed(MappedInputManager::Button::PageBack) || input.isPressed(prevButton);
  const bool next =
      input.isPressed(MappedInputManager::Button::PageForward) || input.isPressed(nextButton);
  return {prev, next};
}

// SIDE buttons only — no front buttons, no tilt. Used for the reader's font
// controls, which are deliberately a side-button-only gesture so the front
// Left/Right keep turning pages.
//
// Goes through PageBack/PageForward rather than raw Up/Down so the user's
// sideButtonLayout swap still applies, and so the gesture is inert when side
// buttons are set to Disabled (both map to nothing then).
inline HeldTurnDirection detectHeldSideDirection(const MappedInputManager& input) {
  return {input.isPressed(MappedInputManager::Button::PageBack),
          input.isPressed(MappedInputManager::Button::PageForward)};
}

// Release edge of a SIDE button, i.e. the end of a tap.
inline HeldTurnDirection detectSideRelease(const MappedInputManager& input) {
  return {input.wasReleased(MappedInputManager::Button::PageBack),
          input.wasReleased(MappedInputManager::Button::PageForward)};
}

// PRESS edge of a SIDE button, i.e. the start of a tap.
//
// Used only by the Confirm-as-modifier line-spacing chord, which must act on an EDGE and
// never on getHeldTime(): getHeldTime() is a single global chord timer (freeink-sdk
// InputManager.cpp applyStateChange stamps buttonPressStart only when nothing was down), so
// inside a Confirm+side chord it reports the time since CONFIRM went down and a 300ms side
// tap already reads as a completed long press.
//
// Same PageBack/PageForward routing as its sibling detectors, so the user's sideButtonLayout
// swap still applies and the chord is inert when side buttons are set to Disabled.
inline HeldTurnDirection detectSidePress(const MappedInputManager& input) {
  return {input.wasPressed(MappedInputManager::Button::PageBack),
          input.wasPressed(MappedInputManager::Button::PageForward)};
}

// FRONT Left/Right only — the mirror of detectHeldSideDirection, and the reason it exists:
// FONT_SIZE_STEP was a SIDE-button-only gesture, so on a device whose owner turns pages with
// the front buttons (or has sideButtonLayout == SIDE_BUTTONS_DISABLED, which makes
// PageBack/PageForward map to nothing at all) the setting had no reachable gesture and looked
// simply broken. CHAPTER_SKIP, the choice next to it in the same list, has always acted on
// BOTH front and side because it keys off detectPageTurn — so the asymmetry was the defect,
// not the expectation.
//
// Same front-button set and the same frontButtonFollowOrientation swap as detectPageTurn, so
// "next" here is the same physical button that pages forward.
inline HeldTurnDirection detectHeldFrontDirection(const MappedInputManager& input) {
  const bool swapFront = input.isNavDirectionSwapped();
  const auto prevButton = swapFront ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  const auto nextButton = swapFront ? MappedInputManager::Button::Left : MappedInputManager::Button::Right;
  return {input.isPressed(prevButton), input.isPressed(nextButton)};
}

// Release edge of a FRONT page-turn button. Needed to swallow the release that ends a front
// hold: detectPageTurn is RELEASE-triggered whenever longPressButtonBehavior != OFF, so an
// unswallowed release would turn the page on top of the size step the hold just made.
inline HeldTurnDirection detectFrontRelease(const MappedInputManager& input) {
  const bool swapFront = input.isNavDirectionSwapped();
  const auto prevButton = swapFront ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  const auto nextButton = swapFront ? MappedInputManager::Button::Left : MappedInputManager::Button::Right;
  return {input.wasReleased(prevButton), input.wasReleased(nextButton)};
}

inline void displayWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh) {
  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }
}

// Grayscale anti-aliasing pass. Renders content twice (LSB + MSB) to build
// the grayscale buffer. Only the content callback is re-rendered — status bars
// and other overlays should be drawn before calling this.
// Kept as a template to avoid std::function overhead; instantiated once per reader type.
template <typename RenderFn>
void renderAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn) {
  if (!renderer.storeBwBuffer()) {
    LOG_ERR("READER", "Failed to store BW buffer for anti-aliasing");
    return;
  }

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderFn();
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderFn();
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);

  renderer.restoreBwBuffer();
}

}  // namespace ReaderUtils
