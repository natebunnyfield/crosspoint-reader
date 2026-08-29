#pragma once

#include <SdCardFontRegistry.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Reader font size is stored as a SLOT (see CrossPointSettings::fontSizeSlot),
// not an absolute point size. Families ship deliberately harmonized ramps, so
// slot 2 of one family is meant to read as the same apparent size as slot 2 of
// another even though the point sizes differ (Lora 15pt vs Venetian301 19pt).
// Matching on absolute pt across a family switch throws that harmonization away
// and silently moves the reader several steps; the slot is the unit that
// actually carries meaning.
//
// WHAT THE RAMPS ARE HARMONIZED ON HAS CHANGED TWICE, and this comment said
// "x-height" through both. It is no longer the whole answer:
//
//   - x-height, 2026-08-02 .. 2026-08-26. Still the anchor for a family with
//     no `scale:`.
//   - ink per character for Almendra alone, 2026-08-26
//     (docs/almendra-size-match-2026-08-26.md).
//   - WORDS PER FULL PAGE for the other seven installed families since
//     2026-08-27 (docs/almendra-anchored-sizing-2026-08-27.md): each carries a
//     family-wide `scale:` fitted so a book is about the same length whichever
//     face renders it, anchored on Almendra.
//
// So a slot no longer promises the same MEASURED letter size across families —
// it promises about the same amount of text on the page. Measured on the
// shipped .cpfonts at slot 3 (M): advanceY runs 35 (Inknut Junicode) to 41
// (Libris ADF), and x-height 13 to 15. Do not "fix" a family back onto the
// x-height table on the strength of that spread; it is the normalisation
// working, and reverting one family's k is the mistake sd-fonts.yaml's
// `scale:` schema warns about by name.
//
// CrossPointSettings::fontPointSize remains as the RESOLVED size for the active
// family, so the render loop still has a plain point size to hand.

// The built-in Libre Franklin Reader family is compiled in at exactly these
// point sizes (see the global font objects in main.cpp). Only the 14 pt cut
// survives OMIT_FONTS; the other five sizes' EpdFont objects sit under
// #ifndef OMIT_FONTS.
//
// XXS (8 pt) and XS (10 pt) were INSERTED at the bottom on 2026-08-26, not
// appended (owner: "cut XS and XXS versions of every s tiers shipping font",
// then the scope ruling that settled how — "the reindexing never matters
// because it is just me using this"). So the ramp stays in size order and
// every stored `fontSizeSlot` shifted meaning by two ONCE, with no migration:
// there is one user, and the cost is that he re-picks his size once. That
// waiver is specific to this fork's population — if it ever gains users, the
// migration in `fontSlotNeedsMigration` is the thing to build first. The
// existing 1.4/1.5 migration machinery is untouched and still serves its own
// cases.
//
// This ramp is NOT the SD families' ramp and never was. Libre Franklin is the
// same face on both sides, so a given POINT SIZE gives the same x-height
// either way — what differs is which point sizes each ramp spends its slots
// on. Measured 2026-08-26, built-in headers and shipped .cpfonts:
//
//   built-in      8 10 12 14 16 18 pt  ->  x-height  9 12 14 16 18 20 px
//   SD Libre Fr.  7  9 10 12 14 16 pt  ->  x-height  8 10 12 14 16 18 px
//
// So slot k reads ONE STEP LARGER on the built-in fallback than on any
// installed family — 2 px of x-height at every slot but the smallest, where
// hinting makes it 1. That offset predates this change (it was already true of
// the original four slots) and is carried down rather than corrected here;
// correcting it would move the size every no-card reader is already reading at.
inline constexpr uint8_t BUILTIN_READER_POINT_SIZES[] = {8, 10, 12, 14, 16, 18};

// Point sizes selectable for the active reader font, ascending: the SD family's
// installed sizes when `sdFamilyName` names one the registry knows, otherwise
// the built-in set. Never returns empty.
std::vector<uint8_t> readerFontPointSizes(const SdCardFontRegistry* registry, const char* sdFamilyName);

