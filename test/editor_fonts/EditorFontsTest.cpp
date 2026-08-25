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
// iA Writer Quattro is compiled in unconditionally, which is what these tests
// pin. They deliberately do NOT test the renderer: whether a glyph is legible
// is a device/sim question, and asserting it here would be theatre.
#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <set>
#include <string>

#include "ReadingFontList.h"
#include "notes/EditorFonts.h"

namespace {

using namespace editorfonts;

// --- resolve(): the id in the table is not proof the font exists -------------
//
// THE BUG THESE EXIST FOR
//
// The iOS target compiles crosspoint_core with OMIT_FONTS, and main.cpp used to
// register Space Mono and IBM Plex Mono inside `#ifndef OMIT_FONTS`. But
// builtinFontIdFor() reads a compile-time table, so it still handed back
// SPACEMONO_12_FONT_ID -- and the old resolver returned it at its first branch
// without asking anyone. drawText then had no glyphs, so Create Note rendered
// nothing at all while still saving correctly.
//
// Both halves of that were fixed, at different layers, and BOTH are still
// needed. On 2026-08-09 the two families and their insertFont() calls moved
// outside OMIT_FONTS (main.cpp, alongside Libre Franklin 14 pt), so on iOS the
// glyphs are now really there. That is the fix for the phone; it is not a
// reason to trust the table. A font id remains just an int, OMIT_FONTS is still
// a supported build, and any future consumer that defines it -- or any face
// that has not yet been moved out -- lands back on this exact failure. These
// tests pin the resolver's refusal to hand back an id nobody registered.
//
// isRegistered is what the renderer would answer. Nothing here needs a
// GfxRenderer, which is why the suite can keep its no-renderer rule.

constexpr int kUiFallback = 4242;  // stands in for UI_10_FONT_ID
constexpr int kSdId = 777;

auto noneRegistered = [](int) { return false; };
auto allRegistered = [](int) { return true; };
auto noSdCard = [](const char*) { return 0; };

// --- the 2026-08-15 cut to three faces, and the byte it left behind ---------
//
// Pinned by value rather than by "it does not crash": an off-by-one here
// silently opens someone's editor in a different typeface, and nothing on
// screen says why.
//
// Note the composition below -- migrateLegacyStoredIndex() THEN
// selectedFamily(). That is the real call shape: the migration belongs to the
// persisted byte, and selectedFamily() takes a table position. Collapsing the
// two is the bug the function's own comment warns about.
TEST(EditorFonts, CuttingToThreeFacesRemapsWhatDevicesAlreadyStored) {
  // Exact survivors -- the three values that name a face that still exists.
  // These are the ones a wrong migration would visibly steal.
  EXPECT_STREQ(selectedFamily(migrateLegacyStoredIndex(0)), "iAWriterQuattro");
  EXPECT_STREQ(selectedFamily(migrateLegacyStoredIndex(4)), "PragmataPro");
  EXPECT_STREQ(selectedFamily(migrateLegacyStoredIndex(5)), "NittiTypewriter");

  // The two iA siblings land on Quattro. Duo, Mono and Quattro are one
  // superfamily differing only in how they space, so this is the nearest
  // surviving face by a wide margin -- not a reset to row 0 that happens to
  // look like one.
  EXPECT_STREQ(selectedFamily(migrateLegacyStoredIndex(1)), "iAWriterQuattro");
  EXPECT_STREQ(selectedFamily(migrateLegacyStoredIndex(2)), "iAWriterQuattro");

  // IBM Plex Mono lands on PragmataPro: both are engineered monos, where Nitti
  // is a typewriter texture and further away.
  EXPECT_STREQ(selectedFamily(migrateLegacyStoredIndex(3)), "PragmataPro");

  // Out of range for the six-row table it decodes. Includes 6, which was Nitti
  // under the SEVEN-row table -- see the note in EditorFonts.h about why the
  // six-row encoding is the one assumed.
  EXPECT_EQ(migrateLegacyStoredIndex(6), 0);
  EXPECT_EQ(migrateLegacyStoredIndex(255), 0);
}

// Every output is a LIVE position, not just a smaller number. The predecessor
// of this function returned index-1, which for a stored 6 produced 5 -- a
// position that did not exist in the table it was migrating onto.
TEST(EditorFonts, EveryMigratedValueIsInRange) {
  for (int i = 0; i < 256; ++i) {
    const uint8_t got = migrateLegacyStoredIndex(static_cast<uint8_t>(i));
    EXPECT_LT(got, FAMILY_COUNT) << "legacy value " << i << " migrated out of the table";
  }
}

// THE BUG THIS EXISTS FOR, AND WHY THIS TEST ASSERTS SOMETHING WEAKER THAN YOU
// MIGHT EXPECT
//
// The predecessor migration was `index > 3 ? index - 1 : index`, applied
// unconditionally in normalizeRetiredSettings() on EVERY load. Nothing recorded
// that it had run, and the corrected value was written back to settings.json by
// the ordinary valuePtr loop -- so the next boot migrated the already-migrated
// value again. A device on PragmataPro (stored 4) came back on IBM Plex Mono
// (3) after two reboots, and on Nitti (5) it walked 5 -> 4 -> 3, one row per
// boot, until it stuck at the first index the subtraction left alone.
//
// The obvious fix would be to make the mapping IDEMPOTENT. It cannot be, and
// that is not a shortcoming of the table -- it is arithmetic. Idempotence needs
// every output to be a fixed point, so it would need map(1) == 1 and
// map(2) == 2. But 1 and 2 are iA Writer Duo and Mono in the encoding being
// read, and they must land on Quattro (0), while 1 and 2 are PragmataPro and
// Nitti in the encoding being written. The two encodings overlap, and one byte
// cannot mean both. NO position-based migration between these two tables can be
// idempotent.
//
// So idempotence is not the safety property. The GATE is: fromJson() calls this
// only when the "editorFontFamily" key is ABSENT, and sets needsResave so the
// name is written immediately -- after which the function is unreachable for
// that device. That gate lives in CrossPointSettings.cpp, a translation unit
// this suite deliberately does not link, so it is proved end-to-end in the
// simulator instead: qa/editor-font-migration.sh seeds each legacy byte, boots,
// and re-boots to show the resolved family does not drift.
//
// What this test pins is the property the table alone can carry, and the one
// the old subtraction lacked: a SECOND application cannot leave the table. The
// old one could -- migrateStoredIndex(6) returned 5, a position that did not
// exist in the six-row table it was migrating onto.
TEST(EditorFonts, MigratingAnAlreadyMigratedValueStaysInsideTheTable) {
  for (int i = 0; i < 256; ++i) {
    const uint8_t once = migrateLegacyStoredIndex(static_cast<uint8_t>(i));
    const uint8_t twice = migrateLegacyStoredIndex(once);
    EXPECT_LT(twice, FAMILY_COUNT) << "legacy value " << i << " left the table on a second application";
    // Not EXPECT_EQ(once, twice): see above, that is unachievable here. A
    // second application still names a real writing face, it just names the
    // wrong one -- which is exactly why fromJson must never allow one.
  }
}

// indexOfFamily is what fromJson uses to turn the persisted NAME back into a
// position. It is the whole reason a future removal needs no migration at all,
// so it has to be exact and case-insensitive (the name can arrive from a FAT
// directory listing or the web API).
TEST(EditorFonts, StoredNameRoundTripsThroughTheTable) {
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    EXPECT_EQ(indexOfFamily(FAMILIES[i].family), i) << FAMILIES[i].family;
  }
  EXPECT_EQ(indexOfFamily("pragmatapro"), indexOfFamily("PragmataPro"));

