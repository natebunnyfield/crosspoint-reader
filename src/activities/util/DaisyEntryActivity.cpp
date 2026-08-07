#include "DaisyEntryActivity.h"

#include <I18n.h>

#include <cctype>
#include <cmath>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "notes/DaisyRings.h"
#include "util/TypedTextInput.h"

namespace {

// Ring tables moved to src/notes/DaisyRings.h so the split-screen panel picks
// from the same petals. Referenced as daisyrings::ABC_RING / NUM_RING.
using daisyrings::ABC_CHAR_PETALS;
using daisyrings::ABC_RING;
using daisyrings::NUM_CHAR_PETALS;
using daisyrings::NUM_RING;

// Screen-space angle helpers: theta = 0 at 12 o'clock, increasing clockwise.
// Screen y grows downward, so a point at (r, theta) is (cx + r*sin, cy - r*cos).
float petalStep(const int count) { return 2.0f * static_cast<float>(M_PI) / static_cast<float>(count); }

}  // namespace

int DaisyEntryActivity::petalCount() const { return ringIdx == 0 ? ABC_CHAR_PETALS + 1 : NUM_CHAR_PETALS + 1; }

char DaisyEntryActivity::slotChar(const int petal, const int slot) const {
  if (petal < 0 || slot < 0 || slot > 2 || petal >= petalCount() - 1) return '\0';  // utility petal has no chars
  return ringIdx == 0 ? ABC_RING[petal][slot] : NUM_RING[petal][slot];
}

void DaisyEntryActivity::onEnter() {
  Activity::onEnter();
  ringIdx = 0;
  petalIdx = 0;
  activePick = -1;
  pickHandled = false;
  // See KeyboardEntryActivity::onEnter -- the wheel is a text field too, and a
  // host that has a real keyboard should be able to type into it.
  mappedInput.setTextEntryActive(true);
  requestUpdate();
}

void DaisyEntryActivity::onExit() {
  mappedInput.setTextEntryActive(false);
  Activity::onExit();
}

void DaisyEntryActivity::rotate(const int delta) {
  const int n = petalCount();
  petalIdx = (petalIdx + delta + n) % n;
  requestUpdate();
}

void DaisyEntryActivity::insertChar(const char c) {
  if (maxLength != 0 && text.length() + 1 > maxLength) return;
  text.push_back(c);
  requestUpdate();
}

void DaisyEntryActivity::backspace() {
  if (text.empty()) return;
  // Entries typed on the wheel are ASCII, but the initial text may not be:
  // pop a whole UTF-8 code point.
  size_t pos = text.length() - 1;
  while (pos > 0 && (static_cast<uint8_t>(text[pos]) & 0xC0) == 0x80) pos--;
  text.erase(pos);
  requestUpdate();
}

void DaisyEntryActivity::swapRing() {
  // Ruled: the flip stays on the same petal. Pressed on the utility petal it
  // lands on the other ring's utility petal; any other path preserves the
  // index, clamped to the last petal.
  const bool fromUtility = isUtilityPetal();
  ringIdx ^= 1;
  const int n = petalCount();
  petalIdx = fromUtility ? n - 1 : (petalIdx < n ? petalIdx : n - 1);
  requestUpdate();
}

void DaisyEntryActivity::tapPick(const int slot) {
  if (isUtilityPetal()) {
    // Utility petal, top->bottom: backspace / ring swap / OK.
    if (slot == 0) {
      backspace();
    } else if (slot == 1) {
      swapRing();
    } else {
      onComplete(text);
    }
    return;
  }
  const char c = slotChar(petalIdx, slot);
  if (c != '\0') insertChar(c);
}

void DaisyEntryActivity::longPick(const int slot) {
  // Ruled: long-press means uppercase and nothing else.
  const char c = slotChar(petalIdx, slot);
  if (c >= 'a' && c <= 'z') insertChar(static_cast<char>(c - 'a' + 'A'));
}

int DaisyEntryActivity::slotSpacing() const {
  // The tight case is a petal near 3/9 o'clock, where the upright column runs
  // tangentially: the top/bottom line box must stay inside the wedge, whose
  // tangential half-width at the label radius is rm*sin(step/2) (perpendicular
  // distance from the petal's center line to its boundary spoke). Clamp the
  // font's natural line advance to that minus half a line box and a 2px
  // margin, so the column fits both rings (the 12-petal 123 ring is the
  // narrowest) under any System font's metrics.
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  constexpr float rm = (RADIUS_HUB + RADIUS_OUTER) / 2.0f;
  const float halfWedge = rm * sinf(petalStep(petalCount()) / 2.0f);
  const int maxSpacing = static_cast<int>(halfWedge) - lineH / 2 - 2;
  return maxSpacing < lineH ? maxSpacing : lineH;
}

