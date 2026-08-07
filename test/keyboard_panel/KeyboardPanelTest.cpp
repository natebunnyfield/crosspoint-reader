// Regressions for keyboard defects that reached the device. Each of these was
// one assertion away from being caught before shipping.
#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "notes/DaisyRings.h"
#include "notes/Grid13Layout.h"

namespace {

// Walk a layout the way KeyboardPanel::activate() resolves a key, so the tests
// exercise the same lookup rules without pulling in the renderer.
const freeink::ui::KeyboardKey* findKey(const freeink::ui::KeyboardLayout& l, const char* label) {
  for (int r = 0; r < l.rowCount; ++r) {
    for (int c = 0; c < l.rows[r].count; ++c) {
      const auto& k = l.rows[r].keys[c];
      if (k.label != nullptr && std::strcmp(k.label, label) == 0) return &k;
    }
  }
  return nullptr;
}

}  // namespace

// The SDK's space key carries output == nullptr and KeyKind::Space. Reading
// key.output directly made the space bar type NOTHING in QWERTY.
TEST(KeyboardPanel, SdkSpaceKeyNeedsOutputLookupNotTheField) {
  const auto& l = freeink::ui::builtinKeyboardLayout(freeink::ui::KeyboardLayoutId::QwertyEn, false, false, true);
  const freeink::ui::KeyboardKey* space = nullptr;
  for (int r = 0; r < l.rowCount && space == nullptr; ++r) {
    for (int c = 0; c < l.rows[r].count; ++c) {
      if (l.rows[r].keys[c].kind == freeink::ui::KeyKind::Space) {
        space = &l.rows[r].keys[c];
        break;
      }
    }
  }
  ASSERT_NE(space, nullptr) << "QWERTY has no Space key";
  // This is the trap: the field is null, so only the lookup yields a space.
  const char* viaLookup = freeink::ui::keyboardOutputFor(l, space->value);
  ASSERT_NE(viaLookup, nullptr);
  EXPECT_STREQ(viaLookup, " ");
}

// 13-Grid's space must be a real Space key: KeyKind::Normal with a " " label
// drew a blank gap, because the SDK only draws the space rule for Space.
TEST(KeyboardPanel, Grid13SpaceIsARealSpaceKey) {
  const freeink::ui::KeyboardKey* space = nullptr;
  for (int r = 0; r < grid13::SL_LAYOUT.rowCount && space == nullptr; ++r) {
    for (int c = 0; c < grid13::SL_LAYOUT.rows[r].count; ++c) {
      if (grid13::SL_LAYOUT.rows[r].keys[c].value == ' ') {
        space = &grid13::SL_LAYOUT.rows[r].keys[c];
        break;
      }
    }
  }
  ASSERT_NE(space, nullptr);
  EXPECT_EQ(space->kind, freeink::ui::KeyKind::Space);
  EXPECT_STREQ(freeink::ui::keyboardOutputFor(grid13::SL_LAYOUT, space->value), " ");
}

// Every 13-Grid row must sum to 13 width units, or the column anchor that makes
// vertical navigation exact silently stops being exact.
TEST(KeyboardPanel, Grid13RowsAllSumTo13Units) {
  for (int r = 0; r < grid13::SL_LAYOUT.rowCount; ++r) {
    int units = 0;
    for (int c = 0; c < grid13::SL_LAYOUT.rows[r].count; ++c) units += grid13::SL_LAYOUT.rows[r].keys[c].widthUnits;
    EXPECT_EQ(units, 13) << "row " << r;
  }
}

// The daisy rings are the shipped character set. An earlier panel invented
// "0-9" as a cell, which typed only '0', '-' and '9'.
TEST(KeyboardPanel, DaisyRingsCoverTheAlphabetAndDigits) {
  std::string seen;
  for (int p = 0; p < daisyrings::ABC_CHAR_PETALS; ++p) {
    for (int s = 0; s < 3; ++s) seen.push_back(daisyrings::ABC_RING[p][s]);
  }
  for (char c = 'a'; c <= 'z'; ++c) EXPECT_NE(seen.find(c), std::string::npos) << "missing letter " << c;

  std::string nums;
  for (int p = 0; p < daisyrings::NUM_CHAR_PETALS; ++p) {
    for (int s = 0; s < 3; ++s) nums.push_back(daisyrings::NUM_RING[p][s]);
  }
  for (char c = '0'; c <= '9'; ++c) EXPECT_NE(nums.find(c), std::string::npos) << "missing digit " << c;
}

// Every petal holds exactly three characters — the three pick buttons map to
// them one-to-one, so a short petal would leave a button dead.
TEST(KeyboardPanel, EveryDaisyPetalHasThreeSlots) {
  for (int p = 0; p < daisyrings::ABC_CHAR_PETALS; ++p) {
    for (int s = 0; s < 3; ++s) EXPECT_NE(daisyrings::ABC_RING[p][s], '\0') << "abc petal " << p << " slot " << s;
  }
  for (int p = 0; p < daisyrings::NUM_CHAR_PETALS; ++p) {
    for (int s = 0; s < 3; ++s) EXPECT_NE(daisyrings::NUM_RING[p][s], '\0') << "num petal " << p << " slot " << s;
  }
}

// Long-press must reach uppercase: the panel had no long-press path, so the
// default 13-Grid could not type a capital at all.
TEST(KeyboardPanel, AltOutputGivesUppercaseForLetters) {
  const auto& l = freeink::ui::builtinKeyboardLayout(freeink::ui::KeyboardLayoutId::QwertyEn, false, false, true);
  const freeink::ui::KeyboardKey* q = findKey(l, "q");
  ASSERT_NE(q, nullptr);
  const char* alt = freeink::ui::keyboardAltOutputFor(l, q->value);
  ASSERT_NE(alt, nullptr) << "no alt output for a letter — long-press cannot capitalise";
  EXPECT_STREQ(alt, "Q");
}
