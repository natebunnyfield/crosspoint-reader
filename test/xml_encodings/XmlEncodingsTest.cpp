// The code-page tables behind expat's unknown-encoding handler (sweep item
// #52). Everything here fails SILENTLY in the field: a wrong table parses the
// chapter and sets every accented word to a different word, a missing alias
// sends a perfectly ordinary book down the "cannot be decoded" path, and a low
// half that is not ASCII is a table expat refuses to install at all -- which
// looks exactly like having no handler.
//
// The expected values are not re-derived here from another table; they are
// SPOT VALUES taken from the published code pages, chosen at the places where
// two plausible tables differ. windows-1252 against ISO-8859-1 is the whole
// point of the feature and gets the most of them.
#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

#include "XmlEncodingTables.h"

namespace {

const uint16_t* table(const char* name) { return xmlencodings::highHalfFor(name); }

// The tables store bytes 0x80..0xFF only.
uint16_t at(const uint16_t* t, const unsigned byte) { return t[byte - 0x80]; }

std::string normalised(const char* name) {
  char out[48];
  xmlencodings::normaliseEncodingName(name, out, sizeof(out));
  return out;
}

}  // namespace

TEST(XmlEncodings, Windows1252CarriesTheBandThatDistinguishesItFromLatin1) {
  const uint16_t* t = table("windows-1252");
  ASSERT_NE(t, nullptr);
  // 0x80..0x9F is C1 control space in ISO-8859-1 and typography in
  // windows-1252. If a build ever quietly serves latin-1 for this name, these
  // are the only assertions that would notice.
  EXPECT_EQ(at(t, 0x80), 0x20ACu);  // euro
  EXPECT_EQ(at(t, 0x91), 0x2018u);  // left single quote
  EXPECT_EQ(at(t, 0x92), 0x2019u);  // right single quote (the apostrophe in real prose)
  EXPECT_EQ(at(t, 0x93), 0x201Cu);  // left double quote
  EXPECT_EQ(at(t, 0x94), 0x201Du);  // right double quote
  EXPECT_EQ(at(t, 0x96), 0x2013u);  // en dash
  EXPECT_EQ(at(t, 0x97), 0x2014u);  // em dash
  EXPECT_EQ(at(t, 0x85), 0x2026u);  // ellipsis
  // The half it shares with latin-1 must still be latin-1.
  EXPECT_EQ(at(t, 0xE9), 0x00E9u);  // e acute
  EXPECT_EQ(at(t, 0xEF), 0x00EFu);  // i diaeresis
  EXPECT_EQ(at(t, 0xA3), 0x00A3u);  // pound sign
}

TEST(XmlEncodings, UndefinedBytesBecomeTheReplacementCharacterAndNotAFailure) {
  const uint16_t* t = table("windows-1252");
  ASSERT_NE(t, nullptr);
  // 0x81, 0x8D, 0x8F, 0x90 and 0x9D are undefined in windows-1252. Mapping them
  // to -1 (expat's "illegal byte") would fail the WHOLE CHAPTER over one stray
  // byte, which is the outcome this feature exists to remove.
  for (const unsigned byte : {0x81u, 0x8Du, 0x8Fu, 0x90u, 0x9Du}) {
    EXPECT_EQ(at(t, byte), 0xFFFDu) << "byte " << std::hex << byte;
  }
}

TEST(XmlEncodings, CyrillicAndGreekAreNotLatin1InDisguise) {
  const uint16_t* cp1251 = table("windows-1251");
  ASSERT_NE(cp1251, nullptr);
  EXPECT_EQ(at(cp1251, 0xC0), 0x0410u);  // CYRILLIC CAPITAL A
  EXPECT_EQ(at(cp1251, 0xFF), 0x044Fu);  // CYRILLIC SMALL YA

  const uint16_t* iso5 = table("iso-8859-5");
  ASSERT_NE(iso5, nullptr);
  EXPECT_EQ(at(iso5, 0xB0), 0x0410u);  // the SAME letter at a different byte

  const uint16_t* iso7 = table("iso-8859-7");
  ASSERT_NE(iso7, nullptr);
  EXPECT_EQ(at(iso7, 0xE1), 0x03B1u);  // GREEK SMALL ALPHA

  const uint16_t* koi8 = table("koi8-r");
  ASSERT_NE(koi8, nullptr);
  EXPECT_EQ(at(koi8, 0xC1), 0x0430u);  // KOI8 puts Cyrillic in its own order entirely
}