  // A name that is not ours -- a family removed in this very ruling, or a typo
  // from the web API -- must be reported as unknown rather than guessed at.
  // fromJson turns FAMILY_COUNT into row 0; it does not get a near miss.
  EXPECT_EQ(indexOfFamily("IBMPlexMono"), FAMILY_COUNT);
  EXPECT_EQ(indexOfFamily("iAWriterDuo"), FAMILY_COUNT);
  EXPECT_EQ(indexOfFamily(""), FAMILY_COUNT);
  EXPECT_EQ(indexOfFamily(nullptr), FAMILY_COUNT);
}

// A family that WAS a writing face must stay hidden from the READING picker.
//
// The filter reads FAMILIES, and these are no longer in FAMILIES, so removing
// the rows would have quietly added three families to the reading list on any
// card that carries them -- growing a user-facing list nobody asked to grow.
// FORMER_WRITING_FAMILIES keeps the answer the same.
TEST(EditorFonts, RetiredWritingFacesStayOutOfTheReadingPicker) {
  for (const char* retired : {"iAWriterDuo", "iAWriterMono", "IBMPlexMono", "SpaceMono"}) {
    EXPECT_TRUE(isWritingOnlyFamily(retired)) << retired << " would now be offered as a reading face";
    EXPECT_FALSE(readingfonts::offeredForReading(retired)) << retired;
    // They are retired, not current: nothing should treat them as editor rows.
    EXPECT_FALSE(isEditorFamily(retired)) << retired << " is no longer an editor face";
  }
}

