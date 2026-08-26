#pragma once

#include <SdCardFontRegistry.h>

#include <cstddef>
#include <cstdint>
#include <vector>

// Reader font size is stored as a SLOT (see CrossPointSettings::fontSizeSlot),
// not an absolute point size. Families ship deliberately harmonized ramps —
// sd-fonts.yaml anchors each ramp on x-height so slot 2 of one family reads as
// the same apparent size as slot 2 of another, even though the point sizes
// differ (Lora 15pt vs Venetian301 19pt). Matching on absolute pt across a
// family switch throws that harmonization away and silently moves the reader
// several steps; the slot is the unit that actually carries meaning.
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
// buildFontSizeSetting.
inline constexpr uint8_t READER_FONT_SLOT_COUNT = 6;

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