TEST(XmlEncodings, Latin9DiffersFromLatin1AtExactlyEightBytes) {
  // iso-8859-15 is iso-8859-1 with eight substitutions, the euro among them.
  // A build that served latin-1 for this name would be right 248 times out of
  // 256, which is precisely why it needs its own row.
  const uint16_t* t = table("iso-8859-15");
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(at(t, 0xA4), 0x20ACu);  // euro, where latin-1 has the currency sign
  EXPECT_EQ(at(t, 0xA6), 0x0160u);  // S caron
  EXPECT_EQ(at(t, 0xBD), 0x0153u);  // oe ligature
  EXPECT_EQ(at(t, 0xE9), 0x00E9u);  // unchanged where it is unchanged
}

TEST(XmlEncodings, TheNameIsMatchedAfterNormalisation) {
  EXPECT_EQ(normalised("Windows-1252"), "windows1252");
  EXPECT_EQ(normalised("WINDOWS_1252"), "windows1252");
  EXPECT_EQ(normalised("  iso-8859-2 "), "iso88592");
  EXPECT_EQ(normalised(nullptr), "");
  EXPECT_EQ(normalised(""), "");

  // Every spelling a real declaration uses lands on the same table.
  const uint16_t* canonical = table("windows-1252");
  ASSERT_NE(canonical, nullptr);
  for (const char* spelling : {"Windows-1252", "WINDOWS-1252", "cp1252", "CP1252", "windows_1252", "x-cp1252"}) {
    EXPECT_EQ(table(spelling), canonical) << spelling;
  }
}

TEST(XmlEncodings, NormalisationTruncatesRatherThanOverruns) {
  char out[8];
  std::memset(out, 'X', sizeof(out));
  xmlencodings::normaliseEncodingName("windows-1252-and-then-some", out, sizeof(out));
  EXPECT_STREQ(out, "windows");  // 7 characters plus the terminator

  // outSize 0 must write nothing at all: the name comes off a book, so the
  // caller's buffer is the only thing standing between a hostile declaration
  // and the stack.
  char none[1] = {'Z'};
  xmlencodings::normaliseEncodingName("windows-1252", none, 0);
  EXPECT_EQ(none[0], 'Z');
}

TEST(XmlEncodings, WhatIsNotCarriedIsRefusedRatherThanGuessedAt) {
  // The multi-byte encodings, which is the deliberate omission, and the merely
  // unknown. Both must answer nullptr so the handler raises the book note
  // instead of installing a table that would decode the file into nonsense.
  for (const char* name : {"shift_jis", "Shift-JIS", "euc-jp", "big5", "gb2312", "gbk", "gb18030", "euc-kr",
                           "iso-2022-jp", "utf-7", "not-an-encoding", ""}) {
    EXPECT_EQ(table(name), nullptr) << name;
  }
  EXPECT_EQ(table(nullptr), nullptr);
}

TEST(XmlEncodings, ExpatsOwnEncodingsAreNotShadowed) {
  // Expat decodes these four itself and never calls the handler for them. A
  // table here would be dead weight at best; at worst it is a second opinion
  // about UTF-8, and the two could disagree.
  for (const char* name : {"utf-8", "UTF-8", "utf-16", "us-ascii", "iso-8859-1", "latin1"}) {
    EXPECT_EQ(table(name), nullptr) << name;
  }
}

TEST(XmlEncodings, NoTableCanBeOneExpatWouldRefuse) {
  // XmlInitUnknownEncoding (xmltok.c) walks the table and returns 0 -- no
  // handler at all -- if any entry is outside the BMP, or if a byte below 0x80
  // is not the identity. The generator enforces both; this is the assertion
  // that the SHIPPED file still satisfies them, since the generator can be
  // bypassed by editing the .cpp.
  //
  // The high half is all this stores, so the low half is identity by
  // construction. What can still go wrong is a value expat cannot express.
  for (const char* name :
       {"windows-1250", "windows-1251", "windows-1252",   "windows-1253", "windows-1254", "windows-1255",
        "windows-1256", "windows-1257", "windows-1258",   "windows-874",  "iso-8859-2",   "iso-8859-3",
        "iso-8859-4",   "iso-8859-5",   "iso-8859-6",     "iso-8859-7",   "iso-8859-8",   "iso-8859-9",
        "iso-8859-10",  "iso-8859-13",  "iso-8859-14",    "iso-8859-15",  "iso-8859-16",  "koi8-r",
        "koi8-u",       "macintosh",    "x-mac-cyrillic", "x-mac-greek",  "x-mac-ce"}) {
    const uint16_t* t = table(name);
    ASSERT_NE(t, nullptr) << name;
    for (unsigned byte = 0x80; byte <= 0xFF; ++byte) {
      const uint16_t cp = at(t, byte);
      // uint16_t cannot hold anything above the BMP, which is the point of the
      // storage choice; what it CAN hold and expat cannot accept is a
      // non-character.
      EXPECT_NE(cp, 0xFFFEu) << name << " byte " << std::hex << byte;
      EXPECT_NE(cp, 0xFFFFu) << name << " byte " << std::hex << byte;
    }
  }
}

