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

bool PersistableStoreBase::writeDocToFile(const char* path, const JsonDocument& doc) {
  Storage.mkdir("/.crosspoint");

  // Heap headroom around the one allocation that can fail here. The JSON is
  // serialized into a single Arduino String, so a save needs a CONTIGUOUS block
  // of roughly its length on top of whatever the JsonDocument's pools already
  // took -- which is why matcha-reader frees its font tables before saving
  // (docs/matcha-heap-audit.md). Whether this fork needs that is a measurement,
  // and this is the measurement. Both lines compile out at LOG_LEVEL 0.
  LOG_DBG("PERSIST", "save %s: before serialize maxAlloc=%u free=%u", path, ESP.getMaxAllocHeap(),
          ESP.getFreeHeap());
  String json;
  serializeJson(doc, json);
  LOG_DBG("PERSIST", "save %s: json=%uB maxAlloc=%u free=%u", path, (unsigned)json.length(),
          ESP.getMaxAllocHeap(), ESP.getFreeHeap());

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
    // The target is gone and the temp is still there and complete. Do NOT
    // delete it: readDocFromFile() promotes it on the next boot, which is the
    // whole point of writing it first.
    LOG_ERR("PERSIST", "Failed to rename %s -> %s; the temp holds the data", tmp.c_str(), path);
    return false;
  }
  return true;
}

bool PersistableStoreBase::readDocFromFile(const char* path, JsonDocument& doc) {
  // Recover an interrupted write. A temp beside the target means a save was in
  // flight; which one to trust depends on whether the target survived.
  const String tmp = tempPathFor(path);
  if (Storage.exists(tmp.c_str())) {
    if (!Storage.exists(path)) {
      // Crashed between the remove and the rename. The temp is the only copy of
      // the data -- but only promote it if it actually parses, because a crash
      // during the temp write itself leaves a truncated one, and promoting that
      // would turn a recoverable state into a corrupt file.
      String candidate = Storage.readFile(tmp.c_str());
      JsonDocument probe;
      if (!candidate.isEmpty() && !deserializeJson(probe, candidate)) {
        LOG_INF("PERSIST", "Recovering %s from an interrupted write", path);
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
