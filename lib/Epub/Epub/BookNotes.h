#pragma once

#include <MissingGlyphLedger.h>

#include <cstdint>
#include <string>

// What this book is having done to it, in the reader's words.
//
// Owner ruling 2026-08-23: "override book css, but give a verbose note that it
// is happening in the select chapter screen (do this for all concerning book
// specific issues)."
//
// The firmware makes a great many decisions on a book's behalf and tells
// nobody: docs/book-notes-and-sparse-ruby-2026-08-23.md enumerates 102 of them.
// Most are the app's own typography and apply identically to every book --
// those are not notes, they are the design. A NOTE is a decision that is (a) specific to THIS book, (b)
// visible to a reader who knows what the publisher shipped, and (c) already
// discovered by a parse the firmware performs anyway. Everything that fails one
// of those three tests is listed in the doc with the reason it was left out.
//
// COLLECTED AT PARSE TIME, NOT AT SCREEN-OPEN TIME. The chapter list is the
// point of the Select Chapter screen and it must open instantly; nothing here
// is ever recomputed to draw it. Each note is raised where the decision is
// taken, and the accumulated set is persisted next to the book's cache
// (notes.bin) so a warm resume into an already-paginated book still knows.
//
// TWO SCOPES, because they go stale differently:
//
//   Book scope   raised while the OPF, the TOC, the zip directory and the
//                stylesheets are parsed. Independent of font, size and
//                viewport, so it survives everything short of deleting the
//                book's cache directory -- which is where notes.bin lives.
//   Layout scope raised while a chapter is paginated, so it depends on the
//                measure. Stamped with a fingerprint of the ReaderRenderSpec
//                that produced it; a mismatch on load DISCARDS the layout mask
//                rather than showing a stale claim about a font the reader has
//                since changed. It re-accumulates as chapters repaginate.
//
// Layout-scope notes are therefore incomplete by construction: they know only
// about the chapters paginated so far. That is honest and it is cheap, and the
// alternative -- paginating the whole book to fill in a notice screen -- is the
// one thing the design constraint forbids.
namespace booknotes {

// Bit positions in the persisted masks. APPEND ONLY: the values are written to
// notes.bin, so re-pointing one silently changes what a stored note means. The
// file carries a version byte, but a version bump only helps if it happens --
// appending cannot be forgotten the way a bump can.
enum class Note : uint8_t {
  // -- Book scope ---------------------------------------------------------
  Drm = 0,                     // META-INF/encryption.xml present
  EmbeddedFontsIgnored,        // the zip ships font files this firmware never loads
  NoTableOfContents,           // neither nav nor NCX parsed
  TocEntriesUnresolved,        // a contents entry points at no spine item
  SpineEntriesMissing,         // an itemref names a manifest id that does not exist
  NoHyphenationForLanguage,    // dc:language has no pattern trie on this device
  StylesheetPartlyUnderstood,  // selectors or at-rules the CSS engine cannot represent
  StylesheetSkipped,           // a whole stylesheet, or everything past the rule cap
  VerticalWritingIgnored,      // writing-mode: vertical-* asked for and not implemented
  // -- Layout scope -------------------------------------------------------
  AlignmentOverridden,    // the book asked for an alignment and got Justify
  JustificationDemoted,   // a justified block was set ragged: the measure is too narrow
  ImagesDropped,          // an image could not be shown at all
  TablesFlattened,        // a table was reflowed into paragraphs, or a nested one dropped
  PreformattedCollapsed,  // <pre> / white-space:pre lost its line breaks and spacing
  // -- Appended 2026-08-23. Scope is declared by isLayoutScope below, NOT by
  //    position: a book-scope note appended after the layout ones used to
  //    become layout-scope silently, which is a note that quietly deletes
  //    itself the first time the reader changes font.
  TextEncodingUnsupported,  // BOOK: a file declared a character encoding with no table here
  MissingGlyphs,            // LAYOUT: the reading font has no shape for some of this book's characters
  _COUNT
};

// Which notes go stale when the measure changes. An EXPLICIT list, not an
// ordering test: the enum is append-only because its values are persisted, and
// an ordering test forces a book-scope note appended today to be inserted into
// the middle -- re-pointing every layout bit in every notes.bin already on a
// card. Every new note has to name its scope here, which is the one thing a
// compiler will not remind anyone about.
constexpr bool isLayoutScope(const Note n) {
  switch (n) {
    case Note::AlignmentOverridden:
    case Note::JustificationDemoted:
    case Note::ImagesDropped:
    case Note::TablesFlattened:
    case Note::PreformattedCollapsed:
    case Note::MissingGlyphs:
      return true;
    default:
      return false;
  }
}

struct Details {
  // The measure that triggered the demotion, in characters per line. The
  // narrowest one seen, because that is the one worth quoting.
  uint16_t narrowestCharsPerLine = 0;
  uint16_t imagesDropped = 0;
  uint16_t cssRulesDropped = 0;
  // DISTINCT codepoints the reading font could not draw, not occurrences. An
  // occurrence count would change every time a page was repainted; "eleven
  // characters are missing" is both stable and the fact a reader can act on.
  uint16_t missingCodepoints = 0;
  // The encoding a file declared that this firmware has no table for. A NAME,
  // because there is no enumeration of the encodings that do NOT exist here and
  // saying which one is the whole point of the note. Truncated rather than
  // dropped: every real declaration is far shorter than this.
  char unsupportedEncoding[24] = {0};
};

// One book is open at a time and the raise sites are spread across the parser,
// the CSS engine, the OPF reader and the layout engine -- none of which has a
// path to an Epub instance at the moment it decides. A file-scope accessor is
// the cheap seam; the alternative is threading a pointer through eleven call
// stacks for a feature that writes 20 bytes.
// Everything but the two file-touching methods is INLINE here on purpose. The
// raise sites live in ParsedText and Hyphenator, which several host test suites
// compile on their own; a raise that needed a .cpp would drag HalStorage into
// every one of them, and the first symptom would be a link error in a suite
// that has nothing to do with book notes.
class Notes {
 public:
  bool has(const Note n) const { return ((isLayoutScope(n) ? layoutMask : bookMask) & noteBit(n)) != 0; }
  bool any() const { return bookMask != 0 || layoutMask != 0; }
  uint8_t count() const { return popcount32(bookMask) + popcount32(layoutMask); }
  const Details& details() const { return detail; }

