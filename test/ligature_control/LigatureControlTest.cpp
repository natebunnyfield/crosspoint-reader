// The per-ligature preference model (lib/EpdFont/LigatureControl.h), owner
// ruling 2026-08-24: "give a full subpage of Typography Settings that gives
// all available typography options with full granularity, including toggling
// each individual ligature."
//
// Everything in this file is a PURE function over a spec string, which is the
// whole reason the model was extracted rather than written as an if-ladder
// inside the Typography screen. Each of its failure modes is silent on a
// device: a spec that stops matching means a suppressed ligature quietly comes
// back; a fingerprint that moves when the preference did not means every book
// on the card repaginates for nothing; a fingerprint that does NOT move when
// the preference did means the setting looks inert until the cache is cleared
// by hand. None of the three announces itself, and only the last would ever be
// reported as a bug.
#include <gtest/gtest.h>

#include "LigatureControl.h"

#include <iterator>
#include <string>

namespace {

constexpr uint32_t CP_F = 0x0066;
constexpr uint32_t CP_H = 0x0068;
constexpr uint32_t CP_I = 0x0069;
constexpr uint32_t CP_L = 0x006C;
constexpr uint32_t CP_S = 0x0073;
constexpr uint32_t CP_T = 0x0074;
constexpr uint32_t CP_FF = 0xFB00;  // the ff ligature, which is also a left-hand INPUT for ffi

// Almendra's regular face, read out of fs_/fonts/Almendra/Almendra_14.cpfont
// on 2026-08-24. Note f+h emitting U+FB00, the codepoint that means `ff`
// everywhere else -- that is the font's own table, not a transcription slip,
// and it is the concrete reason this model keys on the INPUT pair.
const EpdLigaturePair ALMENDRA[] = {
    {(CP_F << 16) | CP_H, 0xFB00},
    {(CP_F << 16) | CP_I, 0xFB01},
    {(CP_F << 16) | CP_L, 0xFB02},
    {(CP_S << 16) | CP_T, 0xFB06},
};

// Edgar's regular face, same dump. The four ff-prefixed rows are CHAINED: their
// left side is U+FB00, which is itself produced by f+f.
const EpdLigaturePair EDGAR[] = {
    {(CP_F << 16) | 0x0062, 0xE000},   // fb
    {(CP_F << 16) | CP_F, 0xFB00},     // ff
    {(CP_F << 16) | CP_H, 0xE005},     // fh
    {(CP_F << 16) | CP_I, 0xFB01},     // fi
    {(CP_F << 16) | CP_L, 0xFB02},     // fl
    {(CP_FF << 16) | 0x0062, 0xE001},  // ffb
    {(CP_FF << 16) | CP_I, 0xFB03},    // ffi
    {(CP_FF << 16) | CP_L, 0xFB04},    // ffl
};

}  // namespace

// --- the spec, as a set -----------------------------------------------------

TEST(LigatureSpec, EmptyAndNullSuppressNothing) {
  EXPECT_FALSE(ligatures::specSuppresses("", CP_S, CP_T));
  EXPECT_FALSE(ligatures::specSuppresses(nullptr, CP_S, CP_T));
}

TEST(LigatureSpec, MatchesTheExactPairAndNoOther) {
  EXPECT_TRUE(ligatures::specSuppresses("st", CP_S, CP_T));
  // Order matters: t+s is not the pair s+t, and a set that confused them would
  // switch off a ligature nobody asked about.
  EXPECT_FALSE(ligatures::specSuppresses("st", CP_T, CP_S));
  EXPECT_FALSE(ligatures::specSuppresses("st", CP_F, CP_I));
}

TEST(LigatureSpec, CarriesSeveralPairs) {
  const char* spec = "fh,st";
  EXPECT_TRUE(ligatures::specSuppresses(spec, CP_F, CP_H));
  EXPECT_TRUE(ligatures::specSuppresses(spec, CP_S, CP_T));
  EXPECT_FALSE(ligatures::specSuppresses(spec, CP_F, CP_I));
}

TEST(LigatureSpec, ALigatureCodepointIsALegalLeftSide) {
  // Edgar's ffi is U+FB00 + i. Round-tripping it through the spec is the whole
  // reason the format is two CODEPOINTS rather than two ASCII letters.
  const std::string spec = ligatures::specWith("", CP_FF, CP_I, true);
  EXPECT_TRUE(ligatures::specSuppresses(spec.c_str(), CP_FF, CP_I));
  EXPECT_FALSE(ligatures::specSuppresses(spec.c_str(), CP_F, CP_I));
}

TEST(LigatureSpec, MalformedTokensAreDroppedAndTheRestSurvives) {
  // A hand-edited settings.json and the web settings API can both write this
  // field. Rejecting the whole spec on one bad token would silently turn every
  // suppressed ligature back on, which is the failure worth avoiding.
  EXPECT_TRUE(ligatures::specSuppresses("f,st", CP_S, CP_T));    // one codepoint
  EXPECT_TRUE(ligatures::specSuppresses("ffi,st", CP_S, CP_T));  // three codepoints
  EXPECT_TRUE(ligatures::specSuppresses(",,st,,", CP_S, CP_T));  // stray commas
  EXPECT_FALSE(ligatures::specSuppresses("f", CP_F, CP_H));
}