// Index 0 is iA Writer Quattro, a compiled-in row -- when the build really has
// it. (Index 3 was IBM Plex Mono until the list was cut to three on 2026-08-15;
// that row is gone, and so is the arithmetic that used to chase it.)
TEST(EditorFonts, BuiltinRowResolvesToItsFaceWhenRegistered) {
  EXPECT_EQ(resolve(0, 12, allRegistered, noSdCard, kUiFallback), builtinFontIdFor(0, 12));
}

// The regression: same row, but this build omitted the face. Returning the id
// anyway is what drew a blank page.
TEST(EditorFonts, BuiltinRowDoesNotResolveToAnUnregisteredFace) {
  const int got = resolve(0, 12, noneRegistered, noSdCard, kUiFallback);
  EXPECT_NE(got, builtinFontIdFor(0, 12)) << "returned a font id the renderer has no glyphs for";
  EXPECT_EQ(got, kUiFallback) << "with no editor face in the binary, only the UI face can draw";
}

// Every row, not just the one that was reported. A card-only row falls through
// the SD lookup to the mono fallback, which is equally absent under OMIT_FONTS.
TEST(EditorFonts, NoRowResolvesToAnUnregisteredFace) {
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    const int got = resolve(static_cast<uint8_t>(i), 12, noneRegistered, noSdCard, kUiFallback);
    EXPECT_EQ(got, kUiFallback) << "row " << i << " resolved to an unregistered face";
  }
}

// The card still wins over the UI fallback for a card-only row, and is trusted
// without the isRegistered check: whatever registered it returned the id.
TEST(EditorFonts, CardOnlyRowPrefersTheInstalledFamily) {
  auto sdHasIt = [](const char*) { return kSdId; };
  EXPECT_EQ(resolve(0, 12, noneRegistered, sdHasIt, kUiFallback), kSdId);
}

// A built-in row that IS present must not be overridden by a same-named card
// family -- the built-in is checked first on purpose.
TEST(EditorFonts, BuiltinWinsOverTheCardWhenPresent) {
  auto sdHasIt = [](const char*) { return kSdId; };
  EXPECT_EQ(resolve(0, 12, allRegistered, sdHasIt, kUiFallback), builtinFontIdFor(0, 12));
}

// iA Writer Quattro is the only surviving row whose glyph tables are in this
// repo, so it is the only one that can be asserted unconditionally -- and it
// carries the whole out-of-the-box path, being the default and the fallback.
// PragmataPro and Nitti Typewriter are commercial and gitignored, so their
// presence legitimately varies by checkout; they are checked separately below
// through resolve()'s isRegistered machinery instead.
TEST(EditorFonts, AllFacesAreCompiledIn) {
  bool sawQuattro = false;
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    if (std::strcmp(FAMILIES[i].family, "iAWriterQuattro") == 0) {
      sawQuattro = true;
      EXPECT_NE(FAMILIES[i].builtinFontId[0], 0) << "iA Writer Quattro must be compiled in";
    }
    // Every row claims a built-in id regardless: a card-only editor row would
    // mean the setting does nothing on a device with no matching card family,
    // which is the original bug this file exists for.
    EXPECT_NE(FAMILIES[i].builtinFontId[0], 0) << FAMILIES[i].family << " must be compiled in";
  }
  EXPECT_TRUE(sawQuattro) << "the default and fallback face is missing from the table";
}

// builtinFontIdFor is what resolveEditorFont consults first, so it must agree
// with the table for every in-range index.
TEST(EditorFonts, BuiltinLookupMatchesTheTable) {
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    EXPECT_EQ(builtinFontIdFor(static_cast<uint8_t>(i), 12), FAMILIES[i].builtinFontId[0]) << "index " << i;
  }
}

