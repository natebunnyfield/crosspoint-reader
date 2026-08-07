#include "KeyboardPanel.h"

#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"
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
// only caller; this keeps the mirror off the activity's heap allocation.
constexpr int MAX_MIRRORED_KEYS = 26;
constexpr int MAX_MIRRORED_ROWS = 6;

const fui::KeyboardLayout& withoutAltHints(const fui::KeyboardLayout& src) {
  static fui::KeyboardKey keys[MAX_MIRRORED_KEYS];
  static fui::KeyboardRow rows[MAX_MIRRORED_ROWS];
  static fui::KeyboardLayout mirrored{rows, 0};
  static const fui::KeyboardLayout* cached = nullptr;

  if (cached == &src) return mirrored;
  // Anything that outgrows the pool keeps its hints rather than rendering a
  // half-built layout.
  if (src.rowCount > MAX_MIRRORED_ROWS) return src;

  int pool = 0;
  for (uint8_t r = 0; r < src.rowCount; ++r) {
    const fui::KeyboardRow& row = src.rows[r];
    bool hinted = false;
    for (uint8_t c = 0; c < row.count && !hinted; ++c) hinted = row.keys[c].alt != nullptr;
    if (!hinted || pool + row.count > MAX_MIRRORED_KEYS) {
      rows[r] = row;
      continue;
    }
    fui::KeyboardKey* dst = keys + pool;
    for (uint8_t c = 0; c < row.count; ++c) {
      dst[c] = row.keys[c];
      dst[c].alt = nullptr;
    }
    rows[r] = fui::KeyboardRow{dst, row.count, row.insetUnits};
    pool += row.count;
  }
  mirrored.rowCount = src.rowCount;
  cached = &src;
  return mirrored;
}

}  // namespace

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
  if (daisy_) return DAISY_PETALS;
  const fui::KeyboardLayout& l = layout();
  if (row < 0 || row >= l.rowCount) return 0;
  return l.rows[row].count;
}

// The daisywheel's petals, as flat rings. The full-screen DaisyEntryActivity
// draws an actual wheel; a half-height strip has no room for that geometry, so
// the same character groups are laid out as rows here. Same letters, same
// grouping, same order — only the arrangement differs.
//
// Flash-resident and returned by pointer: this is read once per cell per
// render, and the previous std::vector<std::string> form churned twenty heap
// allocations every time the panel repainted.
const char* const* KeyboardPanel::daisyRing(const int ring) const {
  static constexpr const char* RINGS[DAISY_RINGS][DAISY_PETALS] = {{"abc", "def", "ghi", "jkl", "mno"},
                                                                   {"pqrs", "tuv", "wxyz", "0-9", ".,?!"},
                                                                   {"Space", "Del", "Enter", "Shift", "OK"}};
  return RINGS[ring < 0 || ring >= DAISY_RINGS ? DAISY_RINGS - 1 : ring];
}

void KeyboardPanel::moveRow(const int delta) {
  const int rows = rowCount();
  if (rows <= 0) return;
  if (daisy_) {
    ring_ = (ring_ + delta + rows) % rows;
    petal_ = std::min(petal_, DAISY_PETALS - 1);
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
    if (petal_ < 0 || petal_ >= DAISY_PETALS) return r;
    const char* cell = daisyRing(ring_)[petal_];
    if (strcmp(cell, "Space") == 0) {
      r.event = Event::Character;
      r.ch = ' ';
    } else if (strcmp(cell, "Del") == 0) {
      r.event = Event::Backspace;
    } else if (strcmp(cell, "Enter") == 0) {
      r.event = Event::Enter;
    } else if (strcmp(cell, "Shift") == 0) {
      shift_ = !shift_;
    } else if (strcmp(cell, "OK") == 0) {
      r.event = Event::Done;
    } else {
      // A group cell types its letters one at a time: repeated presses cycle
      // through the group, matching the daisywheel's multi-tap idiom.
      if (cell != lastGroup_) {
        lastGroup_ = cell;
        groupIndex_ = 0;
      } else {
        groupIndex_ = (groupIndex_ + 1) % static_cast<int>(strlen(cell));
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
    // Flat-ring rendering; see daisyRing(). Drawn to match what fui::keyboard()
    // gives the other two layouts, so a key looks the same whichever layout is
    // selected: no outline, black label on white, and the selected key a solid
    // black block with the label knocked out. The full grid of 1px boxes this
    // replaced read as a spreadsheet next to them.
    //
    // Ring cells are words rather than single glyphs, so borderless alone would
    // run them together — hence the hairline rules BETWEEN cells (not around
    // them), which separate without boxing.
    const int rows = rowCount();
    const int rowH = rows > 0 ? height / rows : height;
    const int cellW = width / DAISY_PETALS;
    const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    for (int rr = 0; rr < rows; ++rr) {
      const char* const* ring = daisyRing(rr);
      const int cy = y + rr * rowH;
      for (int c = 0; c < DAISY_PETALS; ++c) {
        const int cx = x + c * cellW;
        // The last cell absorbs the division remainder so the strip ends flush.
        const int w = c == DAISY_PETALS - 1 ? width - c * cellW : cellW;
        const bool sel = rr == ring_ && c == petal_;
        if (sel) renderer.fillRect(cx, cy, w, rowH, true);
        if (c > 0 && !sel) renderer.drawLine(cx, cy + rowH / 4, cx, cy + rowH - rowH / 4, true);
        const int tw = renderer.getTextWidth(UI_12_FONT_ID, ring[c]);
        renderer.drawText(UI_12_FONT_ID, cx + (w - tw) / 2, cy + (rowH - lineHeight) / 2, ring[c], !sel);
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
  props.layout = &withoutAltHints(layout());
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
