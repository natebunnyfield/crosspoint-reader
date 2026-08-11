#include "EditorFonts.h"

#include <strings.h>  // strcasecmp — POSIX, not in <cstring>

#include <algorithm>
#include <cstring>

#include "FontDisplayNames.h"

namespace editorfonts {

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

bool isEditorFamily(const char* family) {
  if (family == nullptr) return false;
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    if (strcasecmp(family, FAMILIES[i].family) == 0) return true;
  }
  return false;
}

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
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    if (strcasecmp(family, FAMILIES[i].family) == 0) return !FAMILIES[i].alsoReading;
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

  // Reached only by a genuinely card-only family -- one of the three iA faces,
  // the rows with no built-in form (builtinFontId 0) -- that is also missing from
  // the card, so neither the built-in check nor the SD lookup above could serve
  // it. NOT "OMIT_FONTS builds" wholesale: since 63ea6e6b Space Mono and IBM Plex
  // Mono are compiled into every binary, OMIT_FONTS/iOS included, so a build no
  // longer lacks editor faces outright. The built-in mono fallback on the line
  // above (Space Mono, registered in every build) in fact already catches that
  // iA-on-a-blank-card case, so this final return is effectively dead outside the
  // editor-font tests that stub isRegistered to deny the mono. The caller's UI
  // face is the last resort then -- the wrong texture for a writing surface, but
  // text on screen instead of a blank page.
  return uiFallbackId;
}

}  // namespace editorfonts