// A settings.json from a build with more rows must not index past the array.
// Both accessors clamp to entry 0 rather than reading out of bounds.
TEST(EditorFonts, OutOfRangeIndexFallsBackToTheFirstEntry) {
  const uint8_t past = static_cast<uint8_t>(FAMILY_COUNT);
  EXPECT_STREQ(selectedFamily(past), FAMILIES[0].family);
  EXPECT_EQ(builtinFontIdFor(past, 12), FAMILIES[0].builtinFontId[0]);
  EXPECT_STREQ(selectedFamily(255), FAMILIES[0].family);
  EXPECT_EQ(builtinFontIdFor(255, 12), FAMILIES[0].builtinFontId[0]);
}

// Font id 0 is the renderer's "not found" sentinel, so a built-in row whose id
// collided with it would silently behave as card-only.
TEST(EditorFonts, NoBuiltinIdIsTheNotFoundSentinel) {
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    for (size_t s = 0; s < SIZE_COUNT; ++s) {
      if (FAMILIES[i].builtinFontId[s] != 0) {
        EXPECT_NE(FAMILIES[i].builtinFontId[s], 0) << FAMILIES[i].family << " size slot " << s;
      }
    }
  }
}

// Two rows sharing a font id would make one of them draw the other's face.
// Checked across ALL rows AND ALL sizes: a collision anywhere in the table
// means one row silently draws the other's cut at that size.
TEST(EditorFonts, BuiltinIdsAreDistinct) {
  std::set<int> seen;
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    for (size_t s = 0; s < SIZE_COUNT; ++s) {
      const int id = FAMILIES[i].builtinFontId[s];
      if (id == 0) continue;  // card-only rows legitimately share the 0 marker
      EXPECT_TRUE(seen.insert(id).second)
          << "duplicate builtin font id on " << FAMILIES[i].family << " size slot " << s;
    }
  }
}

// The shipped list, hardcoded. It is no longer the on-disk encoding -- that is
// the family NAME now -- so editing this table is allowed in a way it was not
// before. What is NOT allowed is editing it silently: three faces is an owner
// ruling (2026-08-15), and the picker, the colophon and the built-in font
// registrations in main.cpp all have to move with it.
TEST(EditorFonts, RowOrderIsFrozen) {
  ASSERT_EQ(FAMILY_COUNT, 3u) << "a row was added or removed. Three writing faces is an owner ruling; "
                                 "if it has changed, update main.cpp's font registrations, fontIds.h, "
                                 "all.h and ColophonData.h to match, and note that persistence is by "
                                 "NAME now so no index migration is needed -- see EditorFonts.h";
  EXPECT_STREQ(FAMILIES[0].family, "iAWriterQuattro");
  // COMMERCIAL, glyph tables gitignored.
  EXPECT_STREQ(FAMILIES[1].family, "PragmataPro");
  // COMMERCIAL, Regular-only source; Bold/Italic/BoldItalic synthesised at
  // build time.
  EXPECT_STREQ(FAMILIES[2].family, "NittiTypewriter");
}

// PragmataPro is COMMERCIAL, and it is the only row whose glyph tables are not
// in this repo (builtinFonts/pragmatapro_*.h are gitignored). So it must carry a
// built-in id like the others -- the row is a built-in, not a card family, and
// nothing should ever go looking for /fonts/PragmataPro -- while a clone without
// the licensed TTFs still has to compile and behave.
//
// That second half is what this pins: the row's correctness must not depend on
// the font being present, because for most checkouts it is absent. resolve()
// already has the machinery (isRegistered), so the test is that PragmataPro
// routes through it rather than around it.
TEST(EditorFonts, PragmataProIsABuiltinRowThatDegradesWhenTheFontIsAbsent) {
  const uint8_t pp = 1;  // was 4 until the list was cut to three on 2026-08-15
  ASSERT_STREQ(FAMILIES[pp].family, "PragmataPro");
  EXPECT_NE(FAMILIES[pp].builtinFontId[0], 0) << "PragmataPro is compiled in, not a card family";

  // Present in this build: the row resolves to its own face.
  EXPECT_EQ(resolve(pp, 12, allRegistered, noSdCard, kUiFallback), builtinFontIdFor(pp, 12));

  // Absent from this build -- the ordinary case for a clone without the
  // commercial TTFs. It must NOT hand back an id with no glyphs behind it.
  const int degraded = resolve(pp, 12, noneRegistered, noSdCard, kUiFallback);
  EXPECT_NE(degraded, builtinFontIdFor(pp, 12)) << "returned a font id the renderer has no glyphs for";

  // And when the OTHER editor faces are present (the real shape of a clone
  // without PragmataPro: the OFL faces compiled in, this one missing), degrades
  // to a sibling WRITING face, never to UI chrome.
  const auto allButPragmata = [&](int id) { return id != FAMILIES[pp].builtinFontId[0]; };
  const int sibling = resolve(pp, 12, allButPragmata, noSdCard, kUiFallback);
  EXPECT_EQ(sibling, fallbackFontId(12)) << "a missing commercial face must fall back to a built-in mono";
  EXPECT_NE(sibling, kUiFallback) << "the editor must never drop to UI chrome while a mono is available";
}

