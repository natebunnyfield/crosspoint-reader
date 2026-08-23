#pragma once
#include <cstdint>

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
    return h;
  }
};
