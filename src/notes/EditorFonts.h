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
};

// All five are OFL. The iA faces are the "S" (narrow) cuts.
//
// Space Mono and IBM Plex Mono are BUILT IN (owner ruling 2026-08-06). Before
// that every entry was card-only, and since no card carried any of them this
// setting did nothing whatsoever: resolveEditorFont() got 0 back from the SD
// resolver and fell through to the 10 pt UI face no matter which row you
// picked. Compiling the two mono faces in costs ~83 KB of flash (one size, 12
// pt, four styles each) and makes the setting work on a blank card.
//
// The three iA faces stay card-only on purpose. They are not fetchable by URL
// the way the Google Fonts pair is, and keeping the rows listed keeps the
// persisted indices stable — SETTINGS.editorFont stores this POSITION, so
// deleting a row would silently re-point every saved settings.json at a
// different family.
inline constexpr Entry FAMILIES[] = {
    {"iAWriterQuattro", "iA Writer Quattro", 0},
    {"iAWriterDuo", "iA Writer Duo", 0},
    {"iAWriterMono", "iA Writer Mono", 0},
    {"SpaceMono", "Space Mono", SPACEMONO_12_FONT_ID},
    {"IBMPlexMono", "IBM Plex Mono", IBMPLEXMONO_12_FONT_ID},
};
inline constexpr size_t FAMILY_COUNT = sizeof(FAMILIES) / sizeof(FAMILIES[0]);

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
