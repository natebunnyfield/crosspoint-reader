#include "KeyboardPanel.h"

#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "notes/DaisyRings.h"
#include "notes/Grid13Layout.h"

namespace fui = freeink::ui;

namespace notes {
namespace {

// The panel types on Confirm RELEASE (NoteEditorActivity::handlePanelKey), so
// activate() can only ever emit KeyboardKey::output — there is no long-press
// path here at all. Drawing the SDK's corner alt hints would advertise
// characters this surface cannot produce, and at panel key sizes they crowd the
// label besides. So the panel renders a mirror of the layout with `alt`
// cleared; the tables themselves are untouched, which matters because
// grid13::SL_LAYOUT is SHARED with the full-screen KeyboardEntryActivity, and
// that one does have long-press and must keep its hints.
//
// Rows with no alt are aliased to the original, so only the hinted rows cost
// storage: 13-grid's number + symbol rows are the worst case at 26 keys
// (QWERTY's digit row is 10). Static rather than a member because exactly one
// activity — so one panel — is on screen at a time, and the render task is the

}  // namespace

void KeyboardPanel::begin() {
  grid13_ = SETTINGS.keyboardLayout == CrossPointSettings::KEYBOARD_GRID13;
  daisy_ = SETTINGS.keyboardLayout == CrossPointSettings::KEYBOARD_DAISY;
  shift_ = false;
  symbols_ = false;
  row_ = 0;
  col_ = 0;
  petal_ = 0;
  ringIdx_ = 0;
}

const fui::KeyboardLayout& KeyboardPanel::layout() const {
  if (grid13_) return grid13::SL_LAYOUT;
  return fui::builtinKeyboardLayout(fui::KeyboardLayoutId::QwertyEn, shift_, symbols_, /*numberRow=*/true);
}

// Unit helpers mirroring KeyboardEntryActivity: 13-Grid rows all sum to 13
// width units, so a column anchor expressed in UNITS survives a vertical move
// exactly, and a wide key remembers which column it was entered from.
int KeyboardPanel::keyIndexAtUnit(const int row, const int unit) const {
  const fui::KeyboardLayout& l = layout();
  if (row < 0 || row >= l.rowCount) return 0;
  const fui::KeyboardRow& r = l.rows[row];
  int acc = 0;
  for (int i = 0; i < r.count; i++) {
    acc += r.keys[i].widthUnits;
    if (unit < acc) return i;
  }
  return r.count > 0 ? r.count - 1 : 0;
}

void KeyboardPanel::keyUnitSpan(const int row, const int keyIdx, int& start, int& end) const {
  const fui::KeyboardLayout& l = layout();
  start = 0;
  end = 1;
  if (row < 0 || row >= l.rowCount) return;
  const fui::KeyboardRow& r = l.rows[row];
  int acc = 0;
  for (int i = 0; i < r.count && i <= keyIdx; i++) {
    start = acc;
    acc += r.keys[i].widthUnits;
    end = acc;
  }
}

int KeyboardPanel::rowCount() const { return daisy_ ? 3 : layout().rowCount; }

int KeyboardPanel::colsInRow(const int row) const {
  if (daisy_) return DAISY_PETALS;
  const fui::KeyboardLayout& l = layout();
  if (row < 0 || row >= l.rowCount) return 0;
  return l.rows[row].count;
}

// Real daisywheel semantics, borrowed wholesale from DaisyEntryActivity: each
// petal holds three characters and the three pick buttons choose top/middle/
// bottom DIRECTLY. It is not a multi-tap cycle — an earlier version of this
// panel cycled a group on repeated presses, which is not how the wheel works
// and was rightly unusable. Left/Right rotate petals; the last petal is the
// utility one (backspace / ring swap / OK).
int KeyboardPanel::petalCount() const {
  return (ringIdx_ == 0 ? daisyrings::ABC_CHAR_PETALS : daisyrings::NUM_CHAR_PETALS) + 1;
}

char KeyboardPanel::slotChar(const int petal, const int slot) const {
  if (petal < 0 || slot < 0 || slot > 2 || petal >= petalCount() - 1) return '\0';
  return ringIdx_ == 0 ? daisyrings::ABC_RING[petal][slot] : daisyrings::NUM_RING[petal][slot];
}

KeyboardPanel::Result KeyboardPanel::activateSlot(const int slot, const bool longPress) {
  Result r;
  if (!daisy_) return r;

  if (petal_ == petalCount() - 1) {  // utility petal
    if (slot == 0) {
      r.event = Event::Backspace;
    } else if (slot == 1) {
      ringIdx_ ^= 1;  // swap rings, staying on the utility petal
      petal_ = petalCount() - 1;
    } else {
      r.event = Event::Done;
    }
    return r;
  }

  const char c = slotChar(petal_, slot);
  if (c == '\0') return r;
  r.event = Event::Character;
  // Long-press uppercases, exactly as the full-screen wheel does.
  r.ch = longPress ? static_cast<char>(toupper(static_cast<unsigned char>(c))) : c;
  return r;
}

void KeyboardPanel::moveRow(const int delta) {
  if (daisy_) return;  // the wheel has no rows; Up/Down are pick buttons
  const int rows = rowCount();
  if (rows <= 0) return;

  if (grid13_) {
    // Column anchor in units: a vertical move never shifts the column, which
    // is what makes the 3+7+3 bottom row navigable. Clamping instead (what
    // this did before) dumped the selection onto Del every time.
    row_ = (row_ + delta + rows) % rows;
    col_ = keyIndexAtUnit(row_, anchorUnit_);
    return;
  }

  // Proportional mapping keeps vertical travel intuitive between rows with
  // different key counts — a 10-key letter row over a 6-key bottom row.
  const int oldCols = colsInRow(row_);
  row_ = (row_ + delta + rows) % rows;
  const int newCols = colsInRow(row_);
  if (oldCols > 0 && newCols > 0 && oldCols != newCols) col_ = col_ * newCols / oldCols;
  col_ = std::min(std::max(col_, 0), std::max(0, newCols - 1));
}

void KeyboardPanel::moveCol(const int delta) {
  if (daisy_) {  // rotate the wheel
    const int n = petalCount();
    petal_ = (petal_ + delta + n) % n;
    return;
  }
  const int cols = colsInRow(row_);
  if (cols <= 0) return;
  col_ = (col_ + delta + cols) % cols;
  if (grid13_) {
    // Minimal movement of the anchor: a wide key remembers the column it was
    // entered from and gives it back on exit.
    int start = 0;
    int end = 0;
    keyUnitSpan(row_, col_, start, end);
    if (anchorUnit_ < start) anchorUnit_ = start;
    if (anchorUnit_ > end - 1) anchorUnit_ = end - 1;
  }
}

KeyboardPanel::Result KeyboardPanel::activate(const bool longPress) {
  Result r;
  if (daisy_) return r;  // the wheel is driven by activateSlot()

  const fui::KeyboardLayout& l = layout();
  if (row_ < 0 || row_ >= l.rowCount) return r;
  const fui::KeyboardRow& kr = l.rows[row_];
  if (col_ < 0 || col_ >= kr.count) return r;
  const fui::KeyboardKey& key = kr.keys[col_];

  switch (key.kind) {
    case fui::KeyKind::Shift:
      shift_ = !shift_;
      col_ = std::min(col_, std::max(0, colsInRow(row_) - 1));
      return r;
    case fui::KeyKind::Mode:
      symbols_ = !symbols_;
      row_ = std::min(row_, std::max(0, rowCount() - 1));
      col_ = std::min(col_, std::max(0, colsInRow(row_) - 1));
      return r;
    case fui::KeyKind::Delete:
      // Held Del clears the lot, same as the full-screen keyboard.
      r.event = longPress ? Event::ClearAll : Event::Backspace;
      return r;
    case fui::KeyKind::Ok:
      // In a hosted panel "OK" means newline, not "finish": the host activity
      // owns finishing (Back saves), and a multi-line note needs Enter far more
      // than it needs a second way to leave.
      r.event = Event::Enter;
      return r;
    default:
      break;
  }
  // Long-press yields the alt output — uppercase for a letter, the corner hint
  // for a key that declares one — matching KeyboardEntryActivity. Without this
  // the panel could not type a capital except via Shift.
  const char* out = longPress ? fui::keyboardAltOutputFor(l, key.value) : nullptr;
  if (out == nullptr) out = key.output;
  if (out != nullptr && out[0] != '\0') {
    r.event = Event::Character;
    r.ch = out[0];
    // Shift is one-shot, matching the full-screen keyboard. Without this,
    // Shift latched and "Abc" came out "ABC".
    if (shift_ && !symbols_) shift_ = false;
  }
  return r;
}

int KeyboardPanel::preferredHeight(const GfxRenderer& renderer) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Same key height as the full-screen KeyboardEntryActivity, so a key is the
  // same size wherever the owner meets one. Sizing from the glyph's line height
  // instead gave 30px rows against that keyboard's 48, which left no air around
  // the label and pushed the number row's alt hints into it.
  const int rows = rowCount();
  const int gap = metrics.keyboardKeySpacing;
  const int wanted = rows * metrics.keyboardKeyHeight + (rows > 1 ? (rows - 1) * gap : 0);
  // Never eat more than 45% of the screen: the note is the point, the keyboard
  // is the tool.
  const int cap = renderer.getScreenHeight() * 45 / 100;
  return wanted < cap ? wanted : cap;
}

