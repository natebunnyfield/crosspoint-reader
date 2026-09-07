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
  // THREE KEYS, newest first at every level. Owner ruling 2026-09-06: *"sort by
  // origin year, secondary sort by next year, tertiary by next year."*
  //
  //   1. ORIGIN     the year on the line the picker now draws at the top of a
  //                 font's info -- the original type, not the digitisation.
  //                 Coelacanth sorts on Jenson's c. 1470, not Rogers' 1914.
  //   2. STAGE 1    the family's own first lineage stage.
  //   3. STAGE 2    its second, if it has one.
  //
  // The sort was on stage 1 alone until this ruling, which put the list in a
  // visibly odd order once the origin line shipped: Coelacanth showed
  // "c. 1470 Venice" four rows ABOVE Inknut's "1469 Venice". Ordering on the
  // line the reader can see is the fix.
  //
  // THIS COMPARATOR IS SHARED WITH THE IN-BOOK FONT CYCLE (the next/previous
  // family gesture reads the same order), so the cycle follows the list. That
  // is deliberate: two orders for one set of fonts is worse than either.
  //
  // Undated families report 0 at every level and therefore sort LAST, which is
  // the intent -- an unlisted or user-installed face has no date to place it
  // by, and it must not land in the middle of the dated ones.
  const uint16_t oa = FontDisplayNames::originYear(a);
  const uint16_t ob = FontDisplayNames::originYear(b);
  if (oa != ob) return oa > ob;
  const uint16_t s1a = FontDisplayNames::lineageStageYear(a, 0);
  const uint16_t s1b = FontDisplayNames::lineageStageYear(b, 0);
  if (s1a != s1b) return s1a > s1b;
  const uint16_t s2a = FontDisplayNames::lineageStageYear(a, 1);
  const uint16_t s2b = FontDisplayNames::lineageStageYear(b, 1);
  if (s2a != s2b) return s2a > s2b;
  // Ties by display name, so the order is stable run to run rather than
  // depending on the order the card happened to be scanned in.
  return FontDisplayNames::displayName(a) < FontDisplayNames::displayName(b);
}

}  // namespace readingfonts
