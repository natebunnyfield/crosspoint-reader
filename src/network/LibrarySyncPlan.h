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
//   2. On CHECK_SHA, ask hashVerdict() whether the hash is needed at all, and
//      only then hash the card file and ask shaMatches().
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

// What the ledger remembers about a book the last time its digest was actually
// computed, and what the card says about it now.
//
// Owner ruling 2026-08-23: SHA-256 every book on every run was the biggest
// remaining cost of Update Library -- the whole library read end to end to
// discover, almost always, that nothing had changed. Size and modification time
// are two numbers the directory entry already holds.
struct CardStamp {
  size_t bytes = 0;
  uint16_t fatDate = 0;  // FAT-encoded, as HalFile::getModifyDateTime returns them
  uint16_t fatTime = 0;
  // FALSE when the HAL could not answer. A card or a port that has no
  // modification time is not a card where everything is unchanged.
  bool haveMtime = false;
};

struct SyncRecord {
  bool present = false;  // false = this build has never verified this book
  size_t bytes = 0;
  uint16_t fatDate = 0;
  uint16_t fatTime = 0;
  const char* sha = nullptr;  // the digest that was verified, lowercase hex
};

enum class HashVerdict {
  SKIP_HASH,  // size AND mtime match what was recorded beside a digest the manifest still wants
  MUST_HASH,  // anything else, INCLUDING every way of not knowing
};

// Every "I cannot tell" answer is MUST_HASH, and that is the whole safety
// argument. The first run after this shipped has no records at all, and a
// verdict that skipped on a missing record would declare the entire library
// unchanged and never look at it again -- a silent no-op that looks exactly
// like a fast sync.
inline HashVerdict hashVerdict(const CardStamp& card, const SyncRecord& record, const char* manifestSha) {
  if (!record.present) return HashVerdict::MUST_HASH;
  if (!card.haveMtime) return HashVerdict::MUST_HASH;
  if (record.bytes != card.bytes) return HashVerdict::MUST_HASH;
  if (record.fatDate != card.fatDate || record.fatTime != card.fatTime) return HashVerdict::MUST_HASH;
  // The record says "these bytes hash to X". If the manifest now wants
  // something else, the record is evidence FOR a download, not against reading
  // the file -- but it is only evidence, so hash and be sure. Skipping here
  // would trust a stored digest to decide a download.
  if (!shaMatches(record.sha, manifestSha)) return HashVerdict::MUST_HASH;
  return HashVerdict::SKIP_HASH;
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