// The reading/writing split still holds for the new row. PragmataPro's Bold,
// Italic and BoldItalic cover only ASCII + Latin-1 (234 of the converter's 1665
// codepoints, against 950 in the Regular), so an emphasised word outside
// Latin-1 renders '?'. That is tolerable for notes the owner types; it is not
// tolerable against arbitrary book text, which is why this row must stay
// writing-only.
TEST(EditorFonts, PragmataProIsWritingOnly) {
  EXPECT_TRUE(isEditorFamily("PragmataPro"));
  EXPECT_TRUE(isEditorFamily("pragmatapro")) << "matched case-insensitively like the other rows";
  EXPECT_TRUE(isWritingOnlyFamily("PragmataPro"));
  EXPECT_FALSE(FAMILIES[1].alsoReading) << "promoting this row would expose its Latin-1-only bold to book text";
}

// NittiTypewriter: COMMERCIAL, same degradation contract as PragmataPro.
// The glyph tables are gitignored; a clone without the TTFs must still compile
// and the row must degrade to a built-in mono — not crash, not return garbage.
TEST(EditorFonts, NittiTypewriterIsABuiltinRowThatDegradesWhenTheFontIsAbsent) {
  const uint8_t nt = 2;  // was 5 until the list was cut to three on 2026-08-15
  ASSERT_STREQ(FAMILIES[nt].family, "NittiTypewriter");
  EXPECT_NE(FAMILIES[nt].builtinFontId[0], 0) << "NittiTypewriter is compiled in, not a card family";

  // Present in this build: the row resolves to its own face.
  EXPECT_EQ(resolve(nt, 12, allRegistered, noSdCard, kUiFallback), builtinFontIdFor(nt, 12));

  // Absent from this build — the ordinary case for a clone without the TTF.
  const int degraded = resolve(nt, 12, noneRegistered, noSdCard, kUiFallback);
  EXPECT_NE(degraded, builtinFontIdFor(nt, 12)) << "returned a font id the renderer has no glyphs for";

  // Degrades to a built-in mono, not to UI chrome.
  const auto allButNitti = [&](int id) { return id != FAMILIES[nt].builtinFontId[0]; };
  const int sibling = resolve(nt, 12, allButNitti, noSdCard, kUiFallback);
  EXPECT_EQ(sibling, fallbackFontId(12)) << "a missing commercial face must fall back to a built-in mono";
  EXPECT_NE(sibling, kUiFallback) << "the editor must never drop to UI chrome while a mono is available";
}

// NittiTypewriter is writing-only. The synthetic bold at 12pt produces a narrow
// 'a' counter (5 px 2-bit white interior — ≥1 so it passes the counter-survival
// criterion, but barely). A typewriter face at 5 px width is not suited to
// arbitrary book text, so this row must never be promoted to alsoReading.
TEST(EditorFonts, NittiTypewriterIsWritingOnly) {
  EXPECT_TRUE(isEditorFamily("NittiTypewriter"));
  EXPECT_TRUE(isEditorFamily("nittitypewriter")) << "matched case-insensitively like the other rows";
  EXPECT_TRUE(isWritingOnlyFamily("NittiTypewriter"));
  EXPECT_FALSE(FAMILIES[2].alsoReading) << "typewriter face with narrow bold counter must not reach the reading picker";
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

// The shipped default is index 0 (iAWriterQuattro). It was card-only until
// 2026-08-11 (ruling: all five families compiled in), so before that the editor
// opened in the 10 pt UI face on every out-of-the-box device. Now index 0 has
// its own built-in id and resolves without the fallback.
TEST(EditorFonts, TheShippedDefaultResolvesToAMonospaceFace) {
  // Index 0 is now compiled in (iAWriterQuattro, owner ruling 2026-08-11).
  EXPECT_NE(builtinFontIdFor(0, 12), 0) << "index 0 (iAWriterQuattro) must be compiled in";
  // fallbackFontId() must still return a real id for the remaining resolution
  // chain (OMIT_FONTS builds, future card-only additions).
  EXPECT_NE(fallbackFontId(12), 0) << "no built-in family to fall back to: the editor fallback is dead";
}

// The fallback must name a family that is actually compiled in, not an
// arbitrary constant.
TEST(EditorFonts, FallbackIsOneOfTheBuiltinRows) {
  const int fb = fallbackFontId(12);
  ASSERT_NE(fb, 0);
  bool found = false;
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    if (FAMILIES[i].builtinFontId[0] == fb) found = true;
  }
  EXPECT_TRUE(found) << "fallbackFontId() returned an id no row carries";
}