  // Raise a note. Idempotent, and free when the note is already up -- these sit
  // inside per-element and per-word loops.
  void raise(const Note n) {
    uint32_t& mask = isLayoutScope(n) ? layoutMask : bookMask;
    if ((mask & noteBit(n)) != 0) return;  // the hot path
    mask |= noteBit(n);
    dirty = true;
  }
  // The narrowest measure seen is the one worth quoting: it is the worst case
  // the reader is actually looking at.
  void raiseWithCharsPerLine(const Note n, const uint16_t charsPerLine) {
    if (charsPerLine > 0 && (detail.narrowestCharsPerLine == 0 || charsPerLine < detail.narrowestCharsPerLine)) {
      detail.narrowestCharsPerLine = charsPerLine;
      dirty = true;
    }
    raise(n);
  }
  void countDroppedImage() {
    if (detail.imagesDropped < UINT16_MAX) {
      detail.imagesDropped++;
      dirty = true;
    }
    raise(Note::ImagesDropped);
  }
  // The FIRST unsupported encoding wins. A book is written in one encoding in
  // practice, and a later chapter that declares a second is the less
  // informative of the two -- the reader has already stopped at the first.
  void raiseUnsupportedEncoding(const char* name) {
    if (name != nullptr && name[0] != '\0' && detail.unsupportedEncoding[0] == '\0') {
      size_t i = 0;
      for (; name[i] != '\0' && i + 1 < sizeof(detail.unsupportedEncoding); ++i) {
        detail.unsupportedEncoding[i] = name[i];
      }
      detail.unsupportedEncoding[i] = '\0';
      dirty = true;
    }
    raise(Note::TextEncodingUnsupported);
  }
  // Distinct codepoints, counted by the caller: the ledger that knows which
  // ones have already been seen lives with the font, not here.
  void setMissingCodepoints(const uint16_t howMany) {
    if (howMany == 0) return;
    if (howMany != detail.missingCodepoints) {
      detail.missingCodepoints = howMany;
      dirty = true;
    }
    raise(Note::MissingGlyphs);
  }
  void countDroppedCssRules(const uint16_t howMany) {
    if (howMany == 0) return;
    const uint32_t sum = static_cast<uint32_t>(detail.cssRulesDropped) + howMany;
    detail.cssRulesDropped = sum > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(sum);
    dirty = true;
    raise(Note::StylesheetPartlyUnderstood);
  }

  // Point at a book's cache directory and load notes.bin if present. Clears
  // everything first, so opening a second book cannot inherit the first book's
  // notes. The stored layout fingerprint comes along unjudged -- the reader
  // decides whether it is still current, via setLayoutFingerprint below.
  void openBook(const std::string& cacheDir);
  // The reader's resolved spec, once per render pass. A change DISCARDS the
  // layout mask: every note in it was a claim about a measure that no longer
  // exists, and the sections are about to repaginate for the same reason.
  void setLayoutFingerprint(const uint32_t newFingerprint) {
    // Called from the reader's per-render path, so the equal case must cost a
    // compare and nothing else.
    if (newFingerprint == fingerprint) return;
    fingerprint = newFingerprint;
    layoutMask = 0;
    detail.narrowestCharsPerLine = 0;
    detail.imagesDropped = 0;
    // A different reading font has a different set of holes in it, so the old
    // count is not merely stale, it is about a face that is no longer on screen.
    // The ledger's lifetime is owned HERE, next to the figure it feeds, so
    // there is only one place that can forget to clear it.
    detail.missingCodepoints = 0;
    missingglyphs::current().reset();
    dirty = true;
  }
  // Write notes.bin if anything changed since the last write. Cheap no-op
  // otherwise, so it can sit at the end of a section build.
  void flush();

  // Test seam. Not for firmware use.
  void resetForTest() {
    clear();
    cachePath.clear();
    fingerprint = 0;
  }

 private:
  // NOT named `bit`: Arduino.h defines that as a macro, and this header is
  // included from translation units compiled behind it.
  static uint32_t noteBit(const Note n) { return 1u << static_cast<uint8_t>(n); }
  static uint8_t popcount32(uint32_t v) {
    uint8_t n = 0;
    while (v) {
      v &= v - 1;
      n++;
    }
    return n;
  }
  void clear() {
    bookMask = 0;
    layoutMask = 0;
    detail = Details{};
    dirty = false;
    missingglyphs::current().reset();
  }

  uint32_t bookMask = 0;
  uint32_t layoutMask = 0;
  uint32_t fingerprint = 0;
  Details detail;
  std::string cachePath;  // empty means "nothing to persist to"
  bool dirty = false;
};

inline Notes& current() {
  static Notes instance;
  return instance;
}

static_assert(static_cast<uint8_t>(Note::_COUNT) <= 32, "the masks are uint32_t");

// No user-facing text lives here. Nothing under lib/ calls tr(), and these
// notes are read aloud to a person -- the note id to StrId mapping is in
// src/activities/reader/BookNotesActivity.cpp, on the side of the build that
// owns I18n.

}  // namespace booknotes