TEST(XmlEncodings, EveryCodePageIsDistinctFromEveryOther) {
  // Two rows pointing at the same 128 numbers means one of them is a
  // copy-paste, and the symptom is a whole language decoded as another.
  static constexpr const char* kNames[] = {
      "windows-1250", "windows-1251", "windows-1252",   "windows-1253", "windows-1254", "windows-1255",
      "windows-1256", "windows-1257", "windows-1258",   "windows-874",  "iso-8859-2",   "iso-8859-3",
      "iso-8859-4",   "iso-8859-5",   "iso-8859-6",     "iso-8859-7",   "iso-8859-8",   "iso-8859-9",
      "iso-8859-10",  "iso-8859-13",  "iso-8859-14",    "iso-8859-15",  "iso-8859-16",  "koi8-r",
      "koi8-u",       "macintosh",    "x-mac-cyrillic", "x-mac-greek",  "x-mac-ce"};
  const size_t count = sizeof(kNames) / sizeof(kNames[0]);
  EXPECT_EQ(count, xmlencodings::kSingleByteEncodingCount) << "a code page was added without a row here";
  for (size_t i = 0; i < count; ++i) {
    const uint16_t* a = table(kNames[i]);
    ASSERT_NE(a, nullptr) << kNames[i];
    for (size_t j = i + 1; j < count; ++j) {
      const uint16_t* b = table(kNames[j]);
      ASSERT_NE(b, nullptr) << kNames[j];
      EXPECT_NE(0, std::memcmp(a, b, 128 * sizeof(uint16_t))) << kNames[i] << " == " << kNames[j];
    }
  }
}

TEST(XmlEncodings, ARealSentenceRoundTripsThroughTheShippedTable) {
  // The decode expat will perform, done by hand over the same table, on the
  // exact bytes tools/make_encoding_book.py writes into the fixture's
  // windows-1252 chapter. A spot value proves one cell; this proves the thing
  // the feature is for -- that a publisher's paragraph comes out as the
  // paragraph, punctuation and accents together.
  //
  // Bytes below 0x80 are ASCII by construction (the generator refuses a code
  // page where they are not), so the walk mirrors the handler: identity below,
  // table lookup above.
  const uint16_t* t = table("windows-1252");
  ASSERT_NE(t, nullptr);

  // "A publisher\x92s \x93curly quotes\x94 \x97 caf\xE9, na\xEFve, \xA350\x85"
  static const unsigned char kBytes[] = {'A', ' ', 'p',  'u',  'b', 'l',  'i', 's', 'h',  'e', 'r',  0x92,
                                         's', ' ', 0x93, 'c',  'u', 'r',  'l', 'y', ' ',  'q', 'u',  'o',
                                         't', 'e', 's',  0x94, ' ', 0x97, ' ', 'c', 'a',  'f', 0xE9, ',',
                                         ' ', 'n', 'a',  0xEF, 'v', 'e',  ',', ' ', 0xA3, '5', '0',  0x85};
  static const uint32_t kExpected[] = {'A', ' ', 'p',    'u',    'b', 'l',    'i', 's', 'h',    'e', 'r',    0x2019,
                                       's', ' ', 0x201C, 'c',    'u', 'r',    'l', 'y', ' ',    'q', 'u',    'o',
                                       't', 'e', 's',    0x201D, ' ', 0x2014, ' ', 'c', 'a',    'f', 0x00E9, ',',
                                       ' ', 'n', 'a',    0x00EF, 'v', 'e',    ',', ' ', 0x00A3, '5', '0',    0x2026};
  static_assert(sizeof(kBytes) / sizeof(kBytes[0]) == sizeof(kExpected) / sizeof(kExpected[0]),
                "the two sides of the round trip must describe the same text");

  for (size_t i = 0; i < sizeof(kBytes) / sizeof(kBytes[0]); ++i) {
    const unsigned byte = kBytes[i];
    const uint32_t got = byte < 0x80 ? byte : at(t, byte);
    EXPECT_EQ(got, kExpected[i]) << "at byte " << i << " (0x" << std::hex << byte << ")";
  }
}

TEST(XmlEncodings, TheSameBytesReadDifferentlyInADifferentCodePage) {
  // The other half of the round trip, and the reason the declaration matters at
  // all: byte 0xE9 is e-acute in windows-1252, CYRILLIC SHORT I in
  // windows-1251, and GREEK SMALL IOTA in iso-8859-7. Nothing about the bytes
  // says which.
  EXPECT_EQ(at(table("windows-1252"), 0xE9), 0x00E9u);
  EXPECT_EQ(at(table("windows-1251"), 0xE9), 0x0439u);
  EXPECT_EQ(at(table("iso-8859-7"), 0xE9), 0x03B9u);
}
