#pragma once

#include <cstddef>

// The Update Library compare logic, PURE on purpose: no Storage, no network,
// no Arduino. Every decision here is host-testable (the simulator repo compiles
// this header directly in tests/library_sync_plan_test.cpp), because a wrong
// verdict is silent on device — a book that should update is skipped, or an
// unchanged book is re-downloaded on every run, and neither looks like an
// error on screen.
//
// The contract with the caller (LibraryUpdater):
//   1. Ask sizeVerdict() with what the card holds and what the manifest
//      promises. DOWNLOAD needs no hash work; CHECK_SHA means the sizes agree
//      and only the digest can tell the two apart.
//   2. On CHECK_SHA, hash the card file and ask shaMatches().
// Removal is deliberately not representable: nothing here can say "delete" —
// books on the card that the manifest does not mention are none of this
// feature's business.
namespace librarysync {

enum class SizeVerdict {
  DOWNLOAD,   // missing, or a different size — no digest needed
  CHECK_SHA,  // same size; only the sha256 can decide
};

inline SizeVerdict sizeVerdict(bool existsOnCard, size_t cardBytes, size_t manifestBytes) {
  if (!existsOnCard) return SizeVerdict::DOWNLOAD;
  if (cardBytes != manifestBytes) return SizeVerdict::DOWNLOAD;
  return SizeVerdict::CHECK_SHA;
}

// Case-insensitive hex compare: GitHub tooling and openssl disagree about hex
// case, and a case-sensitive compare would re-download the whole library
// forever without ever reporting anything wrong.
inline bool shaMatches(const char* a, const char* b) {
  if (!a || !b) return false;
  size_t i = 0;
  for (; a[i] != '\0' && b[i] != '\0'; ++i) {
    char ca = a[i];
    char cb = b[i];
    if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
    if (ca != cb) return false;
  }
  // Both must end together, and a sha256 hex digest is exactly 64 chars —
  // two empty strings matching each other would otherwise read as "unchanged".
  return a[i] == '\0' && b[i] == '\0' && i == 64;
}

// The manifest names the file that lands on the card, so a hostile or corrupt
// manifest entry must not be able to escape /books/. No separators, no
// dot-leading names (".." and hidden files both), no empties.
inline bool isSafeFileName(const char* name) {
  if (!name || name[0] == '\0' || name[0] == '.') return false;
  for (size_t i = 0; name[i] != '\0'; ++i) {
    if (name[i] == '/' || name[i] == '\\') return false;
  }
  return true;
}

}  // namespace librarysync