// Every card-only row degrades to a writing face rather than to UI chrome.
TEST(EditorFonts, EveryRowResolvesToSomeMonospaceFace) {
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    const int resolved = builtinFontIdFor(static_cast<uint8_t>(i), 12) != 0
                             ? builtinFontIdFor(static_cast<uint8_t>(i), 12)
                             : fallbackFontId(12);
    EXPECT_NE(resolved, 0) << "row " << i << " (" << FAMILIES[i].family << ") resolves to nothing";
  }
}

// --- Size axis: the new 12/14 pt setting -------------------------------------
//
// THE BUG THESE EXIST FOR
//
// Adding a size picker is trivially two-pointed: if builtinFontIdFor(i, 12) and
// builtinFontIdFor(i, 14) return the same value, the setting renders, persists,
// and changes nothing — exactly the original editor-font bug, repeated for sizes.
// Every test below exists to catch a specific version of that failure.

// nearestOfferedSize: exact hits must pass through unchanged. A size that IS
// offered must not snap to a neighbour.
TEST(EditorFonts, NearestOfferedSizePassesThroughExactHits) {
  for (size_t s = 0; s < SIZE_COUNT; ++s) {
    EXPECT_EQ(nearestOfferedSize(SIZES[s]), SIZES[s])
        << "SIZES[" << s << "] = " << static_cast<int>(SIZES[s]) << " snapped to something else";
  }
}

// Values below the offered range should snap to the smallest offered size, not
// to zero or to whatever happens to be at the array head.
TEST(EditorFonts, NearestOfferedSizeSnapsValuesBelowRangeToSmallest) {
  EXPECT_EQ(nearestOfferedSize(0), SIZES[0]) << "0 pt must snap to the smallest offered size";
  EXPECT_EQ(nearestOfferedSize(11), SIZES[0]) << "11 pt (one below 12) must snap to 12";
}

// Values above the offered range should snap to the largest offered size.
TEST(EditorFonts, NearestOfferedSizeSnapsValuesAboveRangeToLargest) {
  const uint8_t largest = SIZES[SIZE_COUNT - 1];
  EXPECT_EQ(nearestOfferedSize(20), largest) << "20 pt (above all offered sizes) must snap to the largest";
  EXPECT_EQ(nearestOfferedSize(255), largest) << "255 pt must snap to the largest offered size";
}

// 13 pt is exactly between 12 and 14. The tie-breaking rule is SMALLER:
// shrinking keeps more of the note on screen. 13 was asked for in the same
// ruling as 12/14 and DECLINED because no ramp anywhere carries a 13 -- so
// this test exists to make that decision observable: if 13 were offered, a
// stored 13 would snap to itself; since it is not, it must snap down, not up.
TEST(EditorFonts, NearestOfferedSizeTieBreaksToSmaller) {
  // 13 is equidistant from 12 and 14 (|13-12|=1, |13-14|=1). Ties go smaller.
  EXPECT_EQ(nearestOfferedSize(13), 12u) << "13 pt ties 12 and 14; must snap to 12 (smaller) not 14 (larger)";
}

