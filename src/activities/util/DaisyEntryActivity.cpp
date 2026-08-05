#include "DaisyEntryActivity.h"

#include <I18n.h>

#include <cctype>
#include <cmath>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// Petals clockwise from 12 o'clock, each [top, middle, bottom]; the utility
// petal (backspace / swap / OK) is appended as the last petal of each ring, so
// it is wrap-adjacent to petal 1. Coverage: a-z (+ uppercase via long-press),
// 0-9, . , - _ / : ; @ ' " ! ? & ( ) + = # $ % * ~ and space -- the full WiFi
// password requirement per docs/daisywheel.md.
constexpr int ABC_CHAR_PETALS = 9;
constexpr char ABC_RING[ABC_CHAR_PETALS][3] = {{'a', 'b', 'c'}, {'d', 'e', 'f'}, {'g', 'h', 'i'},
                                               {'j', 'k', 'l'}, {'m', 'n', 'o'}, {'p', 'q', 'r'},
                                               {'s', 't', 'u'}, {'v', 'w', 'x'}, {'y', 'z', ' '}};
constexpr int NUM_CHAR_PETALS = 11;
constexpr char NUM_RING[NUM_CHAR_PETALS][3] = {{'1', '2', '3'}, {'4', '5', '6'}, {'7', '8', '9'},  {'0', '.', ','},
                                               {'-', '_', '/'}, {':', ';', '@'}, {'\'', '"', '!'}, {'?', '&', '('},
                                               {')', '+', '='}, {'#', '$', '%'}, {'*', '~', ' '}};

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
  requestUpdate();
}

void DaisyEntryActivity::onExit() { Activity::onExit(); }

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

void DaisyEntryActivity::slotCenter(const int petal, const int slot, int& outX, int& outY) const {
  const float step = petalStep(petalCount());
  const float theta = static_cast<float>(petal) * step;
  const float rm = (RADIUS_HUB + RADIUS_OUTER) / 2.0f;
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
  outX = WHEEL_CX + static_cast<int>(rm * sinf(theta));
  // The column reads upright in every petal: top char at side-Up everywhere.
  outY = WHEEL_CY - static_cast<int>(rm * cosf(theta)) + (slot - 1) * lineH;
}

void DaisyEntryActivity::handleTap(const int x, const int y) {
  const float dx = static_cast<float>(x - WHEEL_CX);
  const float dy = static_cast<float>(y - WHEEL_CY);
  const float r = sqrtf(dx * dx + dy * dy);
  if (r < static_cast<float>(RADIUS_HUB) || r > static_cast<float>(RADIUS_OUTER)) return;
  const int n = petalCount();
  const float step = petalStep(n);
  float theta = atan2f(dx, -dy);  // 0 at 12 o'clock, clockwise positive
  if (theta < 0) theta += 2.0f * static_cast<float>(M_PI);
  const int petal = static_cast<int>(theta / step + 0.5f) % n;
  if (petal != petalIdx) {
    petalIdx = petal;  // first tap focuses
    requestUpdate();
    return;
  }
  // Tap on the focused petal types the nearest char slot.
  int bestSlot = 1;
  long bestDist = -1;
  for (int slot = 0; slot < 3; slot++) {
    int sx = 0;
    int sy = 0;
    slotCenter(petal, slot, sx, sy);
    const long dist = static_cast<long>(sx - x) * (sx - x) + static_cast<long>(sy - y) * (sy - y);
    if (bestDist < 0 || dist < bestDist) {
      bestDist = dist;
      bestSlot = slot;
    }
  }
  tapPick(bestSlot);
}

void DaisyEntryActivity::loop() {
  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    handleTap(tx, ty);
    return;
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
    }
    if (activePick == pick.slot && !pickHandled && mappedInput.isPressed(pick.button) &&
        mappedInput.getHeldTime() > LONG_PRESS_MS) {
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
      int fontId = UI_12_FONT_ID;
      if (utility) {
        label = slot == 0 ? tr(STR_KEY_DEL) : (slot == 1 ? swapLabel : tr(STR_OK_BUTTON));
        fontId = SMALL_FONT_ID;
      } else {
        const char c = slotChar(i, slot);
        if (c == ' ') {
          label = "sp";
          fontId = SMALL_FONT_ID;
        } else {
          buf[0] = c;
        }
      }
      const int w = renderer.getTextWidth(fontId, label);
      renderer.drawText(fontId, sx - w / 2, sy - renderer.getLineHeight(fontId) / 2, label, !focused);
    }
  }

  // Hub: ring tag above a short tail preview with a block cursor.
  {
    const char* tag = ringIdx == 0 ? tr(STR_RING_ABC) : tr(STR_RING_123);
    const int tagW = renderer.getTextWidth(SMALL_FONT_ID, tag);
    renderer.drawText(SMALL_FONT_ID, WHEEL_CX - tagW / 2, WHEEL_CY - renderer.getLineHeight(SMALL_FONT_ID) - 10, tag,
                      true);
    std::string tail = displayText();
    while (!tail.empty() && renderer.getTextWidth(UI_12_FONT_ID, tail.c_str()) > RADIUS_HUB * 2 - 30) {
      size_t cut = 1;
      while (cut < tail.length() && (static_cast<uint8_t>(tail[cut]) & 0xC0) == 0x80) cut++;
      tail.erase(0, cut);
    }
    const int tailW = renderer.getTextWidth(UI_12_FONT_ID, tail.c_str());
    const int tailX = WHEEL_CX - (tailW + 4) / 2;
    renderer.drawText(UI_12_FONT_ID, tailX, WHEEL_CY, tail.c_str());
    renderer.fillRect(tailX + tailW + 1, WHEEL_CY, 2, lineHeight, true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, "^", "v");

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
