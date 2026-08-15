// Regressions for keyboard defects that reached the device. Each of these was
// one assertion away from being caught before shipping.
#include <gtest/gtest.h>

#include <cctype>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

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

// ---------------------------------------------------------------------------
// Daisy ring layout (owner rulings, 2026-08-06). These pin the four decisions
// so a later edit to DaisyRings.h cannot quietly undo them.
// ---------------------------------------------------------------------------

namespace {
// Every character in petals 1..N-1 of the number ring. The last petal is
// convenience duplicates plus space and is deliberately excluded.
std::vector<char> numCoreChars() {
  std::vector<char> out;
  for (int p = 0; p < daisyrings::NUM_CHAR_PETALS - 1; ++p) {
    for (int s = 0; s < 3; ++s) out.push_back(daisyrings::NUM_RING[p][s]);
  }
  return out;
}
}  // namespace

// The wheel was missing < > [ ] { } \ | ^ and the backtick entirely, so a WiFi
// password containing one could not be typed at all. Every printable ASCII
// character must now have exactly one home.
TEST(DaisyRings, CoversEveryPrintableAsciiExactlyOnce) {
  std::set<char> seen;
  for (int p = 0; p < daisyrings::ABC_CHAR_PETALS; ++p) {
    for (int s = 0; s < 3; ++s) seen.insert(daisyrings::ABC_RING[p][s]);
  }
  for (const char c : numCoreChars()) seen.insert(c);

  for (char c = 0x21; c < 0x7F; ++c) {   // printable, excluding space
    if (c >= 'A' && c <= 'Z') continue;  // uppercase comes from long-press
    EXPECT_TRUE(seen.count(c) == 1) << "printable character missing from the wheel: '" << c << "'";
  }
  EXPECT_TRUE(seen.count(' ') == 1) << "space missing";
}

// Petals 1..N-1 carry no duplicates: a character with two homes means one of
// them is wasted and the ring is longer than it needs to be.
TEST(DaisyRings, NumberRingCoreHasNoDuplicates) {
  const std::vector<char> core = numCoreChars();
  const std::set<char> unique(core.begin(), core.end());
  EXPECT_EQ(core.size(), unique.size()) << "a character appears twice in petals 1..N-1";
}

// Owner ruling: top = open, bottom = close, middle = related. A pair must be
// ONE button apart, never on opposite sides of the ring as ( and ) were.
TEST(DaisyRings, BracketPairsSitOpenAboveCloseOnOnePetal) {
  const std::pair<char, char> pairs[] = {{'(', ')'}, {'[', ']'}, {'{', '}'}, {'<', '>'}};
  for (const auto& [open, close] : pairs) {
    bool found = false;
    for (int p = 0; p < daisyrings::NUM_CHAR_PETALS; ++p) {
      if (daisyrings::NUM_RING[p][0] != open) continue;
      found = true;
      EXPECT_EQ(daisyrings::NUM_RING[p][2], close)
          << "'" << open << "' must have '" << close << "' directly below it (Up opens, Down closes)";
    }
    EXPECT_TRUE(found) << "no petal leads with '" << open << "'";
  }
}

// Space sits in the SAME slot on both rings, so the target does not move when
// you swap. Bottom of the last petal, matching {y, z, space}.
TEST(DaisyRings, BothRingsEndWithSpaceInTheBottomSlot) {
  EXPECT_EQ(daisyrings::ABC_RING[daisyrings::ABC_CHAR_PETALS - 1][2], ' ');
  EXPECT_EQ(daisyrings::NUM_RING[daisyrings::NUM_CHAR_PETALS - 1][2], ' ');
  // ...and the two slots above space are real characters, not blanks: an
  // all-space petal was the earlier draft and wasted two thirds of a rotation.
  EXPECT_NE(daisyrings::NUM_RING[daisyrings::NUM_CHAR_PETALS - 1][0], ' ');
  EXPECT_NE(daisyrings::NUM_RING[daisyrings::NUM_CHAR_PETALS - 1][1], ' ');
}