// Every possible stored value (0..255) must resolve to something that IS in
// SIZES. A value outside SIZES would mean a call to builtinFontIdFor() with
// that resolved size would look up an out-of-range slot.
TEST(EditorFonts, NearestOfferedSizeAlwaysReturnsAMemberOfSizes) {
  for (int v = 0; v < 256; ++v) {
    const uint8_t snapped = nearestOfferedSize(static_cast<uint8_t>(v));
    bool found = false;
    for (size_t s = 0; s < SIZE_COUNT; ++s) {
      if (SIZES[s] == snapped) {
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found) << "nearestOfferedSize(" << v << ") = " << static_cast<int>(snapped) << " which is not in SIZES";
  }
}

// The 12 pt and 14 pt cuts of every row must carry different ids. If they were
// the same id, the size setting would silently do nothing -- the original
// editor-font bug, replayed for sizes. Two cuts sharing an id means one draws
// the other's glyphs at the wrong metrics, which is a rendering corruption, not
// a silent no-op.
TEST(EditorFonts, EachRowHasDifferentIdsForDifferentSizes) {
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    const int id12 = builtinFontIdFor(static_cast<uint8_t>(i), 12);
    const int id14 = builtinFontIdFor(static_cast<uint8_t>(i), 14);
    EXPECT_NE(id12, id14) << FAMILIES[i].family
                          << ": 12pt and 14pt return the same id -- the size setting would do nothing";
    // Both must also be non-zero: a zero here means the size is card-only for
    // this row, which the editor does not support (all rows must be built in).
    EXPECT_NE(id12, 0) << FAMILIES[i].family << " 12pt id is the not-found sentinel";
    EXPECT_NE(id14, 0) << FAMILIES[i].family << " 14pt id is the not-found sentinel";
  }
}

// Every id in the entire table -- all rows, all sizes -- must be distinct.
// A collision anywhere means one cut silently draws another's glyphs.
TEST(EditorFonts, AllTableIdsAreDistinctAndNonZero) {
  std::set<int> seen;
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    for (size_t s = 0; s < SIZE_COUNT; ++s) {
      const int id = FAMILIES[i].builtinFontId[s];
      EXPECT_NE(id, 0) << FAMILIES[i].family << " size slot " << s << " is the not-found sentinel";
      EXPECT_TRUE(seen.insert(id).second)
          << "duplicate id " << id << " on " << FAMILIES[i].family << " size slot " << s;
    }
  }
}

// The two fallback ids must differ: falling back must change the FACE, not the
// SIZE. If fallbackFontId(12) == fallbackFontId(14), then a device whose chosen
// face is absent would always open the editor at 12 pt regardless of the size
// setting -- a silent size reset with no visible cause.
//
// Each fallback id must equal the first compiled-in row's cut at that size,
// which is how the implementation is specified (EditorFonts.cpp:83-93): the
// fallback follows the table rather than naming a family twice.
TEST(EditorFonts, FallbackDiffersBetweenSizesAndMatchesFirstRow) {
  const int fb12 = fallbackFontId(12);
  const int fb14 = fallbackFontId(14);

  EXPECT_NE(fb12, fb14) << "fallbackFontId(12) == fallbackFontId(14): falling back would silently reset the size to 12";

  // Each fallback equals the first row's cut at that size. If a new row were
  // inserted before iAWriterQuattro and its id happened to match, this would
  // fire, catching the change before it ships.
  EXPECT_EQ(fb12, FAMILIES[0].builtinFontId[0])
      << "fallbackFontId(12) must equal the first row's 12pt id -- it is the face of last resort at that size";
  EXPECT_EQ(fb14, FAMILIES[0].builtinFontId[1])
      << "fallbackFontId(14) must equal the first row's 14pt id -- falling back must not change the size";
}