void KeyboardPanel::render(GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  const auto& metrics = UITheme::getInstance().getMetrics();

  if (daisy_) {
    // The wheel drawn as a strip: petals left-to-right, each showing its three
    // characters stacked top/middle/bottom in the order the Up/Confirm/Down
    // buttons pick them. A half-height band cannot hold a readable circle, but
    // the mapping from button to character stays literal.
    const int n = petalCount();
    const int visible = n < 7 ? n : 7;
    const int cellW = width / visible;
    const int slotH = height / 3;
    for (int i = 0; i < visible; ++i) {
      const int petal = (petal_ - visible / 2 + i + n) % n;
      const int cx = x + i * cellW;
      const bool sel = petal == petal_;
      if (sel) renderer.drawRect(cx, y, cellW, height, 2, true);
      for (int slot = 0; slot < 3; ++slot) {
        char label[8];
        if (petal == n - 1) {
          static const char* const UTIL[3] = {"del", "123", "ok"};
          snprintf(label, sizeof(label), "%s", UTIL[slot]);
        } else {
          snprintf(label, sizeof(label), "%c", slotChar(petal, slot));
        }
        const int tw = renderer.getTextWidth(UI_12_FONT_ID, label);
        renderer.drawText(UI_12_FONT_ID, cx + (cellW - tw) / 2,
                          y + slot * slotH + (slotH - renderer.getLineHeight(UI_12_FONT_ID)) / 2, label);
      }
    }
    return;
  }

  fui::GfxRendererTarget target(renderer);
  target.setFont(fui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);
  target.setFont(fui::GfxRendererTarget::FONT_BODY, UI_12_FONT_ID);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  fui::InteractionBuffer<48> interactions;
  fui::Frame<48> frame(target, device, noInput, interactions);

  fui::KeyboardProps props;
  // Alt hints are shown again: the panel now HAS a long-press path
  // (activate(longPress) -> keyboardAltOutputFor), so the corner hints
  // advertise characters this surface can actually produce. They were
  // suppressed while that path did not exist.
  props.layout = &layout();
  // ASCII only: the UI face has no U+21B5, so a glyph label renders blank.
  props.okLabel = "Enter";
  props.shiftLabel = tr(STR_KEY_SHIFT);
  props.modeLabel = symbols_ ? tr(STR_KEY_MODE_ABC) : tr(STR_KEY_MODE_SYMBOLS);
  props.selectedIndex = static_cast<int16_t>(selectedLogicalIndex());
  props.labelText.font = fui::GfxRendererTarget::FONT_BODY;
  props.altText.font = fui::GfxRendererTarget::FONT_SMALL;
  props.gap = static_cast<int16_t>(metrics.keyboardKeySpacing);
  props.padding = fui::Insets{0, 0, 0, 0};

  fui::keyboard(frame,
                fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(width),
                          static_cast<int16_t>(height)},
                props);
}

// keyboard() indexes keys as one flat run across rows.
int KeyboardPanel::selectedLogicalIndex() const {
  const fui::KeyboardLayout& l = layout();
  int index = 0;
  for (int r = 0; r < l.rowCount; ++r) {
    if (r == row_) return index + col_;
    index += l.rows[r].count;
  }
  return 0;
}

}  // namespace notes
