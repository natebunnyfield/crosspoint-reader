#pragma once

#include <cstddef>
#include <cstdint>

// The Update Fonts compare logic, PURE on purpose — no Storage, no network, no
// Arduino — for the same reason LibrarySyncPlan.h is: a wrong verdict here is
// SILENT on device. A family skipped when it should install leaves a stale font
// and reports "unchanged"; a family installed when it need not be costs a
// multi-megabyte download nobody asked for. Neither prints an error.
//
// WHY THIS FILE IS NOT LibrarySyncPlan.h WITH DIFFERENT WORDS.
// Update Library's unit is a FILE. Update Fonts' unit is a FAMILY, and a family
// is six files (Doves_8.cpfont ... Doves_18.cpfont; four for some older
// recipes). Device-side font download already existed once and was deleted on
// 2026-08-10 — commit c3c8d1268, "fonts cannot be installed fully from device
// downloading them" — because it treated files as independent and so
// "it could never install a family completely". A half-installed family is a
// BROKEN family, not a smaller one: SdCardFontRegistry discovers whatever
// .cpfont files are in the directory, so a family missing its 14 pt cut is
// still offered in the picker and simply has no 14 pt.
//
// Everything below exists to make that state unrepresentable:
//
//   1. fileVerdict()   — per file, exactly LibrarySyncPlan's sizeVerdict.
//   2. familyVerdict() — ANY file needing work installs the WHOLE family. There
//                        is deliberately no "install these three files" verdict.
//   3. commitVerdict() — the staged copy moves into place ONLY when every file
//                        the manifest lists for that family was downloaded AND
//                        verified. Anything else discards the staging directory
//                        and leaves the installed family exactly as it was.
//
// The caller (FontUpdater::syncFamily) stages into "<root>/.<Family>.part",
// which SdCardFontRegistry::scanRoot skips because its name starts with a dot
// (SdCardFontRegistry.cpp:180) — so a family that is mid-download is invisible
// to the picker, not partially visible.
//
// Removal is not representable, same as the library: nothing here can say
// "delete". A family on the card that the manifest does not mention is none of
// this feature's business.

#include "LibrarySyncPlan.h"  // shaMatches / CardStamp / SyncRecord / hashVerdict / FailureKind

