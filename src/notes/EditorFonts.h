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
// is card-only. Callers try this FIRST, then the SD resolver, then the UI face.
int builtinFontIdFor(uint8_t index);

}  // namespace editorfonts