// --- canonical form ---------------------------------------------------------

TEST(LigatureSpec, CanonicalFormIsSortedAndDeduped) {
  EXPECT_EQ(ligatures::canonicalize("st,fh"), ligatures::canonicalize("fh,st"));
  EXPECT_EQ(ligatures::canonicalize("st,st,st"), ligatures::canonicalize("st"));
  // f+h sorts below s+t, so the canonical spelling is the ascending one.
  EXPECT_EQ(ligatures::canonicalize("st,fh"), "fh,st");
}

TEST(LigatureSpec, CanonicalizeIsIdempotent) {
  const std::string once = ligatures::canonicalize("st,fh,st");
  EXPECT_EQ(ligatures::canonicalize(once.c_str()), once);
}

// --- editing ----------------------------------------------------------------

TEST(LigatureSpec, AddAndRemoveRoundTrip) {
  const std::string off = ligatures::specWith("", CP_S, CP_T, true);
  EXPECT_TRUE(ligatures::specSuppresses(off.c_str(), CP_S, CP_T));
  const std::string on = ligatures::specWith(off.c_str(), CP_S, CP_T, false);
  EXPECT_FALSE(ligatures::specSuppresses(on.c_str(), CP_S, CP_T));
  EXPECT_EQ(on, "");
}

TEST(LigatureSpec, AddingTwiceIsNotTwoEntries) {
  const std::string once = ligatures::specWith("", CP_S, CP_T, true);
  EXPECT_EQ(ligatures::specWith(once.c_str(), CP_S, CP_T, true), once);
}

TEST(LigatureSpec, RemovingSomethingAbsentIsANoOp) {
  EXPECT_EQ(ligatures::specWith("st", CP_F, CP_I, false), "st");
}

TEST(LigatureSpec, TheCeilingRefusesRatherThanDropping) {
  // A suppression that vanished to make room for a newer one would read as a
  // setting that does not stick, and nothing on the row could report it.
  std::string spec;
  for (size_t i = 0; i < ligatures::MAX_SUPPRESSED; i++) {
    // Distinct synthetic pairs inside the BMP.
    spec = ligatures::specWith(spec.c_str(), 0x0041 + static_cast<uint32_t>(i), 0x0042, true);
  }
  const std::string full = spec;
  const std::string overflowed = ligatures::specWith(full.c_str(), CP_S, CP_T, true);
  EXPECT_EQ(overflowed, full);
  EXPECT_FALSE(ligatures::specSuppresses(overflowed.c_str(), CP_S, CP_T));
  // ...and every earlier entry is still there.
  EXPECT_TRUE(ligatures::specSuppresses(overflowed.c_str(), 0x0041, 0x0042));
}

TEST(LigatureSpec, TheFullSetFitsTheSettingsField) {
  // CrossPointSettings sizes its char[] from SPEC_BUF_SIZE, so the worst case
  // the model will accept has to fit it -- otherwise a spec the model
  // canonicalizes is truncated on the way to the card, and the truncation is
  // invisible until a ligature comes back on its own.
  std::string spec;
  for (size_t i = 0; i < ligatures::MAX_SUPPRESSED; i++) {
    // U+FB00-block inputs on both sides: three UTF-8 bytes each, the true
    // worst case, since ligature inputs are confined to the BMP.
    spec = ligatures::specWith(spec.c_str(), 0xFB00 + static_cast<uint32_t>(i), 0xFB06, true);
  }
  EXPECT_LT(spec.size() + 1, ligatures::SPEC_BUF_SIZE);
}

// --- the fingerprint, which is a section-cache key --------------------------

TEST(LigatureFingerprint, MovesWhenAPairIsSwitchedOff) {
  EXPECT_NE(ligatures::fingerprint(true, ""), ligatures::fingerprint(true, "st"));
}

TEST(LigatureFingerprint, DoesNotMoveWhenOnlyTheSpellingChanged) {
  // The one that costs money if it is wrong: a hand edit that reordered the
  // spec would otherwise repaginate every book on the card.
  EXPECT_EQ(ligatures::fingerprint(true, "st,fh"), ligatures::fingerprint(true, "fh,st"));
  EXPECT_EQ(ligatures::fingerprint(true, "st"), ligatures::fingerprint(true, "st,st"));
}

TEST(LigatureFingerprint, OffIsNotTheSameAsNothingSuppressed) {
  // Different pages: with ligatures off every pair is set as two glyphs, which
  // is not what an empty suppression list produces.
  EXPECT_NE(ligatures::fingerprint(false, ""), ligatures::fingerprint(true, ""));
}

