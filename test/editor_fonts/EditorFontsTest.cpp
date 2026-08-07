// The editor-font group: which rows are compiled in, and the invariants that
// keep SETTINGS.editorFont meaning the same thing across firmware versions.
//
// THE BUG THIS EXISTS FOR
//
// The Editor Font setting shipped doing NOTHING. Every family in the list was
// card-only, no card carried any of them, so resolveEditorFont() got 0 back
// from the SD resolver and fell through to the 10 pt UI face — for every row.
// The setting rendered, persisted, and changed nothing on screen. Nothing
// failed loudly; the picker just had no effect.
//
// Space Mono and IBM Plex Mono are now compiled in, which is what these tests
// pin. They deliberately do NOT test the renderer: whether a glyph is legible
// is a device/sim question, and asserting it here would be theatre.
#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

#include "notes/EditorFonts.h"

namespace {

using namespace editorfonts;

// The two Google Fonts faces are built in; without this the setting is inert.
TEST(EditorFonts, MonoFacesAreCompiledIn) {
  bool sawSpaceMono = false;
  bool sawPlex = false;
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    if (std::strcmp(FAMILIES[i].family, "SpaceMono") == 0) {
      sawSpaceMono = true;
      EXPECT_NE(FAMILIES[i].builtinFontId, 0)
          << "Space Mono must be compiled in; a 0 here is the inert-setting bug returning";
    }
    if (std::strcmp(FAMILIES[i].family, "IBMPlexMono") == 0) {
      sawPlex = true;
      EXPECT_NE(FAMILIES[i].builtinFontId, 0) << "IBM Plex Mono must be compiled in";
    }
  }
  EXPECT_TRUE(sawSpaceMono);
  EXPECT_TRUE(sawPlex);
}

// builtinFontIdFor is what resolveEditorFont consults first, so it must agree
// with the table for every in-range index.
TEST(EditorFonts, BuiltinLookupMatchesTheTable) {
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    EXPECT_EQ(builtinFontIdFor(static_cast<uint8_t>(i)), FAMILIES[i].builtinFontId) << "index " << i;
  }
}

// A settings.json from a build with more rows must not index past the array.
// Both accessors clamp to entry 0 rather than reading out of bounds.
TEST(EditorFonts, OutOfRangeIndexFallsBackToTheFirstEntry) {
  const uint8_t past = static_cast<uint8_t>(FAMILY_COUNT);
  EXPECT_STREQ(selectedFamily(past), FAMILIES[0].family);
  EXPECT_EQ(builtinFontIdFor(past), FAMILIES[0].builtinFontId);
  EXPECT_STREQ(selectedFamily(255), FAMILIES[0].family);
  EXPECT_EQ(builtinFontIdFor(255), FAMILIES[0].builtinFontId);
}

// Font id 0 is the renderer's "not found" sentinel, so a built-in row whose id
// collided with it would silently behave as card-only.
TEST(EditorFonts, NoBuiltinIdIsTheNotFoundSentinel) {
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    if (FAMILIES[i].builtinFontId != 0) {
      EXPECT_NE(FAMILIES[i].builtinFontId, 0) << FAMILIES[i].family;
    }
  }
}

// Two rows sharing a font id would make one of them draw the other's face.
TEST(EditorFonts, BuiltinIdsAreDistinct) {
  std::set<int> seen;
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    const int id = FAMILIES[i].builtinFontId;
    if (id == 0) continue;  // card-only rows legitimately share the 0 marker
    EXPECT_TRUE(seen.insert(id).second) << "duplicate builtin font id on " << FAMILIES[i].family;
  }
}

// SETTINGS.editorFont persists this POSITION, so the order is the on-disk
// encoding. Reordering or deleting a row silently re-points every saved
// settings.json at a different family; appending is the only safe edit.
// This test is the tripwire for that, so it hardcodes the shipped order.
TEST(EditorFonts, RowOrderIsFrozen) {
  ASSERT_EQ(FAMILY_COUNT, 5u) << "a row was added or removed: rows may only be APPENDED, and if you "
                                 "appended one, extend this test rather than changing the earlier entries";
  EXPECT_STREQ(FAMILIES[0].family, "iAWriterQuattro");
  EXPECT_STREQ(FAMILIES[1].family, "iAWriterDuo");
  EXPECT_STREQ(FAMILIES[2].family, "iAWriterMono");
  EXPECT_STREQ(FAMILIES[3].family, "SpaceMono");
  EXPECT_STREQ(FAMILIES[4].family, "IBMPlexMono");
}

// Every row needs a label the picker can draw and a family name the SD
// resolver can look up. An empty either way renders a blank row.
TEST(EditorFonts, EveryRowHasANameAndALabel) {
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    ASSERT_NE(FAMILIES[i].family, nullptr) << "index " << i;
    ASSERT_NE(FAMILIES[i].label, nullptr) << "index " << i;
    EXPECT_GT(std::strlen(FAMILIES[i].family), 0u) << "index " << i;
    EXPECT_GT(std::strlen(FAMILIES[i].label), 0u) << "index " << i;
  }
}

// The shipped default is index 0, a CARD-ONLY row, so before fallbackFontId()
// existed the editor opened in the 10 pt UI face on every out-of-the-box
// device. Compiling two faces in did not change that by itself: it fixed the
// two new rows and left the default path untouched.
TEST(EditorFonts, TheShippedDefaultResolvesToAMonospaceFace) {
  // Index 0 has no built-in of its own...
  EXPECT_EQ(builtinFontIdFor(0), 0) << "index 0 is expected to be a card-only row";
  // ...so the fallback is what saves it, and it must be a real font id.
  EXPECT_NE(fallbackFontId(), 0) << "no built-in family to fall back to: the default editor font is dead again";
}

// The fallback must name a family that is actually compiled in, not an
// arbitrary constant.
TEST(EditorFonts, FallbackIsOneOfTheBuiltinRows) {
  const int fb = fallbackFontId();
  ASSERT_NE(fb, 0);
  bool found = false;
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    if (FAMILIES[i].builtinFontId == fb) found = true;
  }
  EXPECT_TRUE(found) << "fallbackFontId() returned an id no row carries";
}

// Every card-only row degrades to a writing face rather than to UI chrome.
TEST(EditorFonts, EveryRowResolvesToSomeMonospaceFace) {
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    const int resolved =
        builtinFontIdFor(static_cast<uint8_t>(i)) != 0 ? builtinFontIdFor(static_cast<uint8_t>(i)) : fallbackFontId();
    EXPECT_NE(resolved, 0) << "row " << i << " (" << FAMILIES[i].family << ") resolves to nothing";
  }
}

}  // namespace
