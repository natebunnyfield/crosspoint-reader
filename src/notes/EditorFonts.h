// Owner ruling 2026-08-05: the EDITOR font group is its own list, chosen by a
// CrossPoint setting under Text Settings. These are writing faces, NOT reading
// faces — they do not join the six-family reading S tier, and a card carrying
// them is not thereby carrying a seventh reading family.
//
// The persisted form is the family NAME, not this list's order (2026-08-15,
// see migrateLegacyStoredIndex below). Recipes live in
// lib/EpdFont/scripts/sd-fonts.yaml under "Editor group".
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <string>
#include <vector>

#include "fontIds.h"

namespace editorfonts {

struct Entry {
  const char* family;  // sd-fonts.yaml family name == on-card directory name
  const char* label;   // picker label
  // Non-zero when the face is COMPILED IN; the card is then never consulted.
  // Zero means card-only: the family must be present in /fonts or /.fonts, or
  // the editor falls back to the UI face.
  int builtinFontId;
  // Also offered as a READING face in Text Settings (owner ruling
  // 2026-08-09). The ruling at the top of this file -- writing faces are not
  // reading faces -- still holds for the rest; Quattro is the exception the
  // owner asked for, and a flag on the row is how an exception stays visible
  // instead of becoming a special case buried in the filter.
  bool alsoReading;
};

// THREE faces, by owner ruling 2026-08-15: iA Writer Quattro, PragmataPro,
// Nitti Typewriter. Quattro is OFL and always compiled in; the other two are
// commercial and documented at their own entries below.
//
// Every row is BUILT IN (monos: owner ruling 2026-08-06; iA faces: owner
// ruling 2026-08-11). Before 2026-08-06 every entry was card-only, and since
// no card carried any of them this setting did nothing whatsoever:
// resolveEditorFont() got 0 back from the SD resolver and fell through to the
// 10 pt UI face no matter which row you picked.
//
// Flash cost of the survivors: ~72 KB for iA Writer Quattro plus whatever the
// two commercial cuts weigh where their TTFs are present (all at 12 pt, four
// styles each, 2-bit compressed). The _2x companions are dead flash on device
// where RENDER_SCALE=1 and are stripped by --gc-sections; they ship only in
// the simulator/iOS binaries that run at RENDER_SCALE=2.
//
// FOUR rows have been removed, in two rulings:
//   2026-08-15  Space Mono, from index 3 of a seven-row table.
//   2026-08-15  iA Writer Duo, iA Writer Mono and IBM Plex Mono, from indices
//               1, 2 and 3 of the six-row table the first ruling left behind.
//
// The first removal was absorbed by shifting the stored POSITION, and that was
// a mistake: the byte on disk carried no record of which table it indexed, so
// the shift re-applied on every load and walked a saved value down the list one
// row per boot (pick PragmataPro, get IBM Plex Mono two reboots later). The
// second removal does not repeat it. SETTINGS.editorFont is now persisted by
// FAMILY NAME under the settings key "editorFontFamily", exactly as
// sdFontFamilyName and language already are, so no future removal or reorder
// can re-point a saved value at a different face and this list is no longer
// order-frozen. See migrateLegacyStoredIndex() below for the one-shot rescue of
// the position-encoded bytes that already exist.
//
// What "keep in the repo" means for a removed family: the recipe stays in
// sd-fonts.yaml, the picker label stays in FontDisplayNames.h, and the
// generated builtinFonts/*.h stay tracked. Only the wiring goes — the row, the
// id, the font objects in main.cpp, and the all.h includes — so the family
// costs no flash while remaining one commit away from returning.
inline constexpr Entry FAMILIES[] = {
    {"iAWriterQuattro", "iA Writer Quattro", IAWRITERQUATTRO_12_FONT_ID, /*alsoReading=*/true},
    // COMMERCIAL, and the only row whose glyph tables are not in this repo.
    // builtinFonts/pragmatapro_*.h are gitignored (see .gitignore) and built
    // locally from lib/EpdFont/local_fonts/, so a clone without the licensed
    // TTFs compiles fine and this row simply is not registered — resolve()
    // degrades it to a built-in mono and the picker marks it unreachable.
    // That is the SAME mechanism a card-only row uses, reached from the other
    // direction, which is why no new branch was needed anywhere.
    //
    // Coverage is ASYMMETRIC across the styles, unlike every other row here.
    // The Regular carries 950 of the 1665 codepoints the converter asks for
    // (more than SpaceMono's 539 or IBM Plex Mono's 732); the Bold, Italic and
    // BoldItalic carry 234 — full ASCII and full Latin-1 plus the smart quotes,
    // dashes and ellipsis, but no Latin Extended-A. So an emphasised word
    // containing e.g. "ż" or "ř" renders '?' where the roman would render the
    // letter. EpdFont::getGlyph substitutes U+FFFD then '?', so the metrics stay
    // honest and nothing slides — it degrades, it does not corrupt the line.
    // Accepted for a Latin-1 writing face; do NOT promote this row to
    // alsoReading, where the gap would meet arbitrary book text.
    {"PragmataPro", "PragmataPro", PRAGMATAPRO_12_FONT_ID, false},
    // COMMERCIAL, same pattern as PragmataPro. builtinFonts/nitti*.h are
    // gitignored (see .gitignore) and built locally from
    // lib/EpdFont/local_fonts/NittiTypewriter-Regular.ttf. UNLIKE PragmataPro,
    // only Regular exists; Bold/Italic/BoldItalic are synthesised at build time
    // via fontconvert.py's --synth-embolden-em 0.045 --synth-slant-deg 11.
    // Coverage across all four styles is symmetric — the synthesiser produces
    // the same codepoint set from the one Regular source — unlike PragmataPro
    // whose Bold/Italic cover only ASCII+Latin-1. Writing-only: the synthetic
    // bold's 'a' counter is narrow (5 px 2-bit white interior, ≥1 ✓) but a
    // typewriter face at 5 px width is not suited to arbitrary book text.
    {"NittiTypewriter", "Nitti Typewriter", NITTITYPEWRITER_12_FONT_ID, false},
};
inline constexpr size_t FAMILY_COUNT = sizeof(FAMILIES) / sizeof(FAMILIES[0]);

// Table position for a persisted family NAME, or FAMILY_COUNT when the name is
// not one of ours (a family since removed, or a typo from the web API). The
// caller decides what "not ours" means; this does not guess.
size_t indexOfFamily(const char* family);

// ONE-SHOT rescue of a settings.json written before the name key existed.
//
// The input is a POSITION in the six-row table that shipped between the two
// 2026-08-15 rulings — 0 Quattro, 1 Duo, 2 Mono, 3 Plex, 4 PragmataPro,
// 5 Nitti — and the output is a position in FAMILIES above. Three rows map
// exactly; the three removed families have to land somewhere:
//
//   0 iAWriterQuattro -> 0 iAWriterQuattro   exact
//   1 iAWriterDuo     -> 0 iAWriterQuattro   same superfamily, same designer;
//   2 iAWriterMono    -> 0 iAWriterQuattro     Duo/Mono/Quattro differ only in
//                                              how they space, so the nearest
//                                              surviving face is very near
//   3 IBMPlexMono     -> 1 PragmataPro       both engineered monos; Nitti is a
//                                              typewriter texture and further
//   4 PragmataPro     -> 1 PragmataPro       exact
//   5 NittiTypewriter -> 2 NittiTypewriter   exact
//   >=6               -> 0                   out of range for that table
//
// Why the SIX-row encoding and not the seven-row one that preceded it: the
// six-row table is what main's picker writes (EditorFontSelectionActivity
// stores a live displayOrder() position) and what its toJson persists, so it is
// the encoding of every byte that a build carrying the first ruling has saved.
// A seven-row byte only survives on a device that has not booted since
// 2026-08-15 02:53, and for those the three ambiguous values (4 Plex, 5
// PragmataPro, 6 Nitti) land on PragmataPro, Nitti and the default — all
// surviving writing faces, none of them a reading face. That is the cost of
// disambiguating a byte that never recorded its own encoding, and it is the
// last time it has to be paid.
//
// Applied ONCE, in CrossPointSettings::fromJson, and only when the
// "editorFontFamily" key is ABSENT. Presence of the name is the discriminator
// the old bare byte never had, and fromJson sets needsResave so the name is
// written immediately — after which this function is unreachable for that
// device forever. That is what stops the re-application that walked stored
// values down the list one row per boot under the first ruling.
//
// It must read the RAW doc["editorFont"] byte, never the field: the generic
// ENUM loop in fromJson clamps against the CURRENT FAMILY_COUNT (3) before
// normalizeRetiredSettings ever runs, so by then every value worth migrating
// has already been thrown away and replaced with the default.
//
// Never call this from selectedFamily() or builtinFontIdFor(): those take a
// position in FAMILIES, and the picker hands them positions from
// displayOrder(). Migrating in there would rewrite a live position that was
// never stale. A stored value and a table position are different things that
// share a type, which is exactly the sort of confusion that ships quietly.
uint8_t migrateLegacyStoredIndex(uint8_t index);

// Family name for the current SETTINGS.editorFont, or the first entry when the
// stored index is out of range (a settings.json written before a family was
// removed from the list).
const char* selectedFamily(uint8_t index);

// Compiled-in font id for the current SETTINGS.editorFont, or 0 when that row
// is card-only. Callers try this FIRST, then the SD resolver, then fallback().
int builtinFontIdFor(uint8_t index);

// The last resort, and it is a MONOSPACE face rather than the UI face.
//
// Historically most rows were card-only, index 0 among them, so the shipped
// default resolved to nothing and the editor came up in 10 pt Libre Franklin.
// Falling back to a built-in mono means every row lands on a writing face, and
// an unreachable row degrades to a sibling in the same group instead of to UI
// chrome. It still matters with three rows: iA Writer Quattro is the only one
// whose glyph tables are in this repo, so on a clone without the licensed TTFs
// the other two are unregistered and land here.
//
// Deliberately NOT done by changing the default index: SETTINGS.editorFont is a
// persisted position, every existing device already has 0 stored, and a device
// that stored 0 could not have chosen it -- the row was unreachable in Settings.
// Remapping the stored value would also overwrite a deliberate web-API choice.
// Resolving at read time fixes fresh, upgraded and web-set devices alike and
// touches no persisted state.
int fallbackFontId();

// The whole resolution chain, in one place, with the availability check the
// two activities' copies of it did not have.
//
// builtinFontIdFor() reads a compile-time table, so it hands back
// SPACEMONO_12_FONT_ID whether or not that family was ever registered with the
// renderer. main.cpp registers it inside `#ifndef OMIT_FONTS`, which the iOS
// target defines -- so on that build EVERY row resolved to an id the renderer
// has no glyphs for. The editor drew nothing at all while still saving
// correctly, which is exactly how it was reported: "not displaying text, shows
// one pixel in the upper left, saves fine".
//
// Both lookups are injected rather than reached through globals, so this stays
// testable without a GfxRenderer or a CrossPointSettings -- the editor-font
// suite deliberately links neither.
//   isRegistered : does the renderer actually have glyphs for this font id
//   sdLookup     : resolve a family name to an SD font id at 12 pt, or 0
//   uiFallbackId : the caller's own last resort, used only when nothing resolves
int resolve(uint8_t index, const std::function<bool(int)>& isRegistered,
            const std::function<int(const char*)>& sdLookup, int uiFallbackId);

// Is `family` (an on-card directory name) one of these writing faces?
//
// The reading font picker lists whatever SdCardFontRegistry discovered, so a
// card carrying the editor faces used to grow extra reading families -- exactly
// what the ruling at the top of this file says must not happen. Installing them
// is what surfaces it: build the three iA Writer recipes onto a card and the
// reading picker goes from four families to seven.
//
// Case-insensitive, because the family name comes from a directory on a FAT
// card and the recipe's spelling is not what necessarily lands there.
bool isEditorFamily(const char* family);

// Should the READING picker hide this family? True for editor faces that are
// writing-only. The reading picker asks this, not isEditorFamily, so a face
// offered for both shows up in both.
bool isWritingOnlyFamily(const char* family);

// The order the PICKER lists these in: display position -> stored index.
//
// This was a separate permutation because FAMILIES used to be append-only (its
// index WAS the persisted value). Persistence is by name now, so the table
// could in principle be reordered directly — but the permutation stays: it
// keeps the on-screen order derived from lineage dates rather than from
// whatever order the rows happen to sit in, which is what makes it agree with
// Text Settings automatically. Nothing here moves a stored value.
//
// Owner ruling 2026-08-09: this list presents and sorts IDENTICALLY to Text
// Settings. That is the reading picker's comparator, restated —
// FontSelectionActivity.cpp:136-141 — reverse chronological by each family's
// earliest (creation) year from FontDisplayNames, undated families last, ties
// broken by the row's own displayed title.
//
// It supersedes the previous compiled-in-faces-first grouping, and it does put
// the three card-only iA faces above the two that always work: they are the
// newest lineage (2018) and the sort has one key. rowSubtitle() carries the
// "Not on card" mark that makes that legible, and resolve() still degrades a
// card-only row to a built-in mono, so the ordering costs nothing functional.
//
// Derived from the table rather than written out, so a face added later sorts
// itself instead of leaving a stale list behind.
std::vector<uint8_t> displayOrder();

// The picker's subtitle for one row: the same "Designer · YEAR PLACE" colophon
// the reading picker draws (FontDisplayNames::subtitle), with the availability
// mark PREPENDED when the face cannot be reached.
//
// Prepended, not appended: the theme wraps a subtitle over kColophonLines and
// ellipsizes the overflow (LyraTheme.cpp:275-281 via renderer.wrappedText), so
// a mark on the tail of a two-line colophon is exactly the thing that gets cut.
// The one fact the owner must not miss cannot live where it can be truncated.
//
// `unavailableMarker` is injected rather than read through I18N so this stays
// linkable without the translation tables — the three editor-font suites link
// this file and nothing else. Pass tr(STR_FONT_NOT_ON_CARD).
std::string rowSubtitle(uint8_t storedIndex, bool available, const char* unavailableMarker);

}  // namespace editorfonts
