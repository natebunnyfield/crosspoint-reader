#pragma once
#include <cstdint>

#include "AutoJustify.h"

// The resolved text-rendering configuration a reader hands to the layout
// engine. Section-cache validation keys on ELEVEN of the twelve fields: a
// section file built with a different spec is discarded and rebuilt.
//
// `smallFontId` is the exception, and the claim that this keys on "every
// field" was wrong until 2026-08-23. It is a real layout input -- the rotated
// wide-table cut -- and it is neither written to the section file nor compared
// on load. That is safe today only by accident: SdCardFontSystem::resolveFontId
// ignores its pointSize argument (there is one loaded reader-size font), so
// getSmallestReaderFontId returns on its first iteration and smallFontId is a
// pure function of fontId, which IS compared.
//
// Two things follow, and neither is hypothetical. If the resolver ever gains a
// real second size, this field starts varying independently and the cache goes
// stale silently -- so add it to the file and the comparison in the same change
// that makes it vary. And in the meantime the wide-table feature it exists for
// does nothing at all for an SD family, which is every shipped configuration.
//
// Build one via CrossPointSettings::readerRenderSpec(width, height), which
// fills every field: the settings-derived ones from the store, the viewport
// from the caller. Taking the viewport as arguments is what keeps a spec from
// existing in a half-filled state — the 0 defaults below are a last-resort
// backstop (a 0x0 viewport lays out nothing), not an invitation to omit it.
struct ReaderRenderSpec {
  int fontId = 0;
  // The SMALLEST size the current reading family offers. Used only by the
  // rotated table page (T-021): a wide table is set a size down, because at the
  // reading size it does not fit even turned -- measured, not assumed, on a
  // five-column table whose column floors summed to 899px against 668 available.
  int smallFontId = 0;
  float lineCompression = 1.0f;
  bool extraParagraphSpacing = false;
  uint8_t paragraphAlignment = 0;
  uint16_t viewportWidth = 0;
  uint16_t viewportHeight = 0;
  bool hyphenationEnabled = false;
  bool embeddedStyle = true;
  uint8_t imageRendering = 0;
  bool focusReadingEnabled = false;
  // Line Grid (2026-08-22): every vertical advance the paginator makes rounds
  // UP to a whole line-height. In the spec so a toggle repaginates.
  bool lineGridEnabled = false;
  // Automatic justification threshold, in characters per line (owner ruling
  // 2026-08-24). A block asking to be justified keeps it only when its own
  // measure carries at least this many characters. In the spec -- and in the
  // section file, and in the comparison on load -- because moving it moves line
  // BREAKS, not just painted x: a narrower block stops being justified, and the
  // ragged branch also stops hyphenating lines already past 70% of the measure
  // (ParsedText.cpp's raggedSkipsHyphen). Without it here a threshold change
  // would be accepted against every stale cache on the card and do nothing at
  // all until the book was cleared. Defaults to autojustify::THRESHOLD_CHARS.
  uint8_t justifyThresholdChars = autojustify::THRESHOLD_CHARS;
  // Identity of the per-ligature preference (owner ruling 2026-08-24), as
  // ligatures::fingerprint(enabled, spec) -- see lib/EpdFont/LigatureControl.h.
  // A 32-bit hash rather than the spec string itself, because this struct is
  // written into every section file and compared field by field on load: a
  // variable-length field would change the header's shape, and nothing here
  // needs to READ the preference back, only to notice that it moved.
  //
  // In the spec because a ligature changes a glyph's ADVANCE -- `st` set as
  // one shape is narrower than s followed by t -- so switching one moves every
  // line break after it on the line. Without this field a card full of
  // paginated books would keep its old breaks against a header that compared
  // equal, and the toggle would look inert until the cache was cleared by
  // hand: exactly the failure justifyThresholdChars was added to avoid, one
  // field above.
  //
  // 0 is deliberately not the fingerprint of any real preference, so a
  // default-constructed spec cannot compare equal to a configured one by
  // accident.
  uint32_t ligatureFingerprint = 0;

  // A cheap identity for "the measure this book was laid out to", used by
  // booknotes::Notes to decide whether a stored layout-scope note still
  // describes what the reader is looking at. Not a cache key -- the section
  // file compares every field itself; this only has to CHANGE when the
  // pagination would, so an FNV-1a walk over the fields that move a line break
  // is enough. lineCompression is folded in by its bit pattern because it is
  // the only float.
  [[nodiscard]] uint32_t layoutFingerprint() const {
    uint32_t h = 2166136261u;
    const auto mix = [&h](const uint32_t v) {
      h ^= v;
      h *= 16777619u;
    };
    uint32_t compressionBits = 0;
    static_assert(sizeof(compressionBits) == sizeof(lineCompression), "float32 assumed");
    __builtin_memcpy(&compressionBits, &lineCompression, sizeof(compressionBits));
    mix(static_cast<uint32_t>(fontId));
    mix(compressionBits);
    mix(static_cast<uint32_t>(viewportWidth) << 16 | viewportHeight);
    mix(static_cast<uint32_t>(extraParagraphSpacing) | static_cast<uint32_t>(hyphenationEnabled) << 1 |
        static_cast<uint32_t>(embeddedStyle) << 2 | static_cast<uint32_t>(focusReadingEnabled) << 3 |
        static_cast<uint32_t>(lineGridEnabled) << 4 | static_cast<uint32_t>(paragraphAlignment) << 5 |
        static_cast<uint32_t>(imageRendering) << 13);
    // Mixed on its own rather than packed into the word above: that word is
    // already carrying imageRendering at bit 13 and a byte would collide with
    // it. A note's layout scope has to notice this move for the same reason the
    // section cache does -- the breaks change, so a stored note about "the
    // narrowest paragraph on this page" is describing a page that no longer
    // exists.
    mix(static_cast<uint32_t>(justifyThresholdChars));
    // Already a 32-bit hash, so it goes in whole. Same argument as the
    // threshold above: switching a ligature moves line breaks, so a stored
    // layout-scope note describes a page that no longer exists.
    mix(ligatureFingerprint);
    return h;
  }
};
