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

}  // namespace