// Special keys are UPPERCASE; characters are not (owner ruling 2026-08-06).
// A lowercase "del" reads as three letters you could type. This walks the
// shared layout tables so a new special key cannot be added in lowercase.
TEST(KeyboardLabels, SpecialKeysAreUppercase) {
  const fui::KeyboardLayout& l = grid13::SL_LAYOUT;
  for (uint8_t r = 0; r < l.rowCount; ++r) {
    for (uint8_t k = 0; k < l.rows[r].count; ++k) {
      const fui::KeyboardKey& key = l.rows[r].keys[k];
      if (key.kind == fui::KeyKind::Normal || key.label == nullptr) continue;
      for (const char* c = key.label; *c; ++c) {
        EXPECT_FALSE(*c >= 'a' && *c <= 'z')
            << "special key \"" << key.label << "\" has a lowercase letter; special keys are uppercase so they do "
            << "not read as characters you could type";
      }
    }
  }
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

// ---------------------------------------------------------------------------
// The hold-space-for-caret-mode sentinel (NoteEditorActivity::handlePanelKey).
//
// The editor decides "the space bar was held" from the RESULT of a long press:
// a Character event carrying ' '. That is only safe while two things hold, and
// both are properties of tables and SDK code this test can reach, so pin them
// here rather than discovering the regression as a dead gesture on hardware.
// ---------------------------------------------------------------------------

// 1. A long press on space still resolves to a plain space. keyboardAltOutputFor
//    returns nullptr for any key whose kind is not Normal, so Space falls back
//    to keyboardOutputFor. Give the space key an `alt` and the fallback stops
//    firing -- the gesture would type that alt instead of opening caret mode.
TEST(KeyboardPanel, LongPressOnSpaceStillResolvesToASpace) {
  const auto& qwerty = freeink::ui::builtinKeyboardLayout(freeink::ui::KeyboardLayoutId::QwertyEn, false, false, true);
  EXPECT_EQ(freeink::ui::keyboardAltOutputFor(qwerty, ' '), nullptr);
  EXPECT_STREQ(freeink::ui::keyboardOutputFor(qwerty, ' '), " ");

  EXPECT_EQ(freeink::ui::keyboardAltOutputFor(grid13::SL_LAYOUT, ' '), nullptr);
  EXPECT_STREQ(freeink::ui::keyboardOutputFor(grid13::SL_LAYOUT, ' '), " ");
}

// 2. No OTHER key may resolve to a space on long press, or holding it would
//    open caret mode instead of typing what it advertises. Covers the case
//    flip (no letter flips to ' ') and every declared alt in both grids.
TEST(KeyboardPanel, NoKeyButSpaceLongPressesToASpace) {
  const freeink::ui::KeyboardLayout* layouts[] = {
      &grid13::SL_LAYOUT,
      &freeink::ui::builtinKeyboardLayout(freeink::ui::KeyboardLayoutId::QwertyEn, false, false, true),
      &freeink::ui::builtinKeyboardLayout(freeink::ui::KeyboardLayoutId::QwertyEn, true, false, true),
      &freeink::ui::builtinKeyboardLayout(freeink::ui::KeyboardLayoutId::QwertyEn, false, true, true),
  };
  for (const auto* l : layouts) {
    for (int r = 0; r < l->rowCount; ++r) {
      for (int c = 0; c < l->rows[r].count; ++c) {
        const auto& key = l->rows[r].keys[c];
        if (key.kind == freeink::ui::KeyKind::Space) continue;
        const char* out = freeink::ui::keyboardAltOutputFor(*l, key.value);
        if (out == nullptr) out = freeink::ui::keyboardOutputFor(*l, key.value);
        if (out == nullptr) continue;  // Shift, Mode, Del, OK produce no text
        EXPECT_STRNE(out, " ") << "key '" << (key.label ? key.label : "?")
                               << "' long-presses to a space, which the "
                                  "note editor reads as the caret-mode gesture";
      }
    }
  }
}

// 3. Daisy: the space slot uppercases to itself, so the same sentinel holds on
//    the wheel, and no other slot may reach a space by the case flip.
TEST(KeyboardPanel, DaisyLongPressReachesASpaceOnlyFromTheSpaceSlot) {
  EXPECT_EQ(std::toupper(static_cast<unsigned char>(' ')), ' ');
  for (int p = 0; p < daisyrings::ABC_CHAR_PETALS; ++p) {
    for (int s = 0; s < 3; ++s) {
      const char c = daisyrings::ABC_RING[p][s];
      if (c == ' ') continue;
      EXPECT_NE(std::toupper(static_cast<unsigned char>(c)), ' ') << "petal " << p << " slot " << s;
    }
  }
}

// Every 13-Grid row must sum to 13 width units, or the column anchor that makes
// vertical navigation exact silently stops being exact.
TEST(KeyboardPanel, Grid13RowsAllSumTo13Units) {
  // insetUnits COUNTS, on both sides -- this is the SDK's own arithmetic
  // (keyboard.h: `units = insetUnits * 2 + sum(widthUnits)`), and summing only
  // the widths is a weaker claim than the renderer makes. A short centred row
  // (9 keys + 2 either side, 7 + 3) is exactly as valid as a full one; what
  // breaks the column-anchor navigation is a row that does not reach 13.
  for (int r = 0; r < grid13::SL_LAYOUT.rowCount; ++r) {
    const fui::KeyboardRow& row = grid13::SL_LAYOUT.rows[r];
    int units = row.insetUnits * 2;
    for (int c = 0; c < row.count; ++c) units += row.keys[c].widthUnits;
    EXPECT_EQ(units, 13) << "row " << r;
  }
}

// The owner rulings that reshaped this layout: the thirteen characters the
// number row used to hide got faces of their own (2026-08-10), and the
// leftovers collapsed onto one symbol row where six ride as alts so that Del,
// Space and Return could take the freed cells (2026-08-11).
//
// Pinned as an EXACT SET, not a count. The failure this guards is a character
// that stops being typeable at all, and that happens by an alt quietly moving
// or disappearing while the totals still look right.
TEST(KeyboardPanel, Grid13SecondaryCharactersAreExactlyTheShiftPairs) {
  std::set<std::pair<std::string, std::string>> pairs;
  for (int r = 0; r < grid13::SL_LAYOUT.rowCount; ++r) {
    const fui::KeyboardRow& row = grid13::SL_LAYOUT.rows[r];
    for (int c = 0; c < row.count; ++c) {
      const fui::KeyboardKey& k = row.keys[c];
      if (k.kind == fui::KeyKind::Normal && k.alt) pairs.insert({k.output ? k.output : k.label, k.alt});
    }
  }
  // Each is the shifted partner of its key on a real keyboard, which is the
  // whole reason these five are the ones that may hide.
  const std::set<std::pair<std::string, std::string>> want{{".", ">"}, {",", "<"}, {"'", "\""},
                                                           {"[", "{"}, {"]", "}"}, {"\\", "|"}};
  EXPECT_EQ(pairs, want);
}

// No alt may duplicate a character that already has a key of its own.
//
// This is the promotion mistake, and it is silent both ways: the SDK suppresses
// the hint for an alt that has its own face (so the long-press still works but
// nothing advertises it), while the key it hangs off keeps its reserved band and
// sits lower than a plain neighbour for a hint that is never drawn. ; and ? were
// exactly this case on 2026-08-11 -- promoted to faces, and their old alts had
// to come off : and / in the same edit.
// Every alt in the layout lives on ONE row (owner ruling 2026-08-11: "move all
// alts to row 3"). The band is reserved per row, so alts scattered across rows
// drop those rows' labels and leave the rest full-height -- which is legible but
// not what was asked, and drifts silently the moment a character is promoted or
// demoted without checking where its partner ended up.
TEST(KeyboardPanel, Grid13AltsAllLiveOnOneRow) {
  int rowsWithAlts = 0;
  for (int r = 0; r < grid13::SL_LAYOUT.rowCount; ++r) {
    const fui::KeyboardRow& row = grid13::SL_LAYOUT.rows[r];
    bool any = false;
    for (int c = 0; c < row.count; ++c) {
      const fui::KeyboardKey& k = row.keys[c];
      if (k.kind == fui::KeyKind::Normal && k.alt) any = true;
    }
    if (any) ++rowsWithAlts;
  }
  EXPECT_EQ(rowsWithAlts, 1);
}

// Rows 1 and 2 are a real keyboard's number row and its shifted face, column for
// column, EXCEPT the last -- which carries Del over Space, with Return under
// them on row 3, so the three read as one right-hand column (owner ruling
// 2026-08-11: "delete key needs to be in the upper right and space key needs to
// one row below that").
//
// Both halves are pinned. The parity is the reason the arrangement is learnable
// and is one careless insertion from being lost (; and ? sat in that row for a
// day and broke two pairs); the right column is an explicit ruling that a later
// reshuffle would otherwise quietly undo.
TEST(KeyboardPanel, Grid13NumberRowsAreColumnParallelExceptTheSpecialColumn) {
  const fui::KeyboardRow& shifted = grid13::SL_LAYOUT.rows[0];
  const fui::KeyboardRow& digits = grid13::SL_LAYOUT.rows[1];
  const fui::KeyboardRow& symbols = grid13::SL_LAYOUT.rows[2];
  ASSERT_EQ(shifted.count, 13);
  ASSERT_EQ(digits.count, 13);
  ASSERT_EQ(symbols.count, 13);

  const char* want[] = {"~", "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "_"};
  const char* base[] = {"`", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-"};
  for (int c = 0; c < 12; ++c) {
    EXPECT_STREQ(shifted.keys[c].output, want[c]) << "shifted face, column " << c;
    EXPECT_STREQ(digits.keys[c].output, base[c]) << "number row, column " << c;
  }

  // The right column, top to bottom.
  EXPECT_EQ(shifted.keys[12].kind, fui::KeyKind::Delete);
  EXPECT_EQ(digits.keys[12].kind, fui::KeyKind::Space);
  EXPECT_EQ(symbols.keys[12].kind, fui::KeyKind::Ok);
  // ...and they must line up, which only holds while all three rows are a full
  // 13 cells with no inset. An inset row would shift Return out from under Space.
  EXPECT_EQ(shifted.insetUnits, 0);
  EXPECT_EQ(digits.insetUnits, 0);
  EXPECT_EQ(symbols.insetUnits, 0);
}

TEST(KeyboardPanel, Grid13NoAltDuplicatesAKeyFace) {
  std::set<std::string> faces;
  for (int r = 0; r < grid13::SL_LAYOUT.rowCount; ++r) {
    const fui::KeyboardRow& row = grid13::SL_LAYOUT.rows[r];
    for (int c = 0; c < row.count; ++c) {
      const fui::KeyboardKey& k = row.keys[c];
      if (k.kind == fui::KeyKind::Normal && k.output) faces.insert(k.output);
    }
  }
  for (int r = 0; r < grid13::SL_LAYOUT.rowCount; ++r) {
    const fui::KeyboardRow& row = grid13::SL_LAYOUT.rows[r];
    for (int c = 0; c < row.count; ++c) {
      const fui::KeyboardKey& k = row.keys[c];
      if (k.kind != fui::KeyKind::Normal || !k.alt) continue;
      EXPECT_EQ(faces.count(k.alt), 0u)
          << "'" << k.alt << "' is both a key and the long-press of '" << (k.output ? k.output : "?")
          << "' -- the hint is suppressed, so that alt is unreachable-looking and its host is banded for nothing";
    }
  }
}

TEST(KeyboardPanel, Grid13CoversEveryPrintableAsciiCharacter) {
  // Space is the bottom row's Space key, which carries its own kind rather than
  // a printable label, so it is checked separately by Grid13SpaceIsARealSpaceKey.
  std::set<char> seen;
  for (int r = 0; r < grid13::SL_LAYOUT.rowCount; ++r) {
    const fui::KeyboardRow& row = grid13::SL_LAYOUT.rows[r];
    for (int c = 0; c < row.count; ++c) {
      const fui::KeyboardKey& k = row.keys[c];
      if (k.kind != fui::KeyKind::Normal) continue;
      if (k.output && std::strlen(k.output) == 1) seen.insert(k.output[0]);
      // A long-press alternate is typeable too -- that is the point of it.
      if (k.alt && std::strlen(k.alt) == 1) seen.insert(k.alt[0]);
    }
  }
  // Letters are lowercase on the face; uppercase comes from the long-press
  // case flip, which AltOutputGivesUppercaseForLetters covers.
  for (char ch = '!'; ch <= '~'; ++ch) {
    if (ch >= 'A' && ch <= 'Z') continue;
    EXPECT_EQ(seen.count(ch), 1u) << "no key types '" << ch << "'";
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
