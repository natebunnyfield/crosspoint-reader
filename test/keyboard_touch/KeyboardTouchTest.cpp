// BOOX Picco keyboard touch (src/components/KeyboardTouch.h): which key a
// point in logical portrait space lands on, for every keyboard the Picco shows,
// and the tap/hold routing that turns contacts into key presses.
//
// The keyboards only ever appear in portrait: GfxRenderer's orientation is a
// compile-time Portrait (GfxRenderer.h:74), the logical screen is 480 x 800 on
// the Picco's 800 x 480 panel, and taps arrive already mapped into it by
// GfxRenderer::tapToLogical. So the cases below are the POSITIONS a keyboard
// appears in: the full-screen keyboard (13-Grid, QWERTY letters / shifted /
// symbols, English and Spanish), the Create Note / Claude panel, its daisy
// strip, and the full-screen daisy wheel on both rings.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

#include "components/EdgeSwipe.h"
#include "components/KeyboardTouch.h"
#include "notes/DaisyRings.h"
#include "notes/Grid13Layout.h"

namespace {

namespace ui = freeink::ui;

constexpr int16_t SCREEN_W = 480;  // the Picco's logical portrait screen
constexpr int16_t SCREEN_H = 800;

// LyraSix metrics on a touch board (LyraTheme.h:14-44; UITheme.cpp:39-44 zeroes
// buttonHintsHeight when hasTouch()): key height 48, key spacing 0, width 94%,
// verticalSpacing 16, keyboardVerticalOffset -7, contentSidePadding 20.
constexpr int KEY_H = 48;
constexpr int V_SPACING = 16;
constexpr int KB_OFFSET = -7;
constexpr int SIDE_PAD = 20;

// KeyboardEntryActivity::keyboardRect() with those metrics.
ui::Rect fullScreenRect(const ui::KeyboardLayout& layout) {
  const int rows = layout.rowCount;
  const int width = SCREEN_W * 94 / 100;
  const int height = rows * KEY_H;
  const int y = SCREEN_H - 0 - V_SPACING - KEY_H - height + KB_OFFSET;
  return ui::Rect{static_cast<int16_t>((SCREEN_W - width) / 2), static_cast<int16_t>(y), static_cast<int16_t>(width),
                  static_cast<int16_t>(height)};
}

// KeyboardEntryActivity's touch props (touchKeyProps()), for `rect`.
ui::KeyboardProps fullScreenProps(const ui::KeyboardLayout& layout, const ui::Rect rect, const int16_t gap = 0) {
  ui::KeyboardProps props;
  props.layout = &layout;
  props.keyAction = 1;
  props.inputMask = static_cast<uint16_t>(ui::InputTouch | ui::InputLongPress);
  props.gap = gap;
  props.padding = ui::Insets{0, 0, 0, 0};
  props.bottomHitOverflow = static_cast<int16_t>(std::max(0, SCREEN_H - (rect.y + rect.height)));
  return props;
}

// notes::KeyboardPanel's touch props (KeyboardPanel::touchKeyProps()).
ui::KeyboardProps panelProps(const ui::KeyboardLayout& layout) {
  ui::KeyboardProps props;
  props.layout = &layout;
  props.keyAction = 1;
  props.inputMask = static_cast<uint16_t>(ui::InputTouch | ui::InputLongPress);
  props.gap = 0;
  props.padding = ui::Insets{0, 0, 0, 0};
  return props;
}

// The panel's rect in the note editor: x = contentSidePadding, width = the
// screen less both paddings, height = rows * key height (under the 45% cap),
// sitting above verticalSpacing (NoteEditorActivity::applyLayout).
ui::Rect panelRect(const ui::KeyboardLayout& layout) {
  const int height = layout.rowCount * KEY_H;
  return ui::Rect{static_cast<int16_t>(SIDE_PAD), static_cast<int16_t>(SCREEN_H - V_SPACING - height),
                  static_cast<int16_t>(SCREEN_W - 2 * SIDE_PAD), static_cast<int16_t>(height)};
}

// What keyboard() DRAWS each key as, worked out independently from its layout
// arithmetic (keyboard.h:514-517 and 838-853): equal rows, per-row unit width,
// the last key of a row taking the remainder.
struct KeyBox {
  int x, y, w, h;
  int16_t value;
};

std::vector<KeyBox> drawnKeys(const ui::KeyboardLayout& layout, const ui::Rect rect, const int gap) {
  std::vector<KeyBox> out;
  const int rows = layout.rowCount;
  const int rowH = (rect.height - gap * (rows - 1)) / rows;
  for (int r = 0; r < rows; ++r) {
    const ui::KeyboardRow& row = layout.rows[r];
    int units = row.insetUnits * 2;
    for (int c = 0; c < row.count; ++c) units += row.keys[c].widthUnits ? row.keys[c].widthUnits : 1;
    const int unitW = (rect.width - gap * (row.count - 1)) / units;
    const int y = rect.y + r * (rowH + gap);
    int x = rect.x + row.insetUnits * unitW;
    const int rowRight = rect.x + rect.width - row.insetUnits * unitW;
    for (int c = 0; c < row.count; ++c) {
      const int keyUnits = row.keys[c].widthUnits ? row.keys[c].widthUnits : 1;
      const int w = c == row.count - 1 ? rowRight - x : unitW * keyUnits;
      out.push_back(KeyBox{x, y, w, rowH, row.keys[c].value});
      x += w + gap;
    }
  }
  return out;
}

// The product lookup: the buffer's own touch routing, which is what
// fui::TouchHoldRouter dispatches through.
template <size_t N>
bool keyAt(ui::InteractionBuffer<N>& hits, const int x, const int y, int16_t& value) {
  ui::InputSnapshot release{};
  release.touchReleased = true;
  release.touchX = static_cast<int16_t>(x);
  release.touchY = static_cast<int16_t>(y);
  const ui::ActionEvent event = hits.route(release);
  if (!event) return false;
  value = event.value;
  return true;
}

// Every key: its center, its four inside corners. Adjacent keys share an
// edge at gap 0, so a key's last column/row is its own and the next one's first
// is the neighbor's: these points sit on every boundary.
template <size_t N>
void expectEveryKeyHit(ui::InteractionBuffer<N>& hits, const std::vector<KeyBox>& keys) {
  ASSERT_FALSE(hits.overflowed());
  for (const KeyBox& k : keys) {
    const int pts[5][2] = {{k.x + k.w / 2, k.y + k.h / 2},
                           {k.x, k.y},
                           {k.x + k.w - 1, k.y},
                           {k.x, k.y + k.h - 1},
                           {k.x + k.w - 1, k.y + k.h - 1}};
    for (const auto& p : pts) {
      int16_t value = 0;
      ASSERT_TRUE(keyAt(hits, p[0], p[1], value)) << "no key at (" << p[0] << "," << p[1] << ")";
      EXPECT_EQ(value, k.value) << "at (" << p[0] << "," << p[1] << ")";
    }
  }
}

// ---------------------------------------------------------------------------
// Full-screen keyboard (KeyboardEntryActivity)
// ---------------------------------------------------------------------------

TEST(KeyboardTouch, Grid13FullScreenEveryKeyIsItsDrawnRect) {
  const ui::KeyboardLayout& layout = grid13::SL_LAYOUT;
  const ui::Rect rect = fullScreenRect(layout);
  // The rect the Picco draws it in: 451 x 240 at (14, 489).
  EXPECT_EQ(rect.x, 14);
  EXPECT_EQ(rect.y, 489);
  EXPECT_EQ(rect.width, 451);
  EXPECT_EQ(rect.height, 240);
  ui::InteractionBuffer<kbtouch::KEY_SLOTS> hits;
  kbtouch::buildKeyHits(hits, rect, fullScreenProps(layout, rect), SCREEN_W, SCREEN_H);
  EXPECT_EQ(hits.count(), 65u);
  expectEveryKeyHit(hits, drawnKeys(layout, rect, 0));
}

// The 65th key is the one a 64-slot table loses: 'z', last on the last row.
// This is why KeyboardEntryActivity takes kbtouch::KEY_SLOTS on the Picco.
TEST(KeyboardTouch, Grid13NeedsMoreThanSixtyFourSlots) {
  const ui::KeyboardLayout& layout = grid13::SL_LAYOUT;
  const ui::Rect rect = fullScreenRect(layout);
  const KeyBox z = drawnKeys(layout, rect, 0).back();
  ASSERT_EQ(z.value, 'z');

  ui::InteractionBuffer<64> small;
  kbtouch::buildKeyHits(small, rect, fullScreenProps(layout, rect), SCREEN_W, SCREEN_H);
  EXPECT_TRUE(small.overflowed());
  int16_t value = 0;
  EXPECT_FALSE(keyAt(small, z.x + z.w / 2, z.y + z.h / 2, value));

  ui::InteractionBuffer<kbtouch::KEY_SLOTS> hits;
  kbtouch::buildKeyHits(hits, rect, fullScreenProps(layout, rect), SCREEN_W, SCREEN_H);
  EXPECT_FALSE(hits.overflowed());
  ASSERT_TRUE(keyAt(hits, z.x + z.w / 2, z.y + z.h / 2, value));
  EXPECT_EQ(value, 'z');
}

TEST(KeyboardTouch, Grid13FullScreenOffTheKeys) {
  const ui::KeyboardLayout& layout = grid13::SL_LAYOUT;
  const ui::Rect rect = fullScreenRect(layout);
  ui::InteractionBuffer<kbtouch::KEY_SLOTS> hits;
  kbtouch::buildKeyHits(hits, rect, fullScreenProps(layout, rect), SCREEN_W, SCREEN_H);
  int16_t value = 0;
  // Above the top row: the tips and the text field are not keys.
  EXPECT_FALSE(keyAt(hits, rect.x + rect.width / 2, rect.y - 1, value));
  EXPECT_FALSE(keyAt(hits, SCREEN_W / 2, 100, value));
  // Left and right of the rows (14 px and 15 px margins: outside the 12 px edge snap).
  EXPECT_FALSE(keyAt(hits, rect.x - 1, rect.y + KEY_H / 2, value));
  EXPECT_FALSE(keyAt(hits, 0, rect.y + KEY_H / 2, value));
  EXPECT_FALSE(keyAt(hits, rect.x + rect.width, rect.y + KEY_H / 2, value));
  EXPECT_FALSE(keyAt(hits, SCREEN_W - 1, rect.y + KEY_H / 2, value));
}

// Below the last row is the bottom gap row, and with no hints bar on a touch
// board the bottom row's hit band runs to the screen's bottom edge
// (bottomHitOverflow; fingers land low on e-paper). Visual rects unchanged.
TEST(KeyboardTouch, Grid13BottomRowCatchesTheLowMiss) {
  const ui::KeyboardLayout& layout = grid13::SL_LAYOUT;
  const ui::Rect rect = fullScreenRect(layout);
  ui::InteractionBuffer<kbtouch::KEY_SLOTS> hits;
  kbtouch::buildKeyHits(hits, rect, fullScreenProps(layout, rect), SCREEN_W, SCREEN_H);
  const std::vector<KeyBox> keys = drawnKeys(layout, rect, 0);
  const KeyBox& n = keys[52];  // first key of the last row
  ASSERT_EQ(n.value, 'n');
  int16_t value = 0;
  for (const int y : {rect.y + rect.height, 760, SCREEN_H - 1}) {
    ASSERT_TRUE(keyAt(hits, n.x + n.w / 2, y, value)) << y;
    EXPECT_EQ(value, 'n') << y;
  }
}

// A gap between keys hits nothing. The theme's gap is 0 today
// (keyboardKeySpacing), so this pins the rule for any theme that sets one.
TEST(KeyboardTouch, GapsBetweenKeysHitNothing) {
  const ui::KeyboardLayout& layout = grid13::SL_LAYOUT;
  ui::Rect rect = fullScreenRect(layout);
  constexpr int16_t GAP = 4;
  rect.height = static_cast<int16_t>(rect.height + GAP * (layout.rowCount - 1));
  rect.y = static_cast<int16_t>(rect.y - GAP * (layout.rowCount - 1));
  ui::InteractionBuffer<kbtouch::KEY_SLOTS> hits;
  kbtouch::buildKeyHits(hits, rect, fullScreenProps(layout, rect, GAP), SCREEN_W, SCREEN_H);
  const std::vector<KeyBox> keys = drawnKeys(layout, rect, GAP);
  expectEveryKeyHit(hits, keys);
  int16_t value = 0;
  const KeyBox& a = keys[39];  // 'a', row 3
  ASSERT_EQ(a.value, 'a');
  EXPECT_FALSE(keyAt(hits, a.x + a.w, a.y + a.h / 2, value));  // between 'a' and 'b'
  EXPECT_FALSE(keyAt(hits, a.x + a.w / 2, a.y + a.h, value));  // between rows 3 and 4
  EXPECT_FALSE(keyAt(hits, a.x + a.w / 2, a.y - 1, value));    // between rows 2 and 3
}

struct QwertyCase {
  ui::KeyboardLayoutId id;
  bool shifted;
  bool symbols;
};

// QWERTY in both languages the keyboard picks (layoutForLanguage: Spanish, else
// English), letters with the number row, shifted, and both symbol pages.
TEST(KeyboardTouch, QwertyFullScreenEveryLayerEveryKey) {
  const QwertyCase cases[] = {
      {ui::KeyboardLayoutId::QwertyEn, false, false},  {ui::KeyboardLayoutId::QwertyEn, true, false},
      {ui::KeyboardLayoutId::QwertyEn, false, true},   {ui::KeyboardLayoutId::QwertyEn, true, true},
      {ui::KeyboardLayoutId::SpanishEs, false, false}, {ui::KeyboardLayoutId::SpanishEs, true, false},
      {ui::KeyboardLayoutId::SpanishEs, false, true},  {ui::KeyboardLayoutId::SpanishEs, true, true},
  };
  for (const QwertyCase& c : cases) {
    SCOPED_TRACE(testing::Message() << "layout " << static_cast<int>(c.id) << " shifted " << c.shifted << " symbols "
                                    << c.symbols);
    const ui::KeyboardLayout& layout = ui::builtinKeyboardLayout(c.id, c.shifted, c.symbols, /*numberRow=*/true);
    const ui::Rect rect = fullScreenRect(layout);
    ui::InteractionBuffer<kbtouch::KEY_SLOTS> hits;
    kbtouch::buildKeyHits(hits, rect, fullScreenProps(layout, rect), SCREEN_W, SCREEN_H);
    expectEveryKeyHit(hits, drawnKeys(layout, rect, 0));
  }
}

// syncSelectionToValue (and KeyboardPanel::selectTouched) find the tapped key by
// its value, so a value must name exactly one key in each layer.
TEST(KeyboardTouch, KeyValuesAreUniquePerLayer) {
  std::vector<const ui::KeyboardLayout*> layouts = {&grid13::SL_LAYOUT};
  for (const auto id : {ui::KeyboardLayoutId::QwertyEn, ui::KeyboardLayoutId::SpanishEs}) {
    for (const bool shifted : {false, true}) {
      for (const bool symbols : {false, true})
        layouts.push_back(&ui::builtinKeyboardLayout(id, shifted, symbols, true));
    }
  }
  for (const ui::KeyboardLayout* layout : layouts) {
    std::set<int16_t> seen;
    for (int r = 0; r < layout->rowCount; ++r) {
      for (int c = 0; c < layout->rows[r].count; ++c) {
        EXPECT_TRUE(seen.insert(layout->rows[r].keys[c].value).second)
            << "duplicate value " << layout->rows[r].keys[c].value;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Split-screen panel (notes::KeyboardPanel in Create Note and Claude)
// ---------------------------------------------------------------------------

TEST(KeyboardTouch, PanelGridsEveryKeyIsItsDrawnRect) {
  const ui::KeyboardLayout* layouts[] = {&grid13::SL_LAYOUT,
                                         &ui::builtinKeyboardLayout(ui::KeyboardLayoutId::QwertyEn, false, false, true),
                                         &ui::builtinKeyboardLayout(ui::KeyboardLayoutId::QwertyEn, true, false, true),
                                         &ui::builtinKeyboardLayout(ui::KeyboardLayoutId::QwertyEn, false, true, true)};
  for (const ui::KeyboardLayout* layout : layouts) {
    const ui::Rect rect = panelRect(*layout);
    ui::InteractionBuffer<kbtouch::KEY_SLOTS> hits;
    kbtouch::buildKeyHits(hits, rect, panelProps(*layout), SCREEN_W, SCREEN_H);
    expectEveryKeyHit(hits, drawnKeys(*layout, rect, 0));
    int16_t value = 0;
    // The panel's side margins and the strip of text above it are not keys.
    EXPECT_FALSE(keyAt(hits, rect.x - 1, rect.y + KEY_H / 2, value));
    EXPECT_FALSE(keyAt(hits, rect.x + rect.width, rect.y + KEY_H / 2, value));
    EXPECT_FALSE(keyAt(hits, rect.x + rect.width / 2, rect.y - 1, value));
    // No hit overflow on the panel: below its last row is not a key.
    EXPECT_FALSE(keyAt(hits, rect.x + rect.width / 2, rect.y + rect.height, value));
  }
}

// ---------------------------------------------------------------------------
// Daisy strip (notes::KeyboardPanel, daisy branch)
// ---------------------------------------------------------------------------

TEST(KeyboardTouch, StripCellsAndSlots) {
  // The note editor's strip: 440 wide, three rows of 48.
  constexpr int X = SIDE_PAD;
  constexpr int Y = 600;
  constexpr int W = SCREEN_W - 2 * SIDE_PAD;
  constexpr int H = 3 * KEY_H;
  const int visible = kbtouch::stripVisibleCells(daisyrings::ABC_CHAR_PETALS + 1);
  ASSERT_EQ(visible, 7);
  const int cellW = W / visible;  // 62, with 6 px left over on the right
  const int slotH = H / 3;
  for (int c = 0; c < visible; ++c) {
    for (int s = 0; s < 3; ++s) {
      const int pts[3][2] = {{X + c * cellW + cellW / 2, Y + s * slotH + slotH / 2},
                             {X + c * cellW, Y + s * slotH},
                             {X + c * cellW + cellW - 1, Y + s * slotH + slotH - 1}};
      for (const auto& p : pts) {
        int cell = -1;
        int slot = -1;
        ASSERT_TRUE(kbtouch::stripHit(X, Y, W, H, visible, p[0], p[1], cell, slot));
        EXPECT_EQ(cell, c);
        EXPECT_EQ(slot, s);
      }
    }
  }
  int cell = 0;
  int slot = 0;
  EXPECT_FALSE(kbtouch::stripHit(X, Y, W, H, visible, X - 1, Y + 10, cell, slot));
  EXPECT_FALSE(kbtouch::stripHit(X, Y, W, H, visible, X + 10, Y - 1, cell, slot));
  EXPECT_FALSE(kbtouch::stripHit(X, Y, W, H, visible, X + visible * cellW, Y + 10, cell, slot));  // the remainder
  EXPECT_FALSE(kbtouch::stripHit(X, Y, W, H, visible, X + 10, Y + 3 * slotH, cell, slot));
}

TEST(KeyboardTouch, StripCellShowsThePetalRenderDrawsThere) {
  // render(): cell i shows (petal_ - visible/2 + i + n) % n, the selection in
  // the middle cell, wrapping both ways.
  const int abc = daisyrings::ABC_CHAR_PETALS + 1;
  const int num = daisyrings::NUM_CHAR_PETALS + 1;
  for (const int n : {abc, num}) {
    const int visible = kbtouch::stripVisibleCells(n);
    for (int center = 0; center < n; ++center) {
      for (int i = 0; i < visible; ++i) {
        EXPECT_EQ(kbtouch::stripPetal(center, visible, i, n), (center - visible / 2 + i + n) % n);
      }
      EXPECT_EQ(kbtouch::stripPetal(center, visible, visible / 2, n), center);
    }
  }
}

// ---------------------------------------------------------------------------
// Daisy wheel (DaisyEntryActivity: center 240,440, hub 150, rim 230)
// ---------------------------------------------------------------------------

constexpr int WHEEL_CX = 240;
constexpr int WHEEL_CY = 440;
constexpr int HUB = 150;
constexpr int RIM = 230;

TEST(KeyboardTouch, WheelEverySlotCenterHitsItsSlot) {
  // Both rings, at the line heights the label fonts give (UI_10 or SMALL,
  // 23..28 px across the System font choices, DaisyEntryActivity.cpp:115-148).
  for (const int n : {daisyrings::ABC_CHAR_PETALS + 1, daisyrings::NUM_CHAR_PETALS + 1}) {
    for (const int spacing : {23, 25, 28}) {
      for (int petal = 0; petal < n; ++petal) {
        for (int slot = 0; slot < 3; ++slot) {
          int x = 0;
          int y = 0;
          kbtouch::wheelSlotCenter(WHEEL_CX, WHEEL_CY, HUB, RIM, n, spacing, petal, slot, x, y);
          int hitPetal = -1;
          int hitSlot = -1;
          ASSERT_TRUE(kbtouch::wheelHit(WHEEL_CX, WHEEL_CY, HUB, RIM, n, spacing, x, y, hitPetal, hitSlot))
              << "n " << n << " petal " << petal << " slot " << slot;
          EXPECT_EQ(hitPetal, petal) << "n " << n << " slot " << slot << " spacing " << spacing;
          EXPECT_EQ(hitSlot, slot) << "n " << n << " petal " << petal << " spacing " << spacing;
        }
      }
    }
  }
}

TEST(KeyboardTouch, WheelSlotsSplitHalfwayBetweenTheirCenters) {
  // The abc ring's third petal (72 degrees): its upright column crosses the
  // wedge, so the slot is read off the height alone. The middle slot owns
  // everything within half a spacing of its center; past that, the neighbor;
  // past the outer slots, still the outer slots (all inside this wedge).
  const int n = daisyrings::ABC_CHAR_PETALS + 1;
  const int spacing = 26;
  const int petal = 2;
  int x = 0;
  int y = 0;
  kbtouch::wheelSlotCenter(WHEEL_CX, WHEEL_CY, HUB, RIM, n, spacing, petal, 1, x, y);
  const struct {
    int dy;
    int slot;
  } cases[] = {{0, 1},
               {spacing / 2 - 1, 1},
               {-(spacing / 2 - 1), 1},
               {spacing / 2 + 1, 2},
               {-(spacing / 2 + 1), 0},
               {spacing + spacing / 2 + 2, 2},
               {-(spacing + spacing / 2 + 2), 0}};
  for (const auto& c : cases) {
    int hitPetal = -1;
    int hitSlot = -1;
    ASSERT_TRUE(kbtouch::wheelHit(WHEEL_CX, WHEEL_CY, HUB, RIM, n, spacing, x, y + c.dy, hitPetal, hitSlot)) << c.dy;
    EXPECT_EQ(hitPetal, petal) << c.dy;
    EXPECT_EQ(hitSlot, c.slot) << c.dy;
  }
}

TEST(KeyboardTouch, WheelHubAndOutsideTheRimAreNotKeys) {
  const int n = daisyrings::ABC_CHAR_PETALS + 1;
  int petal = 0;
  int slot = 0;
  EXPECT_FALSE(kbtouch::wheelHit(WHEEL_CX, WHEEL_CY, HUB, RIM, n, 25, WHEEL_CX, WHEEL_CY, petal, slot));
  EXPECT_FALSE(kbtouch::wheelHit(WHEEL_CX, WHEEL_CY, HUB, RIM, n, 25, WHEEL_CX, WHEEL_CY - HUB + 1, petal, slot));
  EXPECT_FALSE(kbtouch::wheelHit(WHEEL_CX, WHEEL_CY, HUB, RIM, n, 25, WHEEL_CX, WHEEL_CY - RIM - 1, petal, slot));
  EXPECT_FALSE(kbtouch::wheelHit(WHEEL_CX, WHEEL_CY, HUB, RIM, n, 25, 2, 100, petal, slot));  // the text field corner
  EXPECT_FALSE(kbtouch::wheelHit(WHEEL_CX, WHEEL_CY, HUB, RIM, n, 25, WHEEL_CX, 790, petal, slot));
  // On the band edges themselves: inside.
  EXPECT_TRUE(kbtouch::wheelHit(WHEEL_CX, WHEEL_CY, HUB, RIM, n, 25, WHEEL_CX, WHEEL_CY - HUB, petal, slot));
  EXPECT_TRUE(kbtouch::wheelHit(WHEEL_CX, WHEEL_CY, HUB, RIM, n, 25, WHEEL_CX, WHEEL_CY - RIM, petal, slot));
}

TEST(KeyboardTouch, WheelPetalsSplitAtTheDrawnSpokes) {
  // render() draws petal i's spokes at (i -/+ 0.5) steps; just either side of
  // one is the two petals it separates.
  for (const int n : {daisyrings::ABC_CHAR_PETALS + 1, daisyrings::NUM_CHAR_PETALS + 1}) {
    const float step = kbtouch::wheelStep(n);
    const float r = (HUB + RIM) / 2.0f;
    for (int i = 0; i < n; ++i) {
      const float spoke = (static_cast<float>(i) + 0.5f) * step;
      for (const float d : {-0.02f, 0.02f}) {
        const int x = WHEEL_CX + static_cast<int>(lroundf(r * sinf(spoke + d)));
        const int y = WHEEL_CY - static_cast<int>(lroundf(r * cosf(spoke + d)));
        int petal = -1;
        int slot = -1;
        ASSERT_TRUE(kbtouch::wheelHit(WHEEL_CX, WHEEL_CY, HUB, RIM, n, 25, x, y, petal, slot));
        EXPECT_EQ(petal, d < 0 ? i : (i + 1) % n) << "n " << n << " spoke " << i;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Tap / hold routing
// ---------------------------------------------------------------------------

kbtouch::TouchInput down(const int x, const int y) {
  kbtouch::TouchInput t;
  t.pressedDown = true;
  t.px = static_cast<int16_t>(x);
  t.py = static_cast<int16_t>(y);
  t.inContact = true;
  return t;
}
kbtouch::TouchInput tap(const int x, const int y) {
  kbtouch::TouchInput t;
  t.tapped = true;
  t.tx = static_cast<int16_t>(x);
  t.ty = static_cast<int16_t>(y);
  return t;
}
kbtouch::TouchInput idle() { return kbtouch::TouchInput{}; }
kbtouch::TouchInput dragging() {
  kbtouch::TouchInput t;
  t.inContact = true;  // past the tap slop: no candidate any more
  return t;
}

// One key: x in [100, 150), y in [100, 150), id 7.
int oneKey(const int x, const int y) { return x >= 100 && x < 150 && y >= 100 && y < 150 ? 7 : -1; }

TEST(KeyboardTouch, HoldRouterTapTypesOnRelease) {
  kbtouch::HoldRouter router;
  EXPECT_FALSE(router.update(oneKey, down(120, 120), 0));
  EXPECT_FALSE(router.update(oneKey, down(120, 120), 200));
  const kbtouch::HoldRouter::Result r = router.update(oneKey, tap(120, 120), 250);
  ASSERT_TRUE(r);
  EXPECT_EQ(r.key, 7);
  EXPECT_FALSE(r.longPress);
}

TEST(KeyboardTouch, HoldRouterHoldFiresOnceWhileDownAndSwallowsTheRelease) {
  kbtouch::HoldRouter router;
  EXPECT_FALSE(router.update(oneKey, down(120, 120), 1000));
  EXPECT_FALSE(router.update(oneKey, down(120, 120), 1000 + kbtouch::HOLD_MS - 1));
  const kbtouch::HoldRouter::Result r = router.update(oneKey, down(120, 120), 1000 + kbtouch::HOLD_MS);
  ASSERT_TRUE(r);
  EXPECT_EQ(r.key, 7);
  EXPECT_TRUE(r.longPress);
  EXPECT_FALSE(router.update(oneKey, down(120, 120), 3000));  // once
  EXPECT_FALSE(router.update(oneKey, tap(120, 120), 3100));   // the release that ends it
  // The next tap is a tap again.
  EXPECT_FALSE(router.update(oneKey, down(120, 120), 4000));
  const kbtouch::HoldRouter::Result next = router.update(oneKey, tap(120, 120), 4100);
  ASSERT_TRUE(next);
  EXPECT_FALSE(next.longPress);
}

TEST(KeyboardTouch, HoldRouterOffKeyTapDispatchesThePressedKey) {
  kbtouch::HoldRouter router;
  EXPECT_FALSE(router.update(oneKey, down(120, 120), 0));
  const kbtouch::HoldRouter::Result r = router.update(oneKey, tap(160, 120), 100);
  ASSERT_TRUE(r);
  EXPECT_EQ(r.key, 7);
}

TEST(KeyboardTouch, HoldRouterOffEveryKeyIsNothing) {
  kbtouch::HoldRouter router;
  EXPECT_FALSE(router.update(oneKey, down(10, 10), 0));
  EXPECT_FALSE(router.update(oneKey, down(10, 10), 5000));  // no hold off the keys
  EXPECT_FALSE(router.update(oneKey, tap(10, 10), 5100));
}

TEST(KeyboardTouch, HoldRouterDragResetsTheHoldClock) {
  kbtouch::HoldRouter router;
  EXPECT_FALSE(router.update(oneKey, down(120, 120), 0));
  EXPECT_FALSE(router.update(oneKey, dragging(), 100));  // drifted into a swipe
  EXPECT_FALSE(router.update(oneKey, idle(), 200));      // lifted without a tap
  // A new press long after the first must time its hold from ITS start.
  EXPECT_FALSE(router.update(oneKey, down(120, 120), 10000));
  EXPECT_FALSE(router.update(oneKey, down(120, 120), 10000 + kbtouch::HOLD_MS - 1));
  EXPECT_TRUE(router.update(oneKey, down(120, 120), 10000 + kbtouch::HOLD_MS));
}

// HoldRouter must behave as fui::TouchHoldRouter (the full-screen keyboard's
// router, upstream's) does on the same contacts: same dispatches, same frames.
TEST(KeyboardTouch, HoldRouterMatchesTheSdkRouter) {
  ui::InteractionBuffer<4> buffer;
  buffer.addInteraction(ui::Interaction{ui::Rect{100, 100, 50, 50}, 1, 7,
                                        static_cast<uint16_t>(ui::InputTouch | ui::InputLongPress), ui::StateNormal,
                                        0});
  ui::TouchHoldRouter sdk;
  sdk.holdMs = kbtouch::HOLD_MS;
  kbtouch::HoldRouter ours;

  struct Frame {
    kbtouch::TouchInput in;
    uint32_t ms;
  };
  const Frame script[] = {
      {down(120, 120), 0},    {down(120, 120), 100},  {tap(120, 120), 150},                           // tap
      {down(120, 120), 1000}, {down(120, 120), 1400}, {down(120, 120), 1500}, {tap(120, 120), 1600},  // hold
      {down(120, 120), 2000}, {tap(170, 120), 2050},                                                  // off-key release
      {down(10, 10), 3000},   {tap(10, 10), 3050},                                                    // off every key
      {down(120, 120), 4000}, {dragging(), 4100},     {idle(), 4200},                                 // drag
      {down(120, 120), 9000}, {down(120, 120), 9349}, {down(120, 120), 9350}, {tap(120, 120), 9400},
  };
  for (const Frame& f : script) {
    const ui::TouchHoldRouter::Result s =
        sdk.update(buffer, f.in.pressedDown, f.in.px, f.in.py, f.in.tapped, f.in.tx, f.in.ty, f.in.inContact, f.ms);
    const kbtouch::HoldRouter::Result o = ours.update(oneKey, f.in, f.ms);
    EXPECT_EQ(static_cast<bool>(s.event), static_cast<bool>(o)) << "at " << f.ms;
    if (s.event && o) {
      EXPECT_EQ(s.event.value, o.key) << "at " << f.ms;
      EXPECT_EQ(s.event.longPress, o.longPress) << "at " << f.ms;
    }
  }
}

// KeyboardPanel::resetTouch() (the host stopped taking touch: caret mode, the
// panel hidden, the Claude answer view) must forget the key under the finger,
// or the next quick tap that misses every key types it.
TEST(KeyboardTouch, ResetGridTouchForgetsTheHeldKey) {
  const ui::KeyboardLayout& layout = grid13::SL_LAYOUT;
  const ui::Rect rect = panelRect(layout);
  const std::vector<KeyBox> keys = drawnKeys(layout, rect, 0);
  const KeyBox& k = keys[keys.size() / 2];
  // A quick tap off every key, with no press pass before it.
  const auto quickTapOffTheKeys = [](ui::TouchHoldRouter& router, ui::InteractionBuffer<kbtouch::KEY_SLOTS>& hits) {
    return static_cast<bool>(router.update(hits, false, 0, 0, true, 5, 5, false, 5000).event);
  };

  // The mechanism: router.reset() alone leaves the table's active key.
  {
    ui::InteractionBuffer<kbtouch::KEY_SLOTS> hits;
    kbtouch::buildKeyHits(hits, rect, panelProps(layout), SCREEN_W, SCREEN_H);
    ui::TouchHoldRouter router;
    router.update(hits, true, static_cast<int16_t>(k.x + k.w / 2), static_cast<int16_t>(k.y + k.h / 2), false, 0, 0,
                  true, 100);
    ASSERT_GE(hits.activeIndex(), 0);
    router.reset();
    EXPECT_TRUE(quickTapOffTheKeys(router, hits)) << "expected the stale key to type without the fix";
  }
  // The fix.
  {
    ui::InteractionBuffer<kbtouch::KEY_SLOTS> hits;
    kbtouch::buildKeyHits(hits, rect, panelProps(layout), SCREEN_W, SCREEN_H);
    ui::TouchHoldRouter router;
    router.update(hits, true, static_cast<int16_t>(k.x + k.w / 2), static_cast<int16_t>(k.y + k.h / 2), false, 0, 0,
                  true, 100);
    ASSERT_GE(hits.activeIndex(), 0);
    kbtouch::resetGridTouch(router, hits);
    EXPECT_EQ(hits.activeIndex(), -1);
    EXPECT_FALSE(quickTapOffTheKeys(router, hits));
  }
  // (0,0) is on no key of any panel or full-screen layout.
  int16_t value = 0;
  for (const ui::KeyboardLayout* l :
       {&grid13::SL_LAYOUT, &ui::builtinKeyboardLayout(ui::KeyboardLayoutId::QwertyEn, false, false, true)}) {
    ui::InteractionBuffer<kbtouch::KEY_SLOTS> a;
    kbtouch::buildKeyHits(a, panelRect(*l), panelProps(*l), SCREEN_W, SCREEN_H);
    EXPECT_FALSE(keyAt(a, 0, 0, value));
    ui::InteractionBuffer<kbtouch::KEY_SLOTS> b;
    const ui::Rect fr = fullScreenRect(*l);
    kbtouch::buildKeyHits(b, fr, fullScreenProps(*l, fr), SCREEN_W, SCREEN_H);
    EXPECT_FALSE(keyAt(b, 0, 0, value));
  }
}

// A touch hold on Del is a plain Del on the Picco: a lost TMA525C lift-off
// turns a tap into a hold, and a hold on Del was clear-all. Every other key
// keeps its hold (the alt, or uppercase).
TEST(KeyboardTouch, TouchHoldOnDelIsNotClearAll) {
  std::vector<const ui::KeyboardLayout*> layouts = {&grid13::SL_LAYOUT};
  for (const auto id : {ui::KeyboardLayoutId::QwertyEn, ui::KeyboardLayoutId::SpanishEs}) {
    for (const bool shifted : {false, true}) {
      for (const bool symbols : {false, true}) {
        layouts.push_back(&ui::builtinKeyboardLayout(id, shifted, symbols, true));
      }
    }
  }
  for (const ui::KeyboardLayout* l : layouts) {
    int deletes = 0;
    for (int r = 0; r < l->rowCount; ++r) {
      for (int c = 0; c < l->rows[r].count; ++c) {
        const ui::KeyboardKey& key = l->rows[r].keys[c];
        if (key.kind == ui::KeyKind::Delete) {
          ++deletes;
          EXPECT_FALSE(kbtouch::touchHoldKeepsLongPress(*l, key.value));
        } else {
          EXPECT_TRUE(kbtouch::touchHoldKeepsLongPress(*l, key.value)) << "row " << r << " col " << c;
        }
      }
    }
    EXPECT_EQ(deletes, 1);
  }
}

// ---------------------------------------------------------------------------
// The left-edge Back swipe (owner 2026-11-06: "Left-edge swipe = Back")
// ---------------------------------------------------------------------------
//
// On the Picco a swipe reaches the firmware through ONE call,
// HalGPIO::wasBackSwipeCandidate, read only by MappedInputManager::
// wasBackGesture, which delivers it as Back exactly when
// edgeswipe::isLeftEdgeBack says so. HalGPIO::wasSwipe, which the Home and menu
// swipes and every SwipeDir consumer go through, returns false on the Picco
// unconditionally. So "which swipes do anything on the Picco" is this
// function's truth table, and nothing else.

TEST(BackSwipe, LeftEdgeRightwardIsBack) {
  EXPECT_TRUE(edgeswipe::isLeftEdgeBack(10, 400, 200, 410, SCREEN_W));
  EXPECT_TRUE(edgeswipe::isLeftEdgeBack(0, 700, 61, 700, SCREEN_W));
  // The left quarter is inclusive: 480 * 0.25 = 120.
  EXPECT_TRUE(edgeswipe::isLeftEdgeBack(120, 400, 300, 400, SCREEN_W));
}

TEST(BackSwipe, EveryOtherSwipeIsNothing) {
  struct Swipe {
    int sx, sy, ex, ey;
    const char* what;
  };
  const Swipe others[] = {
      {121, 400, 300, 400, "rightward, starting just past the left quarter"},
      {240, 400, 440, 400, "mid-screen rightward"},
      {300, 400, 100, 400, "mid-screen leftward"},
      {470, 400, 250, 400, "right edge, leftward"},
      {400, 400, 470, 400, "right edge, rightward"},
      {240, 790, 240, 600, "bottom-up (the Home swipe elsewhere)"},
      {240, 10, 240, 200, "top-down (the menu swipe elsewhere)"},
      {50, 200, 100, 500, "left edge, but more down than across"},
      {50, 500, 100, 200, "left edge, but more up than across"},
      {100, 400, 20, 400, "left edge, leftward"},
  };
  for (const Swipe& w : others) {
    EXPECT_FALSE(edgeswipe::isLeftEdgeBack(w.sx, w.sy, w.ex, w.ey, SCREEN_W)) << w.what;
  }
}

// The finger that swipes lands on a key first: the left column of every
// keyboard is inside the left quarter. The router sees it as a press on that
// key, then (once it travels past the 28 px tap slop) as contact that is no
// longer a tap candidate, then as a lift with no tap: the SDK reports a
// release that moved past 59 px as a swipe and never as a tap
// (InputManager.cpp:582 and :692; TOUCH_TAP_RELEASE_SLOP_PX is
// TOUCH_SWIPE_MIN_PX - 1). Whatever pass the activity reads Back in, the router
// dispatches nothing, and the key it held is forgotten.
TEST(BackSwipe, ASwipeFromAKeyTypesNothingOnTheFullScreenKeyboard) {
  const ui::KeyboardLayout& layout = grid13::SL_LAYOUT;
  const ui::Rect rect = fullScreenRect(layout);
  ui::InteractionBuffer<kbtouch::KEY_SLOTS> hits;
  kbtouch::buildKeyHits(hits, rect, fullScreenProps(layout, rect), SCREEN_W, SCREEN_H);
  const std::vector<KeyBox> keys = drawnKeys(layout, rect, 0);
  const KeyBox& first = keys.front();  // row 0, column 0
  const int sx = first.x + first.w / 2;
  const int sy = first.y + first.h / 2;
  ASSERT_TRUE(edgeswipe::isLeftEdgeBack(sx, sy, sx + 200, sy + 10, SCREEN_W));

  ui::TouchHoldRouter router;
  router.holdMs = kbtouch::HOLD_MS;
  router.overrideHoldMs = kbtouch::DEL_HOLD_MS;
  struct Pass {
    kbtouch::TouchInput in;
    uint32_t ms;
  };
  const Pass swipe[] = {
      {down(sx, sy), 90}, {down(sx, sy), 150},  // landed on the key, not yet moving
      {dragging(), 200},  {dragging(), 260},   {dragging(), 320},
      {dragging(), 380},  {dragging(), 440},   {dragging(), 500},  // well past a hold's 350 ms
      {idle(), 520},                                               // the lift: Back, and no tap
  };
  for (const Pass& p : swipe) {
    const ui::TouchHoldRouter::Result r =
        router.update(hits, p.in.pressedDown, p.in.px, p.in.py, p.in.tapped, p.in.tx, p.in.ty, p.in.inContact, p.ms);
    EXPECT_FALSE(r.event) << "typed a key at " << p.ms;
  }
  EXPECT_EQ(hits.activeIndex(), -1) << "the key the swipe started on is still held";

  // And it is gone for good: a quick tap that misses every key afterwards
  // (released before the 90 ms touch-down delay, so no press pass) must not
  // type the key the swipe started on.
  const ui::TouchHoldRouter::Result after =
      router.update(hits, false, 0, 0, true, static_cast<int16_t>(SCREEN_W - 2), 5, false, 2000);
  EXPECT_FALSE(after.event);
}

TEST(BackSwipe, ASwipeFromASlotTypesNothingOnTheDaisy) {
  kbtouch::HoldRouter router;
  int typed = 0;
  const auto run = [&](const kbtouch::TouchInput& in, const uint32_t ms) {
    if (router.update(oneKey, in, ms)) ++typed;
  };
  run(down(120, 120), 90);
  run(down(120, 120), 150);
  for (uint32_t ms = 200; ms <= 500; ms += 60) run(dragging(), ms);
  run(idle(), 520);
  EXPECT_EQ(typed, 0);
  run(tap(10, 10), 2000);  // a press-less quick tap off every key
  EXPECT_EQ(typed, 0);
}

}  // namespace
