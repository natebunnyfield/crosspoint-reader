#pragma once

// Keys and tokens the firmware sends to services: the one place they are read,
// written and cleared. The pure half (names, trimming, the last-4 rule, the
// entry text) is util/KeyStoreCore.h; this binds it to storage.
//
// NOTHING ELSE: no key is read from the card or settings.json, and nothing
// writes one there. The /github-token.txt, /claude-key.txt and settings.json
// githubToken older builds read are ignored, not migrated (owner 2026-11-26:
// "never worry about backwards compatibility ... while this is in alpha").
//
// WHERE THEY LIVE (owner ruling 2026-11-26, "NVS + NSUserDefaults"):
//   * device: ESP32 NVS, namespace "cpkeys" -- never on the card, so neither
//     File Transfer nor WebDAV can serve it. NVS is plaintext in flash on a
//     board without flash encryption: readable with the device, USB and
//     esptool, not over the network.
//   * simulator: the host's settings surface (SimHostSettings.h) -- on iOS the
//     NSUserDefaults keys the Settings.app rows write, on the desktop a file
//     beside the simulated card.
//
// NEVER LOG A VALUE: at most a key's name and a length.
//
// Header-only (see KeyStoreCore.h).

#include <HalStorage.h>
#include <Logging.h>

#include <string>

#include "util/KeyStoreCore.h"

#ifdef SIMULATOR
#include <SimHostSettings.h>
#else
#include <Preferences.h>
#endif

namespace keystore {

#ifndef SIMULATOR
inline constexpr const char* kNvsNamespace = "cpkeys";
#endif

// The stored value, cleaned (KeyStoreCore normalize). False when unset.
inline bool get(const Key key, std::string& out) {
  out.clear();
#ifdef SIMULATOR
  char buf[kMaxLength + 2] = {};
  const size_t length = key == Key::GithubToken ? sim_host_settings::githubToken(buf, sizeof(buf))
                                                : sim_host_settings::claudeApiKey(buf, sizeof(buf));
  if (length == 0) return false;
  if (length > kMaxLength) {
    LOG_ERR("KEYS", "%s: the host holds %u chars, over the %u kept; ignored", storageName(key),
            static_cast<unsigned>(length), static_cast<unsigned>(kMaxLength));
    return false;
  }
  return normalize(std::string(buf), out);
#else
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, true)) return false;  // namespace not created yet: nothing set
  const auto raw = prefs.getString(nvsName(key), "");
  prefs.end();
  if (raw.length() == 0) return false;
  return normalize(std::string(raw.c_str()), out);
#endif
}

inline bool isSet(const Key key) {
  std::string unused;
  return get(key, unused);
}

// Store `value` (cleaned first). False when it is not a usable key (empty
// after trimming, a control character inside, longer than kMaxLength) or the
// store refused it; the stored value is then unchanged.
inline bool set(const Key key, const std::string& value) {
  std::string clean;
  if (!normalize(value, clean)) return false;
#ifdef SIMULATOR
  const bool ok = key == Key::GithubToken ? sim_host_settings::setGithubToken(clean.c_str())
                                          : sim_host_settings::setClaudeApiKey(clean.c_str());
#else
  Preferences prefs;
  bool ok = prefs.begin(kNvsNamespace, false);
  if (ok) {
    ok = prefs.putString(nvsName(key), clean.c_str()) == clean.size();
    prefs.end();
  }
#endif
  if (ok) {
    LOG_INF("KEYS", "%s: saved (%u chars)", storageName(key), static_cast<unsigned>(clean.size()));
  } else {
    LOG_ERR("KEYS", "%s: the store refused the write", storageName(key));
  }
  return ok;
}

inline bool clear(const Key key) {
#ifdef SIMULATOR
  const bool ok = key == Key::GithubToken ? sim_host_settings::setGithubToken(nullptr)
                                          : sim_host_settings::setClaudeApiKey(nullptr);
#else
  Preferences prefs;
  bool ok = prefs.begin(kNvsNamespace, false);
  if (ok) {
    // remove() is false when the key was never there; cleared either way.
    prefs.remove(nvsName(key));
    prefs.end();
  }
#endif
  LOG_INF("KEYS", "%s: cleared", storageName(key));
  return ok;
}

// The last 4 characters, or "" (see KeyStoreCore last4). For display only.
inline void last4(const Key key, char out[5]) {
  std::string value;
  out[0] = '\0';
  if (get(key, value)) keystore::last4(value, out);
}

// The card files older builds read keys from. Nothing reads them now (no
// migration: owner's no-backwards-compatibility ruling, 2026-11-26), but File
// Transfer and WebDAV would still serve them to the network, so boot deletes
// them without reading or importing them (owner 2026-11-26, "Delete at boot").
inline constexpr const char* kLegacyCardKeyFiles[] = {"/github-token.txt", "/claude-key.txt"};

// Run at boot once the card is mounted. Cheap: two exists() checks. Logs the
// path only, never content.
inline void removeLegacyCardFiles() {
  for (const char* path : kLegacyCardKeyFiles) {
    if (!Storage.exists(path)) continue;
    if (Storage.remove(path)) {
      LOG_INF("KEYS", "removed %s from the card (keys live in Settings now)", path);
    } else {
      LOG_ERR("KEYS", "could not remove %s from the card", path);
    }
  }
}

}  // namespace keystore
