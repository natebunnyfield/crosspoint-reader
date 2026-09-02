#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <components/bars/tap-zones.h>

#include <type_traits>

#include "MappedInputManager.h"
#include "TextAntiAliasing.h"
#include "activities/ActivityManager.h"

namespace ReaderUtils {

constexpr unsigned long SKIP_HOLD_MS = 700;

enum ReaderTouchAction : freeink::ui::ActionId {
  READER_TOUCH_PREV = 1,
  READER_TOUCH_NEXT = 3,
};

// Where a +1 / -1 line-spacing step lands, given the slot it starts from.
//
// LINE_COMPRESSION has only THREE slots (TIGHT / NORMAL / WIDE — CrossPointSettings.h:118),
// so this CLAMPS rather than wraps: wrapping across a
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

struct PageTurnResult {
  bool prev;
  bool next;
};

inline PageTurnResult detectPageTurn(const MappedInputManager& input) {
  const bool usePress = SETTINGS.longPressButtonBehavior == SETTINGS.OFF;
  const bool swapFront = input.isNavDirectionSwapped();
  const auto prevButton = swapFront ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  const auto nextButton = swapFront ? MappedInputManager::Button::Left : MappedInputManager::Button::Right;
  const bool prev =
      (usePress ? (input.wasPressed(MappedInputManager::Button::PageBack) || input.wasPressed(prevButton))
                : (input.wasReleased(MappedInputManager::Button::PageBack) || input.wasReleased(prevButton)));
  const bool powerTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                         input.wasReleased(MappedInputManager::Button::Power);
  const bool next = (usePress ? (input.wasPressed(MappedInputManager::Button::PageForward) || powerTurn ||
                                 input.wasPressed(nextButton))
                              : (input.wasReleased(MappedInputManager::Button::PageForward) || powerTurn ||
                                 input.wasReleased(nextButton)));
  return {prev, next};
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
// nothing.
struct HeldTurnDirection {
  bool prev;
  bool next;
  bool any() const { return prev || next; }
};

inline HeldTurnDirection detectHeldTurnDirection(const MappedInputManager& input) {
  const bool swapFront = input.isNavDirectionSwapped();
  const auto prevButton = swapFront ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  const auto nextButton = swapFront ? MappedInputManager::Button::Left : MappedInputManager::Button::Right;
  const bool prev = input.isPressed(MappedInputManager::Button::PageBack) || input.isPressed(prevButton);
  const bool next = input.isPressed(MappedInputManager::Button::PageForward) || input.isPressed(nextButton);
  return {prev, next};
}

// Which physical side button counts as "previous" / "next" for the reader's FONT gestures.
//
// Normally the PageBack/PageForward aliases, so the user's sideButtonLayout prev/next swap
// applies to the font controls exactly as it does to paging.
//
// The exception is SIDE_BUTTONS_DISABLED, where those aliases map to NOTHING at all
// (MappedInputManager.cpp:41-62). That setting means "the side buttons must not turn pages" —
// but under FONT_SIZE_STEP the side buttons are not page-turn buttons any more, they are the
// font controls, and routing them through the paging aliases made the whole feature dead on
// exactly the devices whose owner had already decided to page with the front buttons. The
// symptom is a setting that is selectable, shows as selected, and does nothing.
//
// So in that case fall back to the raw side buttons in the natural PREV_NEXT order. Paging is
// unaffected: detectPageTurn keeps using PageBack/PageForward, which stay inert.
struct SideFontButtons {
  MappedInputManager::Button prev;
  MappedInputManager::Button next;
};

inline SideFontButtons sideFontButtons() {
  if (SETTINGS.sideButtonLayout == CrossPointSettings::SIDE_BUTTONS_DISABLED) {
    return {MappedInputManager::Button::Up, MappedInputManager::Button::Down};
  }
  return {MappedInputManager::Button::PageBack, MappedInputManager::Button::PageForward};
}

// SIDE buttons only — no front buttons. Used for the reader's font
// controls, which are deliberately a side-button-only gesture so the front
// buttons keep turning pages and nothing else.
inline HeldTurnDirection detectHeldSideDirection(const MappedInputManager& input) {
  const auto side = sideFontButtons();
  return {input.isPressed(side.prev), input.isPressed(side.next)};
}

// Release edge of a SIDE button, i.e. the end of a tap.
inline HeldTurnDirection detectSideRelease(const MappedInputManager& input) {
  const auto side = sideFontButtons();
  return {input.wasReleased(side.prev), input.wasReleased(side.next)};
}

// PRESS edge of a SIDE button, i.e. the start of a tap.
//
// Used only by the Confirm-as-modifier line-spacing chord, which must act on an EDGE and
// never on getHeldTime(): getHeldTime() is a single global chord timer (freeink-sdk
// InputManager.cpp applyStateChange stamps buttonPressStart only when nothing was down), so
// inside a Confirm+side chord it reports the time since CONFIRM went down and a 300ms side
// tap already reads as a completed long press.
//
// Same sideFontButtons() routing as its sibling detectors, so the chord follows the user's
// prev/next swap and stays reachable when side paging is Disabled.
inline HeldTurnDirection detectSidePress(const MappedInputManager& input) {
  const auto side = sideFontButtons();
  return {input.wasPressed(side.prev), input.wasPressed(side.next)};
}

struct TouchPageTurn {
  bool prev;
  bool next;
  unsigned long heldMs;
};

inline TouchPageTurn detectTouchPageTurn(GfxRenderer& renderer, const MappedInputManager& input) {
  TouchPageTurn result{false, false, 0};
  if (!SETTINGS.touchReaderControls || !input.hasTouch()) {
    return result;
  }

  int x = 0;
  int y = 0;
  if (!input.wasScreenTapped(x, y)) {
    return result;
  }

  const int16_t width = static_cast<int16_t>(renderer.getScreenWidth());
  const int16_t height = static_cast<int16_t>(renderer.getScreenHeight());
  const int16_t previousZoneWidth = width / 3;
  const freeink::ui::TapZone zones[] = {
      {freeink::ui::Rect{0, 0, previousZoneWidth, height}, READER_TOUCH_PREV},
      {freeink::ui::Rect{previousZoneWidth, 0, static_cast<int16_t>(width - previousZoneWidth), height},
       READER_TOUCH_NEXT},
  };

  for (const auto& zone : zones) {
    if (!zone.enabled || !zone.rect.contains(static_cast<int16_t>(x), static_cast<int16_t>(y))) continue;
    result.prev = zone.action == READER_TOUCH_PREV;
    result.next = zone.action == READER_TOUCH_NEXT;
    break;
  }
  result.heldMs = gpio.lastTouchHeldMs();
  return result;
}

// Reader menu opens on a downward swipe from the top edge (replaces the old center tap-and-hold).
inline bool isTouchMenuGesture(const MappedInputManager& input) {
  return SETTINGS.touchReaderControls && input.hasTouch() && input.wasMenuGesture();
}

// One helper, blocking or deferred: the async form starts the refresh and
// returns so the caller can overlap CPU work with the panel's refresh time.
// Async callers must not touch the framebuffer until
// renderer.waitRefreshComplete() and must rebuild the differential baseline
// before the next page turn (the tiled grayscale cleanup does).
inline void displayWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh, bool async = false) {
  const auto mode = (pagesUntilFullRefresh <= 1) ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
  if (async) {
    renderer.displayBufferAsync(mode);
  } else {
    renderer.displayBuffer(mode);
  }
  if (pagesUntilFullRefresh <= 1) {
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    pagesUntilFullRefresh--;
  }
}

// Map the persisted TEXT_ANTIALIASING value onto the renderer's plane-mapping
// strength. TEXT_AA_OFF has no grayscale pass, so it maps to the default.
inline GfxRenderer::GrayscaleAaStrength textAaStrength() { return TextAa::strength(); }

// Grayscale anti-aliasing pass. Renders content twice (LSB + MSB) to build the
// grayscale buffer. Only the content callback is re-rendered — status bars and
// other overlays should be drawn before calling this.
//
// The pass itself now lives in TextAa (src/TextAntiAliasing.h), shared with the
// non-reader screens that take the same overlay, and it prefers the tiled path
// when the controller supports it: 8 KB of band scratch instead of a 48 KB
// whole-frame save, on both the X4 and the X3. Unlike TextAa::overlayIfEnabled
// this does NOT check the setting — every existing caller already gates on it.
template <typename RenderFn>
void renderAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn) {
  using Fn = std::remove_reference_t<RenderFn>;
  TextAa::overlay(
      renderer, textAaStrength(), [](void* ctx) { (*static_cast<Fn*>(ctx))(); }, static_cast<void*>(&renderFn));
}

struct BackNavCallback {
  void* ctx;
  void (*fn)(void*);
};

// Returns true if the back button was consumed (caller should return).
//
// Back in a reader is ONE gesture: a press, acted on at release, pops the
// reader. Until 2026-09-02 a 1 s hold went to the file browser instead
// (`backShortToFileBrowser` swapped the two, and has been a constexpr 0 since
// the settings reduction). Owner ruling 2026-09-02 killed the hold, matching
// the 2026-09-01 ruling that removed held-Back from FileBrowser and
// FileManager (docs/hold-gestures.md): the hold was the last held-Back in the
// firmware, it was timed on the SDK's global chord timer
// (docs/ux-navigation-audit-2026-09-02.md F8), and the file browser is one
// press away on Home. `goHome` and `filePath` stay in the signature so the
// four callers did not have to move; neither is read.
inline bool handleBackNavigation(const MappedInputManager& mappedInput, ActivityManager& activityManager,
                                 const char* /*filePath*/, BackNavCallback /*goHome*/) {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.popActivity();
    return true;
  }
  return false;
}

}  // namespace ReaderUtils
