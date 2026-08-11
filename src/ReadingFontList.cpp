#include "ReadingFontList.h"

#include <cstring>

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

}  // namespace readingfonts
