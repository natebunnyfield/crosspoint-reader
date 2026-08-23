#include "ReadingFontList.h"

#include "FontDisplayNames.h"
#include "notes/EditorFonts.h"

namespace readingfonts {
namespace {

// Directory names as they appear on the card, which is also what
// SETTINGS.sdFontFamilyName persists.
//
// Rosarivo: A-tier'd 2026-08-07 and taken off every surface, then reported still
// showing on 2026-08-11 — because that ruling reached sd-fonts.yaml's
// installed_families, which governs what a NEW card is given, and nothing that
// governs what the reader does with a card it already has.
//
// QuattrocentoSans was A-tier'd in the same ruling. It is deliberately NOT here:
// the 2026-08-11 instruction named Rosarivo, and withholding a face the owner
// did not ask about would be the same silent removal this file exists to make
// deliberate. Add it when it is ruled on.
constexpr const char* kRetired[] = {"Rosarivo"};

}  // namespace

bool isRetired(const char* family) {
  if (!family) return false;
  for (const char* name : kRetired) {
    if (strcasecmp(family, name) == 0) return true;
  }
  return false;
}

bool offeredForReading(const char* family) {
  if (!family) return false;
  if (editorfonts::isWritingOnlyFamily(family)) return false;
  if (isRetired(family)) return false;
  return true;
}

bool sortsBefore(const char* a, const char* b) {
  const uint16_t ya = FontDisplayNames::earliestYear(a);
  const uint16_t yb = FontDisplayNames::earliestYear(b);
  // Newest lineage first; undated families report 0 and therefore sort LAST,
  // which is the intent -- an unlisted or user-installed face has no date to
  // place it by.
  if (ya != yb) return ya > yb;
  // Ties by display name, so the order is stable run to run rather than
  // depending on the order the card happened to be scanned in.
  return FontDisplayNames::displayName(a) < FontDisplayNames::displayName(b);
}

}  // namespace readingfonts