namespace fontsync {

// The three shared with Update Library, aliased rather than copied. They are
// not "library" concepts that fonts happen to borrow: a case-insensitive hex
// compare and a storage/network/verify taxonomy are the same problem twice,
// and two copies would drift. LibrarySyncPlan.h is pure, so this costs no
// dependency.
using librarysync::CardStamp;
using librarysync::dominantFailure;
using librarysync::FailureKind;
using librarysync::HashVerdict;
using librarysync::hashVerdict;
using librarysync::shaMatches;
using librarysync::SyncRecord;

enum class FileVerdict {
  DOWNLOAD,   // missing, or a different size — no digest needed
  CHECK_SHA,  // same size; only the sha256 can decide
};

inline FileVerdict fileVerdict(bool existsOnCard, size_t cardBytes, size_t manifestBytes) {
  if (!existsOnCard) return FileVerdict::DOWNLOAD;
  if (cardBytes != manifestBytes) return FileVerdict::DOWNLOAD;
  return FileVerdict::CHECK_SHA;
}

enum class FamilyVerdict {
  UNCHANGED,    // every file the manifest lists is present and matches — nothing is touched
  INSTALL_ALL,  // at least one file is missing or differs — the WHOLE family is staged
};

// ALL OR NOTHING, and the whole point of this header.
//
// `manifestFiles` is how many files the manifest lists for this family;
// `matchingFiles` how many of those the card already holds byte-identical.
// One missing cut means the family installs in full. Downloading only the
// missing file would be cheaper and is exactly the design that was deleted on
// 2026-08-10: it makes "five of six files from build A and one from build B" a
// reachable state, and the six cuts of a family are built together from one set
// of outlines with one set of metrics.
//
// A manifest family with NO files is INSTALL_ALL rather than UNCHANGED so it
// can never be reported as a successful no-op; FontUpdater drops such an entry
// at parse time, and if one ever reaches here it fails loudly instead.
inline FamilyVerdict familyVerdict(size_t manifestFiles, size_t matchingFiles) {
  if (manifestFiles == 0) return FamilyVerdict::INSTALL_ALL;
  if (matchingFiles >= manifestFiles) return FamilyVerdict::UNCHANGED;
  return FamilyVerdict::INSTALL_ALL;
}

enum class CommitVerdict {
  COMMIT,   // every manifest file was staged AND verified — move it into place
  DISCARD,  // anything else — delete the staging directory, keep what is installed
};

// THE GATE THAT MAKES A PARTIAL INSTALL IMPOSSIBLE.
//
// `stagedVerified` counts files that were downloaded, matched the manifest's
// byte count, and matched its sha256 — nothing weaker. `anyFailure` is set by
// the caller the moment any file of this family fails for any reason, so a
// count that happens to reach the total after a retry cannot talk its way past.
//
// Both conditions are needed. Counting alone would commit a family whose sixth
// file failed and whose fifth was somehow counted twice; the flag alone would
// commit a family whose download loop exited early without failing (an empty
// staging directory has no failures in it).
inline CommitVerdict commitVerdict(size_t manifestFiles, size_t stagedVerified, bool anyFailure) {
  if (anyFailure) return CommitVerdict::DISCARD;
  if (manifestFiles == 0) return CommitVerdict::DISCARD;
  if (stagedVerified != manifestFiles) return CommitVerdict::DISCARD;
  return CommitVerdict::COMMIT;
}

// OVERALL progress across the whole run, 0-100, with families as the
// denominator — same reasoning as librarysync::overallPercent, one level up.
// Byte totals are not knowable in advance: a family is downloaded only if the
// compare says it changed, and the installed set is ~80 MB against a few MB of
// actual work on a typical run, so a byte bar would sit near zero and lie.
inline unsigned int overallPercent(size_t doneFamilies, size_t totalFamilies, unsigned int familyPct) {
  if (totalFamilies == 0) return 0;
  if (doneFamilies >= totalFamilies) return 100;
  if (familyPct > 100) familyPct = 100;
  const size_t scaled = doneFamilies * 100u + familyPct;
  const size_t pct = scaled / totalFamilies;
  return pct > 100 ? 100u : static_cast<unsigned int>(pct);
}

// The CURRENT family's own 0-100, over its files. Same shape one level down, so
// the single bar on screen moves smoothly through six ~1 MB downloads instead
// of jumping a sixth at a time.
inline unsigned int familyPercent(size_t doneFiles, size_t totalFiles, unsigned int filePct) {
  return overallPercent(doneFiles, totalFiles, filePct);
}

// The manifest names the directory that lands under /.fonts or /fonts, so a
// hostile or corrupt entry must not be able to escape it -- and, just as
// important, must not be able to name a directory the reader will never look
// at. Alphanumeric, hyphen and underscore only, as
// FontInstaller::isValidFamilyName has it (FontInstaller.cpp:14), PLUS a first
// character that is a letter or a digit.
//
// THE FIRST-CHARACTER RULE IS NOT COSMETIC. SdCardFontRegistry::scanRoot skips
// every directory whose name begins with '.' OR '_', in one expression:
//
//     if (nameBuffer[0] == '.' || nameBuffer[0] == '_') continue;
//         -- SdCardFontRegistry.cpp:181, and again for files at :129
//
// The dot is how this feature's own staging directories hide. The underscore
// is there for macOS ._* forks, and it is the same trapdoor: a family named
// "_Doves" passes an alphanumeric-plus-underscore check, stages, verifies all
// six cuts, commits, and is reported "Added" -- and is permanently invisible in
// the picker, with no error anywhere. It never self-corrects either, because
// countMatchingFiles reads the filesystem rather than the registry, so every
// later run answers UNCHANGED. Found by adversarial review, 2026-09-07; the
// first version of this comment cited that exact line and stopped one character
// short of reading it.
inline bool isSafeFamilyName(const char* name) {
  if (!name || name[0] == '\0') return false;
  const char first = name[0];
  const bool firstAlnum =
      (first >= '0' && first <= '9') || (first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z');
  if (!firstAlnum) return false;
  for (size_t i = 0; name[i] != '\0'; ++i) {
    const char c = name[i];
    const bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (!alnum && c != '-' && c != '_') return false;
  }
  return true;
}

// The point size this file installs at, or 0 if the manifest may not put this
// name on the card. `isSafeFontFileName` below is the boolean form.
//
// STRICTER THAN "no path separators", and every clause has a specific silent
// failure behind it. SdCardFontRegistry discovers a family by scanning its
// directory and parsing exactly "<name>_<size>.cpfont"
// (SdCardFontRegistry.cpp:75-108). A file this validator lets through but that
// parser does not accept -- or accepts as a size it has already seen -- is
// written, verified against the manifest, counted toward the commit gate, and
// then IGNORED. The family "installed completely" and is missing a size on
// screen: the precise failure this whole feature exists to prevent, arriving
// through the front door.
//
//   * the name must begin with the family, then '_', so a file cannot claim to
//     belong to a family whose directory it is not being written into;
//   * the size must be 1..255, the range parseFilename accepts
//     (SdCardFontRegistry.cpp:99) -- "_0" and "_999" are otherwise well-formed;
//   * NO LEADING ZEROS. strtol reads "08" as 8, so Doves_8.cpfont and
//     Doves_08.cpfont are two files on disk that discovery resolves to ONE
//     point size; scanDirectory then drops the second as a duplicate
//     (SdCardFontRegistry.cpp:138-148) and a six-entry family lands with five
//     usable cuts while every count says six. Found by adversarial review,
//     2026-09-07;
//   * the extension must be ".cpfont" and end the string, so "Foo_14.cpfont.tmp"
//     cannot slip past -- the same ends-with reasoning parseFilename gives.
//
// The caller must ALSO reject a family whose entries resolve to the same size
// twice; a validator that sees one name at a time cannot. FontUpdater does it
// in the manifest parse loop.
inline uint8_t fontFileSize(const char* family, const char* name) {
  if (!isSafeFamilyName(family) || !name) return 0;
  size_t i = 0;
  for (; family[i] != '\0'; ++i) {
    // A short `name` ends on its NUL here, which never equals a family byte, so
    // this cannot read past it.
    if (name[i] != family[i]) return 0;
  }
  if (name[i] != '_') return 0;
  ++i;
  if (name[i] == '0') return 0;  // "_0", "_08", "_000" -- see above
  const size_t digitsStart = i;
  unsigned size = 0;
  while (name[i] >= '0' && name[i] <= '9') {
    size = size * 10u + static_cast<unsigned>(name[i] - '0');
    // Bails at the first digit past 255, so `size` cannot run away however long
    // the run of digits is.
    if (size > 255) return 0;
    ++i;
  }
  if (i == digitsStart) return 0;  // at least one digit
  if (size < 1) return 0;
  static constexpr char kExt[] = ".cpfont";
  for (size_t j = 0; j < sizeof(kExt) - 1; ++j) {
    // Every byte of kExt is non-NUL, so a short name mismatches and returns
    // before j advances past its terminator.
    if (name[i + j] != kExt[j]) return 0;
  }
  if (name[i + sizeof(kExt) - 1] != '\0') return 0;
  return static_cast<uint8_t>(size);
}

inline bool isSafeFontFileName(const char* family, const char* name) { return fontFileSize(family, name) != 0; }

}  // namespace fontsync