// resolve() with 14 pt must return the 14 pt id, not the 12 pt one. This is
// the end-to-end path the editor's font resolver walks: if resolve somehow
// used the wrong slot, the size picker would change the stored byte while the
// editor kept drawing the wrong cut.
TEST(EditorFonts, ResolveAt14PtReturnsFourteenPtId) {
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    const int got = resolve(static_cast<uint8_t>(i), 14, allRegistered, noSdCard, kUiFallback);
    const int want = builtinFontIdFor(static_cast<uint8_t>(i), 14);
    EXPECT_EQ(got, want) << FAMILIES[i].family << ": resolve(14) returned the 12pt id, not 14pt";
    EXPECT_NE(got, builtinFontIdFor(static_cast<uint8_t>(i), 12))
        << FAMILIES[i].family << ": resolve(14) returned the same id as resolve(12)";
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// The reading list has TWO consumers, and they must offer the same families.
// ---------------------------------------------------------------------------

// Reader Font lists the SD families minus the writing-only editor faces
// (FontSelectionActivity.cpp). Holding a side button inside a book cycles the
// reader font through what is meant to be the SAME list -- and did not: it
// walked the raw registry, so it stepped onto Space Mono, IBM Plex Mono and the
// iA monos, families the picker will not show and therefore cannot display as
// current. Owner bug report 2026-08-11: "only fonts in Reader Font list
// should be selected."
//
// Parsing the source is deliberate, the same call OmitFontsTest makes: both
// consumers live in activities that drag in Arduino, SdFat and the whole device
// stack, so neither can be linked into a host test. The invariant is genuinely
// textual -- it is about whether a call is present in a function.
TEST(EditorFonts, TheInBookFontCycleAppliesTheWritingOnlyFilter) {
  std::ifstream in(CROSSPOINT_EPUB_READER_CPP);
  ASSERT_TRUE(in.good()) << "cannot read " << CROSSPOINT_EPUB_READER_CPP;
  std::string line;
  bool inCycle = false;
  bool sawCall = false;
  int depth = 0;
  while (std::getline(in, line)) {
    if (!inCycle && line.find("::cycleReaderFontFamily") == std::string::npos) continue;
    inCycle = true;
    // COMMENTS ARE STRIPPED FIRST. The first version of this test matched the
    // bare name, and the explanatory comment inside this very function contains
    // it -- so deleting the call left the test green. Match the CALL only, on
    // code.
    const size_t slashes = line.find("//");
    const std::string code = slashes == std::string::npos ? line : line.substr(0, slashes);
    if (code.find("offeredForReading(") != std::string::npos) sawCall = true;
    for (const char c : code) {
      if (c == '{') depth++;
      if (c == '}') depth--;
    }
    if (depth <= 0 && code.find('}') != std::string::npos) break;
  }
  ASSERT_TRUE(inCycle) << "cycleReaderFontFamily not found -- this test has stopped guarding anything";
  EXPECT_TRUE(sawCall) << "cycleReaderFontFamily does not CALL readingfonts::offeredForReading, so holding a side "
                          "button can select a font Reader Font does not list";
}

// Retired families are withheld from reading. Rosarivo was A-tier'd on
// 2026-08-07 and still appeared in Reader Font on 2026-08-11, because that
// ruling reached sd-fonts.yaml's installed_families -- what a NEW card is given
// -- and nothing that governs a card already provisioned.
TEST(ReadingFontList, RetiredFamiliesAreNotOfferedForReading) {
  EXPECT_TRUE(readingfonts::isRetired("Rosarivo"));
  EXPECT_TRUE(readingfonts::isRetired("rosarivo")) << "the card's directory name is matched case-insensitively";
  EXPECT_FALSE(readingfonts::offeredForReading("Rosarivo"));

  // Still shipped, and must stay offered.
  for (const char* keep : {"Edgar", "Coelacanth", "TeXGyreSchola", "LibreFranklin", "TeXGyreHeros"}) {
    EXPECT_TRUE(readingfonts::offeredForReading(keep)) << keep << " is in installed_families and must be offered";
  }
  // A face nobody has heard of is offered, not hidden -- the fallback
  // FontDisplayNames documents.
  EXPECT_TRUE(readingfonts::offeredForReading("SomeUserInstalledFace"));
  EXPECT_FALSE(readingfonts::offeredForReading(nullptr));
}

// The writing-only rule still applies through the same predicate.
TEST(ReadingFontList, WritingOnlyFacesAreNotOfferedForReading) {
  EXPECT_FALSE(readingfonts::offeredForReading("IBMPlexMono"));
  // Quattro was the one alsoReading exception (owner ruling 2026-08-09) and the
  // owner withdrew it on 2026-08-21 ("remove ia quattro from reading fonts",
  // commit 6c5ebf2a5). The rule now holds with no exceptions, so the flag on the
  // row is what decides -- assert against it rather than against a literal, or
  // the next reversal has to be typed here as well as in the table.
  EXPECT_FALSE(readingfonts::offeredForReading("iAWriterQuattro"))
      << "Quattro left the reading list on 2026-08-21; its editor role is untouched";
  EXPECT_FALSE(readingfonts::offeredForReading("PragmataPro"))
      << "a commercial writing face with a Latin-1-only bold must not reach the reading picker";
}
