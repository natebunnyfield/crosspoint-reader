#pragma once

// DELETIONS STICK: the families the owner removed from this card, and the
// families the HOST bundled onto it. Owner bug 2026-09-26: "warblertext and
// lutetianova and other fonts keep getting recreated after i delete them."
// Full trace: docs/font-deletions-stick-2026-09-26.md.
//
// WHY THIS EXISTS. Update Fonts is a MIRROR of the fonts-latest manifest
// (FontUpdater.h). A family the manifest lists and the card lacks is ADDED --
// which cannot tell "never installed here" from "the owner deleted it", so every
// run re-downloaded whatever he had just removed. The iOS app's seed pass had
// the same bug on 2026-09-15 and the same fix: a ledger on the card
// (ios/CrossPointFsPrep.cpp in crosspoint-simulator, .crosspoint/seeded-fonts.txt).
// This file is that ledger's firmware twin, with the same semantics:
//
//   listed, absent from both roots   -> the owner deleted it. DO NOT DOWNLOAD.
//   listed, present on the card      -> he put it back (web installer, a copy
//                                       into fonts/, WebDAV, the Files app).
//                                       Drop it from the list and sync as usual.
//   not listed                       -> as before.
//
// A family gets on the list two ways:
//   1. FontInstaller::deleteFamily -- the firmware's own delete (the web Fonts
//      page). Recorded at the moment of deletion.
//   2. INFERRED by FontUpdater::syncFamily: the family is absent, but the sync's
//      own ledger (font_sync.json) holds records for it -- so the sync saw it on
//      this card before, and the sync never deletes a family the manifest lists.
//      Something else did: the Files app, Manage Files, WebDAV, a card reader.
//      This is what makes a delete that never passes through the firmware stick,
//      exactly as the seed ledger needs no delete notification from anyone.
//
// A family leaves it when FontInstaller::ensureFamilyDir runs for it (the web
// installer's first step) or when a sync finds it present again.
//
// FORMAT: one family per line, '#' comments, the same shape as
// seeded-fonts.txt, so one parser reads both and the owner can edit either with
// a text editor over File Transfer. Deleting a line brings that family back on
// the next sync (FontUpdater drops its ledger records on a skip precisely so the
// inference cannot put the line straight back).
//
// A MISSING OR UNREADABLE LIST IS EMPTY. Same safe direction as the seed
// ledger: the failure mode is a font reappearing, never a font silently
// withheld. A line that is not a safe family name (fontsync::isSafeFamilyName,
// the validator the install side uses) is ignored rather than trusted.
//
// Header-only on purpose: the iOS source set in crosspoint-simulator is
// generated from this repo's compile database (cmake/CrossPointSources.cmake),
// and a new .cpp would need that regenerated.

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <string>
#include <vector>

#include "FontSyncPlan.h"  // fontsync::isSafeFamilyName

namespace fontdeletions {

// Owner-deleted families, written by this firmware.
constexpr char kDeletedListPath[] = "/.crosspoint/deleted-fonts.txt";
// Families the HOST bundled onto this card, written by the iOS harness's seed
// pass (crosspoint-simulator ios/CrossPointFsPrep.cpp kSeedLedgerPath, which is
// relative to the card root -- the same file). Read only; absent on the device
// and on the desktop, where nothing seeds.
constexpr char kHostSeededListPath[] = "/.crosspoint/seeded-fonts.txt";

// Bound on what is read. The list is a few dozen bytes; a file bigger than this
// is not one this firmware wrote, and is treated as unreadable (empty).
constexpr size_t kMaxListBytes = 4096;

// --- pure ---------------------------------------------------------------------

inline std::vector<std::string> parse(const std::string& text) {
  std::vector<std::string> names;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(start, end - start);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    if (!line.empty() && line[0] != '#' && fontsync::isSafeFamilyName(line.c_str()) &&
        std::find(names.begin(), names.end(), line) == names.end()) {
      names.push_back(line);
    }
    start = end + 1;
  }
  return names;
}

inline std::string serialize(const std::vector<std::string>& names) {
  std::string out =
      "# Font families deleted from this card. Update Fonts does NOT download a\n"
      "# family listed here while it is absent. Delete a line to have that family\n"
      "# come back on the next Update Fonts; installing it again removes it too.\n";
  for (const auto& n : names) out += n + "\n";
  return out;
}

inline bool contains(const std::vector<std::string>& names, const std::string& family) {
  return std::find(names.begin(), names.end(), family) != names.end();
}

// --- storage ------------------------------------------------------------------

inline std::vector<std::string> loadFrom(const char* path) {
  HalFile file;
  if (!Storage.exists(path) || !Storage.openFileForRead("FONTDEL", path, file)) return {};
  const size_t bytes = file.size();
  if (bytes == 0 || bytes > kMaxListBytes) {
    file.close();
    return {};
  }
  std::string text(bytes, '\0');
  const int got = file.read(&text[0], bytes);
  file.close();
  if (got < 0) return {};
  text.resize(static_cast<size_t>(got));
  return parse(text);
}

inline std::vector<std::string> loadDeleted() { return loadFrom(kDeletedListPath); }
inline std::vector<std::string> loadHostSeeded() { return loadFrom(kHostSeededListPath); }

inline bool saveDeleted(const std::vector<std::string>& names) {
  Storage.ensureDirectoryExists("/.crosspoint");
  HalFile file;
  if (!Storage.openFileForWrite("FONTDEL", kDeletedListPath, file)) {
    LOG_ERR("FONTDEL", "Could not write %s", kDeletedListPath);
    return false;
  }
  const std::string body = serialize(names);
  const size_t wrote = file.write(body.data(), body.size());
  file.close();
  return wrote == body.size();
}

// Add `family` to the owner-deleted list. A no-op when it is already there.
inline void recordDeleted(const std::string& family) {
  if (!fontsync::isSafeFamilyName(family.c_str())) return;
  std::vector<std::string> names = loadDeleted();
  if (contains(names, family)) return;
  names.push_back(family);
  if (saveDeleted(names)) LOG_INF("FONTDEL", "%s recorded as deleted by the owner", family.c_str());
}

// Remove `family` from the owner-deleted list. Writes only when it was listed,
// so the web installer's per-file call costs one small read and no write.
inline void clearDeleted(const std::string& family) {
  std::vector<std::string> names = loadDeleted();
  const auto it = std::find(names.begin(), names.end(), family);
  if (it == names.end()) return;
  names.erase(it);
  if (saveDeleted(names)) LOG_INF("FONTDEL", "%s re-added; no longer on the deleted list", family.c_str());
}

}  // namespace fontdeletions