TEST(LigatureFingerprint, WhileOffTheSuppressionListDoesNotMoveIt) {
  // Editing a row that is currently doing nothing must not repaginate.
  EXPECT_EQ(ligatures::fingerprint(false, ""), ligatures::fingerprint(false, "st"));
}

TEST(LigatureFingerprint, IsNeverTheDefaultConstructedZero) {
  // ReaderRenderSpec defaults this field to 0 and compares it field-for-field
  // against the section file. A real preference hashing to 0 would compare
  // equal to a spec nobody filled in.
  EXPECT_NE(ligatures::fingerprint(true, ""), 0u);
  EXPECT_NE(ligatures::fingerprint(false, ""), 0u);
  EXPECT_NE(ligatures::fingerprint(true, "st"), 0u);
}

// --- the live gate the font layer asks ---------------------------------------

TEST(LigatureLive, DefaultsToEverythingAllowed) {
  // A factory-fresh device never runs CrossPointSettings::fromJson(), so
  // nothing configures this. The default has to be correct on its own.
  ligatures::configure(true, "");
  EXPECT_TRUE(ligatures::enabled());
  EXPECT_TRUE(ligatures::allowed(CP_S, CP_T));
}

TEST(LigatureLive, SuppressesOnlyTheConfiguredPair) {
  ligatures::configure(true, "st");
  EXPECT_FALSE(ligatures::allowed(CP_S, CP_T));
  EXPECT_TRUE(ligatures::allowed(CP_F, CP_I));
  ligatures::configure(true, "");
}

TEST(LigatureLive, TheMasterSwitchDominates) {
  ligatures::configure(false, "");
  EXPECT_FALSE(ligatures::enabled());
  EXPECT_FALSE(ligatures::allowed(CP_F, CP_I));
  // ...and turning it back on restores the per-pair choices untouched.
  ligatures::configure(true, "st");
  EXPECT_TRUE(ligatures::allowed(CP_F, CP_I));
  EXPECT_FALSE(ligatures::allowed(CP_S, CP_T));
  ligatures::configure(true, "");
}

TEST(LigatureLive, ReconfiguringReplacesRatherThanAccumulates) {
  ligatures::configure(true, "st");
  ligatures::configure(true, "fi");
  EXPECT_TRUE(ligatures::allowed(CP_S, CP_T));
  EXPECT_FALSE(ligatures::allowed(CP_F, CP_I));
  ligatures::configure(true, "");
}

// --- row labels --------------------------------------------------------------

TEST(LigatureSpelling, APlainPairSpellsItself) {
  EXPECT_EQ(ligatures::spellPair(ALMENDRA, std::size(ALMENDRA), CP_S, CP_T), "st");
  EXPECT_EQ(ligatures::spellPair(ALMENDRA, std::size(ALMENDRA), CP_F, CP_H), "fh");
}

TEST(LigatureSpelling, AChainedPairIsExpandedThroughItsOwnTable) {
  // U+FB00 + i is Edgar's ffi. Labeling that row "ﬀi" would print the very
  // shape the row exists to switch off, and a PUA left side would print
  // nothing at all.
  EXPECT_EQ(ligatures::spellPair(EDGAR, std::size(EDGAR), CP_FF, CP_I), "ffi");
  EXPECT_EQ(ligatures::spellPair(EDGAR, std::size(EDGAR), CP_FF, CP_L), "ffl");
  EXPECT_EQ(ligatures::spellPair(EDGAR, std::size(EDGAR), CP_FF, 0x0062), "ffb");
}

TEST(LigatureSpelling, ExpandsThroughTHISFontsTableAndNotUnicodesMeaning) {
  // The sharp case, and the reason the expansion walks the font's own table
  // rather than decomposing the codepoint. In Edgar, U+FB00 is what f+f
  // produces and spells "ff". In ALMENDRA the very same codepoint is what f+h
  // produces -- so the same left side, in the same position, has to spell
  // "fh" here and "ff" there.
  //
  // Almendra ships no U+FB00 + i rule, so no such row is ever built from it;
  // the pair is asked here because it is the only way to put the two tables'
  // readings of one codepoint side by side.
  EXPECT_EQ(ligatures::spellPair(ALMENDRA, std::size(ALMENDRA), CP_FF, CP_I), "fhi");
  EXPECT_EQ(ligatures::spellPair(EDGAR, std::size(EDGAR), CP_FF, CP_I), "ffi");
}

TEST(LigatureSpelling, AnUnproducedLeftSideIsLeftAsItsOwnCharacter) {
  // A ligature codepoint no rule in this table emits has nothing to expand
  // into, so it stands as itself rather than borrowing a meaning from
  // elsewhere. U+FB06 (st) is such a codepoint in Edgar, which has no st rule.
  EXPECT_EQ(ligatures::spellPair(EDGAR, std::size(EDGAR), 0xFB06, CP_I), "\xEF\xAC\x86i");
}

TEST(LigatureSpelling, AnEmptyTableIsHandled) {
  EXPECT_EQ(ligatures::spellPair(nullptr, 0, CP_S, CP_T), "st");
}
