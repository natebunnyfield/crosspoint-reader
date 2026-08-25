#pragma once

#include <cstdint>
#include <string>

#include "EpdFontData.h"

// Per-ligature control: which of a face's ligature substitutions actually run.
//
// Owner ruling 2026-08-24: *"give a full subpage of Typography Settings that
// gives all available typography options with full granularity, including
// toggling each individual ligature."* The motivating report was Almendra's
// `st` -- *"almendra has at least one distracting ligature (st)"* -- but the
// ask is the instrument, not that one switch.
//
// WHY THIS NEEDS NO FONT REBUILD
//
// Ligature substitution is a RUNTIME lookup, not something baked into the
// glyph run: EpdFont::applyLigatures walks the text and consults the sorted
// EpdLigaturePair table for each adjacent codepoint pair (EpdFont.cpp). So
// switching one off is a decision made at draw time, and neither the .cpfont
// format nor a single byte of any installed family changes.
//
// THE IDENTITY IS THE INPUT PAIR, NOT THE OUTPUT CODEPOINT
//
// This is the load-bearing choice in the file, and getting it the other way
// round would have been silently wrong.
//
// A pair's OUTPUT codepoint is the font's private business. The Unicode
// presentation forms U+FB00..U+FB06 (ff fi fl ffi ffl st st) are shared, but
// everything else a face carries lands in the Private Use Area: Edgar ships
// nine PUA ligatures at U+E000..U+E008 (fb fh fj fk gy plus the four ff-
// prefixed ones), and U+E000 means `fb` only because Edgar's converter said
// so. Another family's U+E000 is a different shape entirely.
//
// The INPUT pair does not have that problem. `st` is U+0073 U+0074 in every
// face that has ever been cut, so a preference expressed that way survives a
// family switch, a font rebuild, and a card moved between devices -- which is
// exactly what a stored preference has to do. Almendra's own table proves the
// output side is not even self-consistent: its `fh` ligature is emitted as
// U+FB00, the codepoint that means `ff` everywhere else.
//
// WHAT THE SPEC STRING IS
//
// A comma-separated list of pairs, each written as its two codepoints in
// UTF-8: `"st"` is the pair s+t, `"fh"` is f+h, and a CHAINED ligature -- one
// whose left side is itself a ligature -- is written with that ligature
// character, so Edgar's ffi (U+FB00 + i) is `"<ff>i"`. Two codepoints per
// token, always; no separator inside a token, and no comma appears in any
// pair, so the format needs no escaping.
//
// It is stored as text rather than as a bitmask over a fixed enumeration
// because the set of ligatures is PER FAMILY and open-ended -- Edgar's nine
// PUA pairs are not Almendra's four -- so there is no fixed enumeration to
// index. It is legible in settings.json for the same reason ownerName is:
// a card is hand-editable, and `"st"` says what it means where `0x00730074`
// does not.
//
// CANONICAL FORM
//
// Parsing sorts and dedupes, so `"st,fh"` and `"fh,st,st"` are the same
// preference and produce the same fingerprint(). That matters: the fingerprint
// is a section-cache key (ReaderRenderSpec::ligatureFingerprint), so if
// re-ordering the string changed it, a hand edit that meant nothing would
// repaginate every book on the card.
namespace ligatures {

// Ceiling on how many pairs one preference can suppress. Edgar carries 14 pairs
// across its four styles and is the richest family on this card, so 24 leaves
// room to switch every one of them off in a family half again as generous. The
// cap bounds the resident array below and the settings field that persists it;
// specWith() REFUSES a pair past it rather than dropping one silently, because
// a suppression that vanishes reads as a setting that does not stick.
//
// The budget is SHARED ACROSS EVERY FAMILY the card has ever used, not spent
// per family -- that follows directly from the key being family-independent,
// which is the whole point of §2 of docs/ligature-control.md. Two rich families
// with disjoint pair sets are the realistic worst case (Edgar's 14 plus
// Almendra's one non-overlapping `st` is 15), so the headroom is real; but a
// reader who switched off everything in four such families would find the
// twenty-fifth toggle silently doing nothing. If that ever becomes reachable,
// raise this and SPEC_BUF_SIZE together -- the test that sweeps the ceiling
// checks the pair, so it will follow.
constexpr size_t MAX_SUPPRESSED = 24;

// Bytes the canonical spec can occupy, terminator included. A pair costs at
// most three bytes per codepoint plus a comma; the presentation forms and the
// PUA both sit in the BMP, so three is the true worst case and 24 pairs fit in
// 24 * 7 = 168. CrossPointSettings sizes its field from this.
constexpr size_t SPEC_BUF_SIZE = 176;

/// Packs a pair the way EpdLigaturePair::pair does, so the two can be compared
/// without either side re-deriving the encoding.
constexpr uint32_t packPair(const uint32_t leftCp, const uint32_t rightCp) {
  return (leftCp << 16) | (rightCp & 0xFFFFu);
}

/// PURE. Does `spec` suppress this pair? A null or empty spec suppresses
/// nothing. Malformed tokens (one codepoint, three codepoints, stray commas)
/// are skipped rather than poisoning the whole spec: the string is reachable
/// through a hand-edited settings.json and the web settings API, and one bad
/// token must not silently turn every ligature back on.
bool specSuppresses(const char* spec, uint32_t leftCp, uint32_t rightCp);

/// PURE. `spec` with the pair suppressed (`suppress`) or allowed again,
/// returned in canonical form. Returns the canonicalized spec unchanged when
/// adding would exceed MAX_SUPPRESSED.
std::string specWith(const char* spec, uint32_t leftCp, uint32_t rightCp, bool suppress);

/// PURE. Canonical form of `spec`: parsed, sorted, deduped, malformed tokens
/// dropped. Round-tripping a stored value through this is what keeps the
/// fingerprint stable across a hand edit that changed only the order.
std::string canonicalize(const char* spec);

/// PURE. A 32-bit identity for the whole preference, for the section cache.
/// `enabled == false` is deliberately NOT the same as an empty spec: with
/// ligatures off the page is laid out differently from a page with all of them
/// on, so the two must not compare equal.
uint32_t fingerprint(bool enabled, const char* spec);

/// PURE. What a pair actually spells, expanding a left side that is itself a
/// ligature by walking back through the same table: Edgar's U+FB00 + `i`
/// spells "ffi" rather than "<ff>i". Used for the row labels, and pure so the
/// expansion can be tested without a font on the card.
///
/// Depth-bounded at four expansions. A malformed table could otherwise name a
/// ligature as its own ancestor and spin forever; four covers every real
/// chain (the longest anyone cuts is ffl, two deep).
std::string spellPair(const EpdLigaturePair* pairs, uint32_t pairCount, uint32_t leftCp, uint32_t rightCp);

// --- The live preference, as the font layer sees it -------------------------
//
// EpdFont sits under CrossPointSettings and cannot include it, so the
// preference is PUSHED down here rather than pulled up there. Same shape as
// MissingGlyphLedger, one library over, and for the same reason.
//
// The DEFAULT STATE IS "everything allowed", which is exactly a fresh
// install's defaults (enabled, empty spec). That is not a convenience: a
// factory-fresh device never runs CrossPointSettings::fromJson() at all
// (PersistableStore returns early when there is no file), so anything whose
// correctness depended on configure() having been called would have been
// wrong on precisely the devices nobody tests on.

/// Re-reads the preference. Called from CrossPointSettings::fromJson() and
/// from the Typography screen when a row moves -- the two places the value can
/// change. Parses once and keeps a resident sorted array, because allowed() is
/// on the glyph path.
void configure(bool enabled, const char* spec);

/// Whether any substitution runs at all.
bool enabled();

/// Whether THIS pair may substitute. Called only after the font has already
/// said it has a ligature for the pair, so it is off the hot path for ordinary
/// text; the sorted-array scan below is bounded by MAX_SUPPRESSED regardless.
bool allowed(uint32_t leftCp, uint32_t rightCp);

}  // namespace ligatures