void DaisyEntryActivity::slotCenter(const int petal, const int slot, int& outX, int& outY) const {
  const float step = petalStep(petalCount());
  const float theta = static_cast<float>(petal) * step;
  constexpr float rm = (RADIUS_HUB + RADIUS_OUTER) / 2.0f;
  outX = WHEEL_CX + static_cast<int>(rm * sinf(theta));
  // The column reads upright in every petal: top char at side-Up everywhere.
  outY = WHEEL_CY - static_cast<int>(rm * cosf(theta)) + (slot - 1) * slotSpacing();
}

void DaisyEntryActivity::loop() {
  // Host keyboard first; see the same block in KeyboardEntryActivity::loop.
  // The wheel has no cursor, so typed text appends at the end -- which is
  // where the wheel's own picks land too.
  std::string typed;
  if (mappedInput.consumeTypedText(typed)) {
    const TypedTextInput::Outcome outcome = TypedTextInput::apply(
        typed,
        [this](const std::string& run) {
          // Whole-run length check rather than insertChar's per-byte one: a
          // typed run can be multi-byte UTF-8 and must not be cut in half at
          // the limit.
          if (maxLength != 0 && text.length() + run.length() > maxLength) return;
          text.append(run);
          requestUpdate();
        },
        [this] {
          const bool had = !text.empty();
          backspace();
          return had;
        });
    switch (outcome) {
      case TypedTextInput::Outcome::Committed:
        onComplete(text);
        return;
      case TypedTextInput::Outcome::Cancelled:
        onCancel();
        return;
      case TypedTextInput::Outcome::Changed:
        // insert/backspace already called requestUpdate().
        return;
      case TypedTextInput::Outcome::None:
        break;
    }
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { rotate(-1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { rotate(1); });

  // The three pick buttons share one tracker: long-press fires uppercase at
  // the threshold while held; release before it commits the plain char.
  struct PickButton {
    MappedInputManager::Button button;
    int slot;
  };
  static constexpr PickButton PICKS[] = {{MappedInputManager::Button::Up, 0},
                                         {MappedInputManager::Button::Confirm, 1},
                                         {MappedInputManager::Button::Down, 2}};
  for (const auto& pick : PICKS) {
    if (mappedInput.wasPressed(pick.button) && activePick < 0) {
      activePick = pick.slot;
      pickHandled = false;
      pickPressedAt = millis();
    }
    // Time THIS button, not whatever has been held longest. getHeldTime() is
    // global, so pressing Select while still holding Right to rotate reported a
    // hold of however long the rotation had run and fired the uppercase pick
    // immediately -- Select stopped producing the plain middle character.
    if (activePick == pick.slot && !pickHandled && mappedInput.isPressed(pick.button) &&
        millis() - pickPressedAt > LONG_PRESS_MS) {
      longPick(pick.slot);
      pickHandled = true;
    }
    if (mappedInput.wasReleased(pick.button) && activePick == pick.slot) {
      if (!pickHandled) tapPick(pick.slot);
      activePick = -1;
      pickHandled = false;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onCancel();
  }
}

std::string DaisyEntryActivity::displayText() const {
  std::string shown = text;
  if (inputType == InputType::Password) {
    // Mask all but the most recent character, matching the grid keyboard.
    for (size_t i = 0; i + 1 < shown.length(); i++) shown[i] = '*';
  }
  return shown;
}

void DaisyEntryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title.c_str());

  // --- text field: single line, tail kept visible, underlined ---
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int fieldY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 3;
  const int fieldMargin = 24;
  const int fieldWidth = pageWidth - 2 * fieldMargin;
  std::string shown = displayText();
  // Drop leading characters until the tail (plus cursor) fits.
  while (!shown.empty() && renderer.getTextWidth(UI_12_FONT_ID, shown.c_str()) > fieldWidth - 8) {
    size_t cut = 1;
    while (cut < shown.length() && (static_cast<uint8_t>(shown[cut]) & 0xC0) == 0x80) cut++;
    shown.erase(0, cut);
  }
  renderer.drawText(UI_12_FONT_ID, fieldMargin, fieldY, shown.c_str());
  const int cursorX = fieldMargin + renderer.getTextWidth(UI_12_FONT_ID, shown.c_str()) + 1;
  renderer.fillRect(cursorX, fieldY, 2, lineHeight, true);
  renderer.drawLine(fieldMargin, fieldY + lineHeight + 3, fieldMargin + fieldWidth, fieldY + lineHeight + 3, 2, true);

  // --- wheel ---
  const int n = petalCount();
  const float step = petalStep(n);
  constexpr int SEGMENTS_PER_PETAL = 6;

  // Rim and hub as segment polylines (no arc-primitive dependence).
  auto ringPoint = [](const float radius, const float theta, int& px, int& py) {
    px = WHEEL_CX + static_cast<int>(radius * sinf(theta));
    py = WHEEL_CY - static_cast<int>(radius * cosf(theta));
  };
  const int totalSegs = n * SEGMENTS_PER_PETAL;
  for (int s = 0; s < totalSegs; s++) {
    const float a0 = static_cast<float>(s) * 2.0f * static_cast<float>(M_PI) / static_cast<float>(totalSegs);
    const float a1 = static_cast<float>(s + 1) * 2.0f * static_cast<float>(M_PI) / static_cast<float>(totalSegs);
    int x0, y0, x1, y1;
    ringPoint(RADIUS_OUTER, a0, x0, y0);
    ringPoint(RADIUS_OUTER, a1, x1, y1);
    renderer.drawLine(x0, y0, x1, y1, true);
    ringPoint(RADIUS_HUB, a0, x0, y0);
    ringPoint(RADIUS_HUB, a1, x1, y1);
    renderer.drawLine(x0, y0, x1, y1, true);
  }
  // Spokes at petal boundaries.
  for (int i = 0; i < n; i++) {
    const float boundary = (static_cast<float>(i) + 0.5f) * step;
    int x0, y0, x1, y1;
    ringPoint(RADIUS_HUB, boundary, x0, y0);
    ringPoint(RADIUS_OUTER, boundary, x1, y1);
    renderer.drawLine(x0, y0, x1, y1, true);
  }

  // Focused petal: filled wedge, labels drawn in paper on ink.
  {
    const float a0 = (static_cast<float>(petalIdx) - 0.5f) * step;
    const float a1 = (static_cast<float>(petalIdx) + 0.5f) * step;
    // Polygon: inner edge at a0, outer arc a0->a1, inner arc a1->a0.
    constexpr int ARC_PTS = SEGMENTS_PER_PETAL + 1;
    int xs[2 * ARC_PTS];
    int ys[2 * ARC_PTS];
    for (int p = 0; p < ARC_PTS; p++) {
      const float t = a0 + (a1 - a0) * static_cast<float>(p) / static_cast<float>(ARC_PTS - 1);
      ringPoint(RADIUS_OUTER, t, xs[p], ys[p]);
    }
    for (int p = 0; p < ARC_PTS; p++) {
      const float t = a1 - (a1 - a0) * static_cast<float>(p) / static_cast<float>(ARC_PTS - 1);
      ringPoint(RADIUS_HUB, t, xs[ARC_PTS + p], ys[ARC_PTS + p]);
    }
    renderer.fillPolygon(xs, ys, 2 * ARC_PTS, true);
  }

  // Petal labels.
  const char* swapLabel = ringIdx == 0 ? tr(STR_RING_123) : tr(STR_RING_ABC);
  for (int i = 0; i < n; i++) {
    const bool focused = i == petalIdx;
    const bool utility = i == n - 1;
    for (int slot = 0; slot < 3; slot++) {
      int sx = 0;
      int sy = 0;
      slotCenter(i, slot, sx, sy);
      char buf[2] = {0, 0};
      const char* label = buf;
      int fontId = UI_10_FONT_ID;
      if (utility) {
        label = slot == 0 ? tr(STR_KEY_DEL) : (slot == 1 ? swapLabel : tr(STR_OK_BUTTON));
        fontId = SMALL_FONT_ID;
      } else {
        const char c = slotChar(i, slot);
        if (c == ' ') {
          // UPPERCASE, like every other special key: a lowercase label reads as
          // characters you could type (owner ruling 2026-08-06).
          label = "SPC";
          fontId = SMALL_FONT_ID;
        } else {
          buf[0] = c;
        }
      }
      const int w = renderer.getTextWidth(fontId, label);
      renderer.drawText(fontId, sx - w / 2, sy - renderer.getLineHeight(fontId) / 2, label, !focused);
    }
  }

  // The hub stays empty (ruled 2026-08-05): the text field above is the one
  // preview, and the active ring reads from the petals themselves (the utility
  // petal's swap slot names the other ring).

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void DaisyEntryActivity::onComplete(std::string result) {
  setResult(KeyboardResult{std::move(result)});
  finish();
}

void DaisyEntryActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
