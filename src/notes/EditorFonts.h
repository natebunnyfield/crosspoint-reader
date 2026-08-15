// Owner ruling 2026-08-05: the EDITOR font group is its own list, chosen by a
// CrossPoint setting under Text Settings. These are writing faces, NOT reading
// faces — they do not join the six-family reading S tier, and a card carrying
// them is not thereby carrying a seventh reading family.
//
// Order is the persisted encoding of SETTINGS.editorFont — APPEND ONLY.
// Recipes live in lib/EpdFont/scripts/sd-fonts.yaml under "Editor group".
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

// The first four are OFL. The iA faces are the "S" (narrow) cuts. PragmataPro
// and Nitti Typewriter are the commercial exceptions and are documented at
// their own entries below.
//
// All four OFL families are BUILT IN (monos: owner ruling 2026-08-06; iA
// faces: owner ruling 2026-08-11). Before 2026-08-06 every entry was
// card-only, and since no card carried any of them this setting did nothing
// whatsoever: resolveEditorFont() got 0 back from the SD resolver and fell
// through to the 10 pt UI face no matter which row you picked.
//
// The "not fetchable by URL" claim that appeared here previously was stale.
// lib/EpdFont/scripts/sd-fonts.yaml has had URL-fetch recipes for all three
// iA Writer families (raw.githubusercontent.com/iaolo/iA-Fonts) since those
// recipes were added — the same mechanism as the Google Fonts pair.
//
// Flash cost: ~41 KB for IBMPlexMono, ~216 KB for the three iA
// families (all at 12 pt, four styles each, 2-bit compressed). The _2x
// companions (~818 KB compressed) are dead flash on device where RENDER_SCALE=1
// and are stripped by --gc-sections; they ship only in the simulator/iOS
// binaries that run at RENDER_SCALE=2.
//
// Rows may only be APPENDED — SETTINGS.editorFont persists this POSITION.
//
// ONE row has ever been removed: Space Mono, which sat at index 3 until
// 2026-08-15 (owner ruling: "remove space mono entirely from app. keep for
// possible future use in repo though"). Removing a row from the middle is
// exactly the hazard this comment warns about, so it is handled rather than
// waved through — editorfonts::migrateStoredIndex() rewrites any value a device
// already persisted, applied once by CrossPointSettings::normalizeRetiredSettings()
// as settings.json is loaded. Nothing else in this list may be deleted without
// extending that function the same way.
//
// What "keep in the repo" means here: the recipe stays in sd-fonts.yaml, the
// picker label stays in FontDisplayNames.h, and the eight generated
// builtinFonts/spacemono_12_*.h stay tracked. Only the wiring is gone — the
// row, the id, the font objects in main.cpp, and the all.h includes — so the
// family costs no flash while remaining one commit away from returning.
inline constexpr Entry FAMILIES[] = {
    {"iAWriterQuattro", "iA Writer Quattro", IAWRITERQUATTRO_12_FONT_ID, /*alsoReading=*/true},
    {"iAWriterDuo", "iA Writer Duo", IAWRITERDUO_12_FONT_ID, false},
    {"iAWriterMono", "iA Writer Mono", IAWRITERMONO_12_FONT_ID, false},
    {"IBMPlexMono", "IBM Plex Mono", IBMPLEXMONO_12_FONT_ID, false},
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

// Rewrite a persisted SETTINGS.editorFont value for the rows that have moved.
//
// Space Mono was index 3; IBM Plex Mono, PragmataPro and Nitti Typewriter each
// shifted down one when it went. So anything above 3 loses one, and a stored 3
// — a device that had Space Mono selected — lands on what is now index 3, IBM
// Plex Mono. That is deliberate rather than a reset to row 0: a Space Mono user
// asked for a grotesque-ish mono, and Plex is the nearest thing left. Values
// below 3 are untouched.
//
// Applied at the SETTINGS boundary, never inside selectedFamily() or
// builtinFontIdFor(): those take a position in FAMILIES, and the picker hands
// them positions from displayOrder(). Migrating in there would rewrite a live
// position that was never stale — picking PragmataPro would select IBM Plex
// Mono. A stored value and a table position are different things that share a
// type, which is exactly the sort of confusion that ships quietly.
uint8_t migrateStoredIndex(uint8_t index);

// Family name for the current SETTINGS.editorFont, or the first entry when the
// stored index is out of range (a settings.json written before a family was
// removed from the list).
const char* selectedFamily(uint8_t index);

// Compiled-in font id for the current SETTINGS.editorFont, or 0 when that row
// is card-only. Callers try this FIRST, then the SD resolver, then fallback().
int builtinFontIdFor(uint8_t index);

// The last resort, and it is a MONOSPACE face rather than the UI face.
//
// Three of the five rows are card-only, index 0 among them, so the shipped
// default resolved to nothing and the editor came up in 10 pt Libre Franklin --
// compiling two faces in fixed the two new rows and left the out-of-the-box
// path exactly as broken as before. Falling back to a built-in mono means every
// row lands on a writing face, and a card-only row degrades to a sibling in the
// same group instead of to UI chrome.
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
// FAMILIES itself is APPEND ONLY (its index IS SETTINGS.editorFont), so the
// on-screen order has to be a separate permutation or nothing can ever be
// reordered again. Nothing here moves a stored value.
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
