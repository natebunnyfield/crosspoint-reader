#pragma once
// The justification threshold's settings.json key, and its ONE-TIME migration
// 40 -> 34 (owner ruling 2026-09-26, "One-time migrate 40->34").
//
// Why a migration at all: the default dropped from 40 to 34 the same day
// (AutoJustify.h), but CrossPointSettings::toJson writes justifyThreshold on
// every save, so every device that ever saved settings holds a 40 that the OLD
// DEFAULT wrote -- and a changed default reaches only a file with no key. A
// chosen 40 and a defaulted 40 look identical, so the owner ruled to migrate
// once and mark the file:
//
//   * 40 with no `justifyMigrated34` flag -> 34, and the file is re-saved;
//   * any other value with no flag -> unchanged, and re-saved to gain the flag;
//   * the flag present -> nothing, so a 40 chosen AFTER the migration sticks;
//   * every save writes the flag, so it runs exactly once per card.
//
// A factory-fresh unit never loads a file (PersistableStore::loadFromFile
// returns early with none), so it starts at the 34 default and its first save
// writes the flag with it.
//
// Header-only and pure so it can be host-tested without the settings stack:
// test/justify_migration.

#include <ArduinoJson.h>

#include <cstdint>

#include "Epub/AutoJustify.h"

namespace justifysetting {

inline constexpr const char* KEY = "justifyThreshold";
inline constexpr const char* MIGRATED_KEY = "justifyMigrated34";
inline constexpr int OLD_DEFAULT = 40;

// The decision, on a value already matched against the ladder.
constexpr int migrate(const int stored, const bool alreadyMigrated) {
  return (!alreadyMigrated && stored == OLD_DEFAULT) ? autojustify::THRESHOLD_CHARS : stored;
}

struct Loaded {
  uint8_t threshold;
  bool needsResave;  // true until the file carries the flag
};

inline Loaded read(JsonVariantConst doc) {
  // Matched against the offered ladder first (a value nobody chose falls to the
  // default), exactly as before the migration existed.
  const int stored = autojustify::clampThreshold(doc[KEY] | autojustify::THRESHOLD_CHARS);
  const bool migrated = doc[MIGRATED_KEY] | false;
  return {static_cast<uint8_t>(migrate(stored, migrated)), !migrated};
}

inline void write(JsonDocument& doc, const uint8_t threshold) {
  doc[KEY] = threshold;
  doc[MIGRATED_KEY] = true;
}

}  // namespace justifysetting
