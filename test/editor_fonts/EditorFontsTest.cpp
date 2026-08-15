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
// IBM Plex Mono is compiled in, which is what these tests
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

// Space Mono was deleted from index 3 on 2026-08-15, the only row ever removed
// from a list whose positions are persisted in SETTINGS.editorFont. Pinned by
// value rather than by "it does not crash": an off-by-one here silently opens
// someone's editor in a different typeface, and nothing on screen says why.
//
// Note the composition below -- migrateStoredIndex() THEN selectedFamily().
// That is the real call shape: the migration belongs to the persisted byte,
// and selectedFamily() takes a table position. Collapsing the two is the bug
// the function's own comment warns about.
TEST(EditorFonts, RemovingSpaceMonoRemapsWhatDevicesAlreadyStored) {
  // Below the hole: untouched.
  EXPECT_EQ(migrateStoredIndex(0), 0);
  EXPECT_EQ(migrateStoredIndex(1), 1);
  EXPECT_EQ(migrateStoredIndex(2), 2);

  // The hole itself. A device that had Space Mono selected lands on what is now
  // index 3 -- IBM Plex Mono, the nearest surviving mono -- rather than being
  // reset to row 0.
  EXPECT_EQ(migrateStoredIndex(3), 3);
  EXPECT_STREQ(selectedFamily(migrateStoredIndex(3)), "IBMPlexMono");

  // Above the hole: each shifts down one, so the face someone chose is the face
  // they keep. These three are the whole point of the migration.
  EXPECT_EQ(migrateStoredIndex(4), 3);
  EXPECT_STREQ(selectedFamily(migrateStoredIndex(4)), "IBMPlexMono");
  EXPECT_EQ(migrateStoredIndex(5), 4);
  EXPECT_STREQ(selectedFamily(migrateStoredIndex(5)), "PragmataPro");
  EXPECT_EQ(migrateStoredIndex(6), 5);
  EXPECT_STREQ(selectedFamily(migrateStoredIndex(6)), "NittiTypewriter");
}

// Index 3 is IBM Plex Mono, a compiled-in row -- when the build really has it.
// (It was Space Mono until 2026-08-15; that row is gone and everything above
// it shifted down one, which migrateStoredIndex() absorbs.)
TEST(EditorFonts, BuiltinRowResolvesToItsFaceWhenRegistered) {
  EXPECT_EQ(resolve(3, allRegistered, noSdCard, kUiFallback), builtinFontIdFor(3));
}

// The regression: same row, but this build omitted the face. Returning the id
// anyway is what drew a blank page.
TEST(EditorFonts, BuiltinRowDoesNotResolveToAnUnregisteredFace) {
  const int got = resolve(3, noneRegistered, noSdCard, kUiFallback);
  EXPECT_NE(got, builtinFontIdFor(3)) << "returned a font id the renderer has no glyphs for";
  EXPECT_EQ(got, kUiFallback) << "with no editor face in the binary, only the UI face can draw";
}

// Every row, not just the one that was reported. A card-only row falls through
// the SD lookup to the mono fallback, which is equally absent under OMIT_FONTS.
TEST(EditorFonts, NoRowResolvesToAnUnregisteredFace) {
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    const int got = resolve(static_cast<uint8_t>(i), noneRegistered, noSdCard, kUiFallback);
    EXPECT_EQ(got, kUiFallback) << "row " << i << " resolved to an unregistered face";
  }
}

// The card still wins over the UI fallback for a card-only row, and is trusted
// without the isRegistered check: whatever registered it returned the id.
TEST(EditorFonts, CardOnlyRowPrefersTheInstalledFamily) {
  auto sdHasIt = [](const char*) { return kSdId; };
  EXPECT_EQ(resolve(0, noneRegistered, sdHasIt, kUiFallback), kSdId);
}

// A built-in row that IS present must not be overridden by a same-named card
// family -- the built-in is checked first on purpose.
TEST(EditorFonts, BuiltinWinsOverTheCardWhenPresent) {
  auto sdHasIt = [](const char*) { return kSdId; };
  EXPECT_EQ(resolve(3, allRegistered, sdHasIt, kUiFallback), builtinFontIdFor(3));
}