// Number of size slots the UI offers (XXS / XS / S / M / L / XL). Families that
// ship a different count fall back to labelling by point size — see
// readerSlotLabel below.
inline constexpr uint8_t READER_FONT_SLOT_COUNT = 6;

// THE slot names, in ONE place. They were written out twice — here's worth of
// them in SettingsList.h and a second copy in FontSelectionActivity.cpp — and
// the two copies did not merely risk drifting, they had already DISAGREED about
// the case that matters: an out-of-range slot. SettingsList clamped it to the
// LAST name; FontSelectionActivity clamped it to index 0, so the largest size on
// the card announced itself as "XXS". Owner report 2026-08-27: "XXL is shown as
// XXS."
inline constexpr const char* READER_SLOT_NAMES[READER_FONT_SLOT_COUNT] = {"XXS", "XS", "S", "M", "L", "XL"};

// Whether a family's installed size count earns the XXS..XL names at all.
//
// A family may ship MORE than six sizes — a user-built family, or a card that
// has lived through a ramp change and kept the file the old ramp vacated
// (nothing on a real SD card prunes; today's Inknut 10 -> 11 pt move leaves a
// 10 pt file behind on every card that already had one). Those extra sizes stay
// REACHABLE on purpose: the stepper clamps to what is installed, so no size a
// user put on the card becomes unselectable. What they must not do is borrow a
// name, because there are only six names and the seventh slot is not "XXL" —
// it has no name at all.
inline constexpr bool readerSlotsAreNamed(size_t installedCount) { return installedCount == READER_FONT_SLOT_COUNT; }

// The label for one slot: "M (14pt)" when the family ships exactly the six the
// names describe, bare "14pt" otherwise.
//
// `slot` is clamped to the LAST installed size, never to the first. That
// direction is the whole bug: a slot past the end is a slot too LARGE, and
// answering it with the smallest name is the one answer that is not merely
// imprecise but backwards.
inline std::string readerSlotLabel(const std::vector<uint8_t>& sizes, uint8_t slot) {
  if (sizes.empty()) return std::string();
  const size_t i = slot < sizes.size() ? slot : sizes.size() - 1;
  const std::string pt = std::to_string(sizes[i]) + "pt";
  if (!readerSlotsAreNamed(sizes.size())) return pt;
  return std::string(READER_SLOT_NAMES[i]) + " (" + pt + ")";
}

// Point size for `slot` within `sizes` (ascending, non-empty); slot is clamped
// into range. Raw-range overload for callers that must not allocate.
uint8_t pointSizeForSlot(const uint8_t* sizes, size_t count, uint8_t slot);

inline uint8_t pointSizeForSlot(const std::vector<uint8_t>& sizes, const uint8_t slot) {
  return sizes.empty() ? 0 : pointSizeForSlot(sizes.data(), sizes.size(), slot);
}

// Index in `sizes` whose point size is nearest `pt`. Used only to migrate a
// pre-slot settings file; ordinary operation never converts pt back to a slot.
uint8_t slotForPointSize(const uint8_t* sizes, size_t count, uint8_t pt);

inline uint8_t slotForPointSize(const std::vector<uint8_t>& sizes, const uint8_t pt) {
  return sizes.empty() ? 0 : slotForPointSize(sizes.data(), sizes.size(), pt);
}

// Closest entry in `sizes` (ascending, `count` > 0) to `pt`; ties resolve to the
// smaller size. Takes a raw range rather than a vector because getReaderFontId()
// runs inside the page render loop and must not allocate.
uint8_t snapToNearestPointSize(const uint8_t* sizes, size_t count, uint8_t pt);

inline uint8_t snapToNearestPointSize(const std::vector<uint8_t>& sizes, const uint8_t pt) {
  return sizes.empty() ? pt : snapToNearestPointSize(sizes.data(), sizes.size(), pt);
}
