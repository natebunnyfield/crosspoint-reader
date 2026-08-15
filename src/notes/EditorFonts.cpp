#include "EditorFonts.h"

#include <strings.h>  // strcasecmp — POSIX, not in <cstring>

#include <algorithm>
#include <cstring>

#include "FontDisplayNames.h"

namespace editorfonts {

size_t indexOfFamily(const char* family) {
  if (family == nullptr) return FAMILY_COUNT;
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    if (strcasecmp(family, FAMILIES[i].family) == 0) return i;
  }
  return FAMILY_COUNT;
}

uint8_t migrateLegacyStoredIndex(uint8_t index) {
  // Positions in the six-row table that shipped between the two 2026-08-15
  // rulings: 0 Quattro, 1 Duo, 2 Mono, 3 Plex, 4 PragmataPro, 5 Nitti. The full
  // reasoning — including why the SIX-row encoding is assumed and what that
  // costs a device still holding a seven-row byte — is in EditorFonts.h.
  //
  // A table rather than arithmetic, deliberately: the previous migration was a
  // subtraction, and a subtraction is what let it apply twice and go unnoticed.
  // This is total, and applying it to its own output is not meaningful, so
  // fromJson gates it on the absence of the name key instead of relying on
  // idempotence.
  static constexpr uint8_t kLegacySixRow[] = {
      0,  // iAWriterQuattro -> iAWriterQuattro
      0,  // iAWriterDuo     -> iAWriterQuattro (same superfamily)
      0,  // iAWriterMono    -> iAWriterQuattro (same superfamily)
      1,  // IBMPlexMono     -> PragmataPro     (nearest surviving mono)
      1,  // PragmataPro     -> PragmataPro
      2,  // NittiTypewriter -> NittiTypewriter
  };
  if (index >= sizeof(kLegacySixRow) / sizeof(kLegacySixRow[0])) return 0;
  return kLegacySixRow[index];
}

const char* selectedFamily(uint8_t index) {
  if (index >= FAMILY_COUNT) return FAMILIES[0].family;
  return FAMILIES[index].family;
}

int builtinFontIdFor(uint8_t index) {
  if (index >= FAMILY_COUNT) return FAMILIES[0].builtinFontId;
  return FAMILIES[index].builtinFontId;
}

int fallbackFontId() {
  // First compiled-in entry, in list order, so this follows the table rather
  // than naming a family twice.
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    if (FAMILIES[i].builtinFontId != 0) return FAMILIES[i].builtinFontId;
  }
  return 0;  // no built-in family at all; caller keeps its own UI-face fallback
}

bool isEditorFamily(const char* family) { return indexOfFamily(family) < FAMILY_COUNT; }

// Families that WERE editor faces and no longer are. They still have recipes in
// sd-fonts.yaml and labels in FontDisplayNames, so a card can carry them —
// and if one does, the reading picker would now offer it, because the filter
// that used to hide it reads FAMILIES and they are no longer in FAMILIES.
//
// Silently growing the reading list by three families is not something the
// removal ruling asked for (docs/sd-card-fonts.md: "Do not install additional
// families anywhere without a new ruling"), so the filter keeps its answer and
// the removal stays a removal rather than a swap. Cheaper than it looks: three
// string literals in flash, consulted only by the reading picker's filter.
inline constexpr const char* FORMER_WRITING_FAMILIES[] = {
    "iAWriterDuo",   // removed 2026-08-15
    "iAWriterMono",  // removed 2026-08-15
    "IBMPlexMono",   // removed 2026-08-15
    "SpaceMono",     // removed 2026-08-15 (earlier ruling the same day)
};

std::vector<uint8_t> displayOrder() {
  std::vector<uint8_t> order(FAMILY_COUNT);
  for (size_t i = 0; i < FAMILY_COUNT; ++i) order[i] = static_cast<uint8_t>(i);
  // The reading picker's comparator, restated (FontSelectionActivity.cpp:136-141):
  // newest earliest (creation) year first, undated families (earliestYear 0)
  // last, ties broken by the row's title.
  //
  // The tie-break reads FAMILIES[].label, not FontDisplayNames::displayName, so
  // it compares the exact string the row DRAWS (see the drawList title lambda in
  // EditorFontSelectionActivity::render). The two tables agree today and a test
  // pins that, but if they ever drifted, sorting on the one the owner cannot see
  // would put the list in an order that looks simply wrong on screen.
  //
  // stable_sort so a pair that tied on both keys would still land in table
  // order rather than at the sort's discretion.
  std::stable_sort(order.begin(), order.end(), [](uint8_t a, uint8_t b) {
    const uint16_t ya = FontDisplayNames::earliestYear(FAMILIES[a].family);
    const uint16_t yb = FontDisplayNames::earliestYear(FAMILIES[b].family);
    if (ya != yb) return ya > yb;
    return std::strcmp(FAMILIES[a].label, FAMILIES[b].label) < 0;
  });
  return order;
}

std::string rowSubtitle(uint8_t storedIndex, bool available, const char* unavailableMarker) {
  if (storedIndex >= FAMILY_COUNT) return "";
  const std::string colophon = FontDisplayNames::subtitle(FAMILIES[storedIndex].family);
  if (available) return colophon;

  const char* marker = unavailableMarker != nullptr ? unavailableMarker : "";
  if (marker[0] == '\0') return colophon;
  if (colophon.empty()) return marker;

  // Marker first, then an em dash — the same pairing the preview pane's label
  // already uses for "<face> — Not on card"
  // (EditorFontSelectionActivity.cpp:237). A middle dot would not do: the
  // colophon spends its one middle dot separating designer from lineage
  // (FontDisplayNames.h:170-183), and a second would read as one more stage.
  return std::string(marker) + " \xE2\x80\x94 " + colophon;
}

bool isWritingOnlyFamily(const char* family) {
  if (family == nullptr) return false;
  if (const size_t i = indexOfFamily(family); i < FAMILY_COUNT) return !FAMILIES[i].alsoReading;
  // Not a current editor face. It may still be a retired one — see
  // FORMER_WRITING_FAMILIES above for why those keep being hidden.
  for (const char* former : FORMER_WRITING_FAMILIES) {
    if (strcasecmp(family, former) == 0) return true;
  }
  return false;
}

int resolve(uint8_t index, const std::function<bool(int)>& isRegistered,
            const std::function<int(const char*)>& sdLookup, int uiFallbackId) {
  // Built-in first -- but only if this build actually compiled it in. A font id
  // is just an int; holding one implies nothing about the family being present.
  if (const int builtin = builtinFontIdFor(index); builtin != 0 && isRegistered && isRegistered(builtin)) {
    return builtin;
  }

  if (sdLookup) {
    if (const int id = sdLookup(selectedFamily(index)); id != 0) return id;
  }

  // A card-only row whose family is not installed degrades to a built-in
  // MONOSPACE face rather than to UI chrome -- again only if it is really there.
  if (const int mono = fallbackFontId(); mono != 0 && isRegistered && isRegistered(mono)) return mono;

  // Reached only by a row whose built-in is not registered in this build
  // (isRegistered returned false) AND the SD card has no matching family. Since
  // all five rows are compiled into every binary as of 2026-08-11 (ruling), this
  // path is effectively dead outside the editor-font tests that stub isRegistered
  // to deny all fonts. The caller's UI face is the last resort then -- the wrong
  // texture for a writing surface, but text on screen instead of a blank page.
  return uiFallbackId;
}

}  // namespace editorfonts