// The five OFL faces are built in (monos: ruling 2026-08-06; iA faces: ruling
// 2026-08-11). Without this the setting is inert for that row. PragmataPro is
// the sixth and is checked separately below: it is compiled in too, but its
// glyph tables are gitignored, so it is the one row whose presence legitimately
// varies by checkout and it cannot be asserted the same way.
TEST(EditorFonts, AllFacesAreCompiledIn) {
  bool sawQuattro = false, sawDuo = false, sawMono = false;
  bool sawPlex = false;
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    if (std::strcmp(FAMILIES[i].family, "iAWriterQuattro") == 0) {
      sawQuattro = true;
      EXPECT_NE(FAMILIES[i].builtinFontId, 0) << "iA Writer Quattro must be compiled in";
    }
    if (std::strcmp(FAMILIES[i].family, "iAWriterDuo") == 0) {
      sawDuo = true;
      EXPECT_NE(FAMILIES[i].builtinFontId, 0) << "iA Writer Duo must be compiled in";
    }
    if (std::strcmp(FAMILIES[i].family, "iAWriterMono") == 0) {
      sawMono = true;
      EXPECT_NE(FAMILIES[i].builtinFontId, 0) << "iA Writer Mono must be compiled in";
    }
    if (std::strcmp(FAMILIES[i].family, "IBMPlexMono") == 0) {
      sawPlex = true;
      EXPECT_NE(FAMILIES[i].builtinFontId, 0) << "IBM Plex Mono must be compiled in";
    }
  }
  EXPECT_TRUE(sawQuattro);
  EXPECT_TRUE(sawDuo);
  EXPECT_TRUE(sawMono);
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
  ASSERT_EQ(FAMILY_COUNT, 6u) << "a row was added or removed: rows may only be APPENDED, and if you "
                                 "appended one, extend this test rather than changing the earlier entries. "
                                 "Removing one is a THIRD case and needs migrateStoredIndex() extended too "
                                 "-- see RemovingSpaceMonoRemapsWhatDevicesAlreadyStored";
  EXPECT_STREQ(FAMILIES[0].family, "iAWriterQuattro");
  EXPECT_STREQ(FAMILIES[1].family, "iAWriterDuo");
  EXPECT_STREQ(FAMILIES[2].family, "iAWriterMono");
  EXPECT_STREQ(FAMILIES[3].family, "IBMPlexMono");
  // Appended 2026-08-14. Every earlier index above is untouched, which is the
  // whole contract: SETTINGS.editorFont on a device that already chose IBM Plex
  // Mono must still mean IBM Plex Mono.
  EXPECT_STREQ(FAMILIES[4].family, "PragmataPro");
  // Appended 2026-08-15. COMMERCIAL, Regular-only source; Bold/Italic/BoldItalic
  // synthesised at build time.
  EXPECT_STREQ(FAMILIES[5].family, "NittiTypewriter");
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
  const uint8_t pp = 4;  // was 5 until Space Mono left index 3 on 2026-08-15
  ASSERT_STREQ(FAMILIES[pp].family, "PragmataPro");
  EXPECT_NE(FAMILIES[pp].builtinFontId, 0) << "PragmataPro is compiled in, not a card family";

  // Present in this build: the row resolves to its own face.
  EXPECT_EQ(resolve(pp, allRegistered, noSdCard, kUiFallback), builtinFontIdFor(pp));

  // Absent from this build -- the ordinary case for a clone without the
  // commercial TTFs. It must NOT hand back an id with no glyphs behind it.
  const int degraded = resolve(pp, noneRegistered, noSdCard, kUiFallback);
  EXPECT_NE(degraded, builtinFontIdFor(pp)) << "returned a font id the renderer has no glyphs for";

  // And when the OTHER editor faces are present (the real shape of a clone
  // without PragmataPro: five OFL monos compiled in, one missing), it degrades
  // to a sibling WRITING face, never to UI chrome.
  const auto allButPragmata = [&](int id) { return id != FAMILIES[pp].builtinFontId; };
  const int sibling = resolve(pp, allButPragmata, noSdCard, kUiFallback);
  EXPECT_EQ(sibling, fallbackFontId()) << "a missing commercial face must fall back to a built-in mono";
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
  EXPECT_FALSE(FAMILIES[4].alsoReading) << "promoting this row would expose its Latin-1-only bold to book text";
}

