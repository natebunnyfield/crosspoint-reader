#pragma once

// WHICH INSTALLED FAMILIES THE READER CURRENTLY WANTS -- one question, one
// place, and deliberately NOT the same question as "which families may be read
// at all" (src/ReadingFontList.h).
//
// The distinction is the whole design. `readingfonts::offeredForReading` is a
// PROJECT ruling: writing-only editor faces and retired families are withheld
// from the reading set and there is no user-facing way to bring them back.
// Deactivation here is the OWNER doing it, deliberately and reversibly, to
// shorten the in-book family cycle without deleting megabytes of .cpfont that
// would then have to be downloaded again.
//
// So the two are asked at different places on purpose:
//
//   the in-book cycle   asks BOTH -- offered, and active
//   the picker          asks offeredForReading ONLY
//
// The picker MUST go on listing a deactivated family, because the picker is the
// only place to turn it back on. A filter that reached the picker would make
// deactivation a one-way door, which is the standing "never silently remove
// user-facing capability" rule failing in its most literal form -- the
// capability would be removed by the user and then unrecoverable.
//
// STORAGE. A comma-separated list of family DIRECTORY names in a char buffer,
// which is the ligaturesOff precedent (lib/EpdFont/LigatureControl.h) and for
// the same reasons: the set is open-ended (a card carries whatever WebDAV put
// on it, including families this project has never heard of), so no fixed
// enumeration a bit index could refer to exists; and keying by name means the
// list survives the registry being reordered, a family being removed and
// re-added, and the picker's own sort. A name in the list that is not on the
// card is simply inert.
//
// Storing the OFF set rather than the ON set is not arbitrary: a newly
// installed family must arrive ACTIVE. With an ON list, every font added over
// WebDAV would land deactivated and appear broken.

#include <stddef.h>
#include <string.h>

namespace fontactivation {

// Sized for the shipping set plus room for user-installed families: the SD
// family name field is char[32] (CrossPointSettings::sdFontFamilyName), so this
// holds roughly seven maximum-length names, or many more at real lengths
// (the longest shipped is "InknutJunicode", 14). The cap is enforced rather
// than silently truncated -- see Result::NoRoom.
constexpr size_t SPEC_BUF_SIZE = 256;

// The separator. A family whose directory name CONTAINS one cannot be
// represented, and toggling it is refused rather than corrupting the list of
// every other family. Directory names may legally contain a comma, so this is a
// real case and not a theoretical one.
constexpr char kSeparator = ',';

enum class Result {
  Deactivated,   // was active, now off
  Reactivated,   // was off, now active
  RefusedLast,   // would have left zero active families; the reader needs one
  RefusedName,   // the family name contains the separator and cannot be stored
  NoRoom,        // the spec buffer cannot hold another name
};

// True when `family` appears as a whole token in `spec`.
//
// Whole token, not substring: "Edgar" must not match inside "EdgarPro". The
// three-line check below is the entire reason this is a function rather than a
// strstr at each call site.
inline bool isDeactivated(const char* spec, const char* family) {
  if (spec == nullptr || family == nullptr || *family == '\0') return false;
  const size_t len = strlen(family);
  for (const char* p = spec; *p != '\0';) {
    const char* end = strchr(p, kSeparator);
    const size_t tokenLen = end != nullptr ? static_cast<size_t>(end - p) : strlen(p);
    if (tokenLen == len && strncmp(p, family, len) == 0) return true;
    if (end == nullptr) break;
    p = end + 1;
  }
  return false;
}

// Convenience inverse, so call sites read as the question they are asking.
inline bool isActive(const char* spec, const char* family) { return !isDeactivated(spec, family); }

// Remove `family` from `spec` in place. No-op when absent.
inline void removeToken(char* spec, const char* family) {
  if (spec == nullptr || family == nullptr) return;
  const size_t len = strlen(family);
  for (char* p = spec; *p != '\0';) {
    char* end = strchr(p, kSeparator);
    const size_t tokenLen = end != nullptr ? static_cast<size_t>(end - p) : strlen(p);
    if (tokenLen == len && strncmp(p, family, len) == 0) {
      if (end != nullptr) {
        // Drop this token AND its trailing separator.
        memmove(p, end + 1, strlen(end + 1) + 1);
      } else {
        // Last token: also drop the separator that precedes it, if any.
        if (p != spec) p--;
        *p = '\0';
      }
      return;
    }
    if (end == nullptr) return;
    p = end + 1;
  }
}

// How many of `families` are currently active.
//
// The caller passes the list the PICKER shows -- i.e. already filtered by
// readingfonts::offeredForReading -- because that is the set the reader can
// actually cycle through, and it is what the last-active rule must protect. A
// count taken over the raw registry would let the last reading family be
// deactivated whenever a writing-only face happened to be installed.
inline size_t activeCount(const char* spec, const char* const* families, size_t familyCount) {
  size_t n = 0;
  for (size_t i = 0; i < familyCount; i++) {
    if (families[i] != nullptr && isActive(spec, families[i])) n++;
  }
  return n;
}

// Toggle `family`, enforcing every refusal. `spec` is modified only on success.
//
// THE LAST ACTIVE FAMILY CANNOT BE DEACTIVATED. Deactivating everything would
// leave the in-book cycle with nothing to step to and the reader resolving to a
// built-in the picker does not even list once SD families exist. The UI refuses
// rather than recovering, because a silent recovery here is indistinguishable
// from the toggle not working.
inline Result toggle(char* spec, size_t cap, const char* family, const char* const* families,
                     size_t familyCount) {
  if (spec == nullptr || family == nullptr || *family == '\0') return Result::RefusedName;
  if (strchr(family, kSeparator) != nullptr) return Result::RefusedName;

  if (isDeactivated(spec, family)) {
    removeToken(spec, family);
    return Result::Reactivated;
  }

  // Deactivating. Refuse if this is the last one standing.
  if (activeCount(spec, families, familyCount) <= 1) return Result::RefusedLast;

  const size_t used = strlen(spec);
  const size_t need = used + (used > 0 ? 1 : 0) + strlen(family) + 1;
  if (need > cap) return Result::NoRoom;

  if (used > 0) {
    spec[used] = kSeparator;
    memcpy(spec + used + 1, family, strlen(family) + 1);
  } else {
    memcpy(spec, family, strlen(family) + 1);
  }
  return Result::Deactivated;
}

}  // namespace fontactivation
