#include "PersistableStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <cstring>
#include <limits>

// Every settings and state save lands here. The write is staged through a temp
// file because the layer underneath is not atomic: SDCardManager::writeFile
// removes the target and THEN opens it O_TRUNC, so power lost in that window
// leaves no file at all and the device boots having forgotten its Wi-Fi, its
// reading position and its owner name.
//
// FAT cannot replace a file in one step -- SdFat's rename fails if the
// destination exists -- so the order is: write the temp in full, remove the
// target, rename the temp over it. The remaining window is between the remove
// and the rename, and unlike before, a COMPLETE copy of the data exists on the
// card throughout it. readDocFromFile() promotes that copy, which is what turns
// a narrower window into an actually recoverable one.
static String tempPathFor(const char* path) { return String(path) + ".tmp"; }

// True when the file at `path` holds exactly these bytes. Size first -- one
// directory lookup, no read -- then a chunked compare that stops at the first
// difference. No heap, and a read error ends it as "different" at once, where
// readFile() would append -1 as 0xFF and keep issuing failing card reads up to
// its 50 KB cap, all under the storage mutex.
static bool fileIsEmpty(const char* path) {
  HalFile file;
  return Storage.openFileForRead("PERSIST", path, file) && file.fileSize() == 0;
}

static bool fileHoldsExactly(const char* path, const char* bytes, const size_t len) {
  HalFile file;
  if (!Storage.openFileForRead("PERSIST", path, file)) return false;
  if (file.fileSize() != len) return false;
  char chunk[128];
  for (size_t off = 0; off < len;) {
    const size_t want = len - off < sizeof(chunk) ? len - off : sizeof(chunk);
    if (file.read(chunk, want) != static_cast<int>(want) || memcmp(chunk, bytes + off, want) != 0) return false;
    off += want;
  }
  return true;
}

bool PersistableStoreBase::writeDocToFile(const char* path, const JsonDocument& doc) {
  Storage.mkdir("/.crosspoint");

  // Heap headroom around the one allocation that can fail here. The JSON is
  // serialized into a single Arduino String, so a save needs a CONTIGUOUS block
  // of roughly its length on top of whatever the JsonDocument's pools already
  // took -- which is why matcha-reader frees its font tables before saving
  // (docs/matcha-heap-audit.md). Whether this fork needs that is a measurement,
  // and this is the measurement. Both lines compile out at LOG_LEVEL 0.
  LOG_DBG("PERSIST", "save %s: before serialize maxAlloc=%u free=%u", path, ESP.getMaxAllocHeap(), ESP.getFreeHeap());
  String json;
  serializeJson(doc, json);
  LOG_DBG("PERSIST", "save %s: json=%uB maxAlloc=%u free=%u", path, (unsigned)json.length(), ESP.getMaxAllocHeap(),
          ESP.getFreeHeap());

  // Identical bytes already on the card: nothing to write. The staged write
  // below is five directory operations and two FAT updates (temp create,
  // remove, rename), and on this target each is a window in which a reset
  // leaves an entry on freed clusters (docs/sd-card-corruption-2026-10-04.md).
  // The sleep/wake cycle alone issues several saves whose content has not
  // changed since the previous one -- the reader's onExit re-saves the state
  // enterDeepSleep() just wrote, onEnter re-saves what the boot path wrote.
  // The compare is a size check and at most a few KB of reads; a missing,
  // short or unreadable file never matches, so doubt falls through to the
  // write. exists() first keeps the "file does not exist" log line off the
  // first boot's saves.
  if (Storage.exists(path) && fileHoldsExactly(path, json.c_str(), json.length())) {
    LOG_DBG("PERSIST", "save %s: unchanged, skipped", path);
    return true;
  }

  const String tmp = tempPathFor(path);
  if (!Storage.writeFile(tmp.c_str(), json)) {
    LOG_ERR("PERSIST", "Failed to write %s", tmp.c_str());
    Storage.remove(tmp.c_str());  // never leave a half-written temp to be promoted
    return false;
  }
  if (Storage.exists(path) && !Storage.remove(path)) {
    LOG_ERR("PERSIST", "Failed to remove %s before rename", path);
    Storage.remove(tmp.c_str());
    return false;
  }
  if (!Storage.rename(tmp.c_str(), path)) {
    // Whatever rename() left -- nothing at either name, the temp still whole,
    // or an empty target beside the temp (SdFat removes the old entry before
    // it fills the new one, scripts/patch_sdfat.py) -- readDocFromFile()
    // sorts out on the next boot. Do NOT delete the temp here.
    LOG_ERR("PERSIST", "Failed to rename %s -> %s", tmp.c_str(), path);
    return false;
  }
  return true;
}

bool PersistableStoreBase::readDocFromFile(const char* path, JsonDocument& doc) {
  // Recover an interrupted write. A temp beside the target means a save was in
  // flight; which one to trust depends on whether the target survived.
  const String tmp = tempPathFor(path);
  if (Storage.exists(tmp.c_str())) {
    if (!Storage.exists(path) || fileIsEmpty(path)) {
      // Crashed between the remove and the rename (no target), or inside the
      // rename after SdFat removed the temp's entry and created the target but
      // before it carried the data over (an EMPTY target, the shape the
      // reordered rename leaves -- scripts/patch_sdfat.py). Either way the
      // temp is the only copy of the data -- but only promote it if it
      // actually parses, because a crash during the temp write itself leaves
      // a truncated one, and promoting that would turn a recoverable state
      // into a corrupt file.
      String candidate = Storage.readFile(tmp.c_str());
      JsonDocument probe;
      if (!candidate.isEmpty() && !deserializeJson(probe, candidate)) {
        LOG_INF("PERSIST", "Recovering %s from an interrupted write", path);
        Storage.remove(path);  // the empty target, if that is what is there
        Storage.rename(tmp.c_str(), path);
      } else {
        LOG_ERR("PERSIST", "Discarding a truncated %s", tmp.c_str());
        Storage.remove(tmp.c_str());
      }
    } else {
      // Target intact: the temp is left over from a write that failed before it
      // touched anything. Stale by definition.
      Storage.remove(tmp.c_str());
    }
  }

  if (!Storage.exists(path)) {
    return false;  // Expected on first boot — not an error.
  }
  String json = Storage.readFile(path);
  if (json.isEmpty()) {
    LOG_ERR("PERSIST", "Failed to read %s (empty)", path);
    return false;
  }
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("PERSIST", "JSON parse error in %s: %s", path, error.c_str());
    return false;
  }
  return true;
}

std::string PersistableStoreBase::extractPassword(JsonVariantConst doc, bool& needsResave) {
  bool valid = false;
  return extractPassword(doc, needsResave, std::numeric_limits<size_t>::max(), valid);
}

std::string PersistableStoreBase::extractPassword(JsonVariantConst doc, bool& needsResave, const size_t maxLength,
                                                  bool& valid) {
  valid = true;
  bool ok = false;
  bool tooLong = false;
  std::string pass = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", maxLength, &ok, &tooLong);
  if (tooLong) {
    valid = false;
    return "";
  }
  if (!ok) {
    // Deobfuscation failed — fall back to legacy plaintext password.
    const char* legacyPassword = doc["password"] | "";
    const size_t legacyLength = strlen(legacyPassword);
    if (legacyLength > maxLength) {
      valid = false;
      return "";
    }
    pass.assign(legacyPassword, legacyLength);
    if (!pass.empty()) needsResave = true;
  }
  // A successfully decoded empty string is a legitimate value; preserve as-is.
  return pass;
}