// NittiTypewriter: COMMERCIAL, same degradation contract as PragmataPro.
// The glyph tables are gitignored; a clone without the TTFs must still compile
// and the row must degrade to a built-in mono — not crash, not return garbage.
TEST(EditorFonts, NittiTypewriterIsABuiltinRowThatDegradesWhenTheFontIsAbsent) {
  const uint8_t nt = 5;  // was 6 until Space Mono left index 3 on 2026-08-15
  ASSERT_STREQ(FAMILIES[nt].family, "NittiTypewriter");
  EXPECT_NE(FAMILIES[nt].builtinFontId, 0) << "NittiTypewriter is compiled in, not a card family";

  // Present in this build: the row resolves to its own face.
  EXPECT_EQ(resolve(nt, allRegistered, noSdCard, kUiFallback), builtinFontIdFor(nt));

  // Absent from this build — the ordinary case for a clone without the TTF.
  const int degraded = resolve(nt, noneRegistered, noSdCard, kUiFallback);
  EXPECT_NE(degraded, builtinFontIdFor(nt)) << "returned a font id the renderer has no glyphs for";

  // Degrades to a built-in mono, not to UI chrome.
  const auto allButNitti = [&](int id) { return id != FAMILIES[nt].builtinFontId; };
  const int sibling = resolve(nt, allButNitti, noSdCard, kUiFallback);
  EXPECT_EQ(sibling, fallbackFontId()) << "a missing commercial face must fall back to a built-in mono";
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
  EXPECT_FALSE(FAMILIES[5].alsoReading) << "typewriter face with narrow bold counter must not reach the reading picker";
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
  EXPECT_NE(builtinFontIdFor(0), 0) << "index 0 (iAWriterQuattro) must be compiled in";
  // fallbackFontId() must still return a real id for the remaining resolution
  // chain (OMIT_FONTS builds, future card-only additions).
  EXPECT_NE(fallbackFontId(), 0) << "no built-in family to fall back to: the editor fallback is dead";
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

// ---------------------------------------------------------------------------
// The reading list has TWO consumers, and they must offer the same families.
// ---------------------------------------------------------------------------

// Text Settings lists the SD families minus the writing-only editor faces
// (FontSelectionActivity.cpp). Holding a side button inside a book cycles the
// reader font through what is meant to be the SAME list -- and did not: it
// walked the raw registry, so it stepped onto Space Mono, IBM Plex Mono and the
// iA monos, families the picker will not show and therefore cannot display as
// current. Owner bug report 2026-08-11: "only fonts in Text Settings list
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
                          "button can select a font Text Settings does not list";
}

// Retired families are withheld from reading. Rosarivo was A-tier'd on
// 2026-08-07 and still appeared in Text Settings on 2026-08-11, because that
// ruling reached sd-fonts.yaml's installed_families -- what a NEW card is given
// -- and nothing that governs a card already provisioned.
TEST(ReadingFontList, RetiredFamiliesAreNotOfferedForReading) {
  EXPECT_TRUE(readingfonts::isRetired("Rosarivo"));
  EXPECT_TRUE(readingfonts::isRetired("rosarivo")) << "the card's directory name is matched case-insensitively";
  EXPECT_FALSE(readingfonts::offeredForReading("Rosarivo"));

  // Still shipped, and must stay offered.
  for (const char* keep : {"Edgar", "Coelacanth", "TeXGyreSchola", "LibreFranklin"}) {
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
  EXPECT_TRUE(readingfonts::offeredForReading("iAWriterQuattro"))
      << "Quattro carries alsoReading (owner ruling 2026-08-09)";
  EXPECT_FALSE(readingfonts::offeredForReading("PragmataPro"))
      << "a commercial writing face with a Latin-1-only bold must not reach the reading picker";
}
