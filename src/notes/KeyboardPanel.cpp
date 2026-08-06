#include "KeyboardPanel.h"

#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "notes/Grid13Layout.h"

namespace fui = freeink::ui;

namespace notes {

void KeyboardPanel::begin() {
  grid13_ = SETTINGS.keyboardLayout == CrossPointSettings::KEYBOARD_GRID13;
  daisy_ = SETTINGS.keyboardLayout == CrossPointSettings::KEYBOARD_DAISY;
  shift_ = false;
  symbols_ = false;
  row_ = 0;
  col_ = 0;
  petal_ = 0;
  ring_ = 0;
}

const fui::KeyboardLayout& KeyboardPanel::layout() const {
  if (grid13_) return grid13::SL_LAYOUT;
  return fui::builtinKeyboardLayout(fui::KeyboardLayoutId::QwertyEn, shift_, symbols_, /*numberRow=*/true);
}

int KeyboardPanel::rowCount() const { return daisy_ ? DAISY_RINGS : layout().rowCount; }

int KeyboardPanel::colsInRow(const int row) const {
  if (daisy_) return static_cast<int>(daisyRing(row).size());
  const fui::KeyboardLayout& l = layout();
  if (row < 0 || row >= l.rowCount) return 0;
  return l.rows[row].count;
}

// The daisywheel's petals, as flat rings. The full-screen DaisyEntryActivity
// draws an actual wheel; a half-height strip has no room for that geometry, so
// the same character groups are laid out as rows here. Same letters, same
// grouping, same order — only the arrangement differs.
std::vector<std::string> KeyboardPanel::daisyRing(const int ring) const {
  switch (ring) {
    case 0:
      return {"abc", "def", "ghi", "jkl", "mno"};
    case 1:
      return {"pqrs", "tuv", "wxyz", "0-9", ".,?!"};
    default:
      return {"Space", "Del", "Enter", "Shift", "OK"};
  }
}

void KeyboardPanel::moveRow(const int delta) {
  const int rows = rowCount();
  if (rows <= 0) return;
  if (daisy_) {
    ring_ = (ring_ + delta + rows) % rows;
    petal_ = std::min(petal_, static_cast<int>(daisyRing(ring_).size()) - 1);
    return;
  }
  row_ = (row_ + delta + rows) % rows;
  col_ = std::min(col_, std::max(0, colsInRow(row_) - 1));
}

void KeyboardPanel::moveCol(const int delta) {
  const int cols = colsInRow(daisy_ ? ring_ : row_);
  if (cols <= 0) return;
  if (daisy_) {
    petal_ = (petal_ + delta + cols) % cols;
    return;
  }
  col_ = (col_ + delta + cols) % cols;
}

KeyboardPanel::Result KeyboardPanel::activate() {
  Result r;
  if (daisy_) {
    const auto ring = daisyRing(ring_);
    if (petal_ < 0 || petal_ >= static_cast<int>(ring.size())) return r;
    const std::string& cell = ring[petal_];
    if (cell == "Space") {
      r.event = Event::Character;
      r.ch = ' ';
    } else if (cell == "Del") {
      r.event = Event::Backspace;
    } else if (cell == "Enter") {
      r.event = Event::Enter;
    } else if (cell == "Shift") {
      shift_ = !shift_;
    } else if (cell == "OK") {
      r.event = Event::Done;
    } else {
      // A group cell types its letters one at a time: repeated presses cycle
      // through the group, matching the daisywheel's multi-tap idiom.
      if (cell != lastGroup_) {
        lastGroup_ = cell;
        groupIndex_ = 0;
      } else {
        groupIndex_ = (groupIndex_ + 1) % static_cast<int>(cell.size());
      }
      r.event = Event::Character;
      const char c = cell[groupIndex_];
      r.ch = shift_ ? static_cast<char>(toupper(c)) : c;
    }
    return r;
  }

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
      r.event = Event::Backspace;
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
  if (key.output != nullptr && key.output[0] != '\0') {
    r.event = Event::Character;
    r.ch = key.output[0];
  }
  return r;
}

int KeyboardPanel::preferredHeight(const GfxRenderer& renderer) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // A row needs room for the glyph plus padding on both sides AND the gap to
  // the next row. Sizing from the glyph alone left the bottom row (Del/Space/
  // OK) squeezed to a sliver against the button hints.
  const int keyH = renderer.getLineHeight(UI_12_FONT_ID) + metrics.keyboardKeySpacing * 4;
  const int wanted = keyH * rowCount() + metrics.keyboardKeySpacing * 2;
  // Never eat more than 45% of the screen: the note is the point, the keyboard
  // is the tool.
  const int cap = renderer.getScreenHeight() * 45 / 100;
  return wanted < cap ? wanted : cap;
}

void KeyboardPanel::render(GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  const auto& metrics = UITheme::getInstance().getMetrics();

  if (daisy_) {
    // Flat-ring rendering; see daisyRing().
    const int rows = rowCount();
    const int rowH = rows > 0 ? height / rows : height;
    for (int rr = 0; rr < rows; ++rr) {
      const auto ring = daisyRing(rr);
      const int cellW = ring.empty() ? width : width / static_cast<int>(ring.size());
      for (size_t c = 0; c < ring.size(); ++c) {
        const int cx = x + static_cast<int>(c) * cellW;
        const int cy = y + rr * rowH;
        const bool sel = (rr == ring_ && static_cast<int>(c) == petal_);
        if (sel) renderer.fillRect(cx + 1, cy + 1, cellW - 2, rowH - 2, true);
        renderer.drawRect(cx, cy, cellW, rowH, true);
        const int tw = renderer.getTextWidth(UI_12_FONT_ID, ring[c].c_str());
        renderer.drawText(UI_12_FONT_ID, cx + (cellW - tw) / 2, cy + (rowH - renderer.getLineHeight(UI_12_FONT_ID)) / 2,
                          ring[c].c_str(), !sel);
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
  const fui::KeyboardLayout& l = layout();
  props.layout = &l;
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
