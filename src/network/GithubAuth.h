#pragma once

#include <Logging.h>

#include <string>

#include "util/KeyStore.h"

// The GitHub credential shared by every screen that reads a private release on
// natebunnyfield/claude-tools -- Update Library (LibraryUpdater) and Update
// Fonts (FontUpdater). ONE token, not one per feature.
//
// Since 2026-11-26 it lives in the key store (util/KeyStore.h: NVS on the
// device, the host settings surface in the simulator), set from Settings >
// GitHub Token or the device web page. The card file and the settings.json
// field older builds read are ignored, so this reads one place.
namespace githubauth {

// The token, or "" when none is set. NEVER LOG THE RETURN VALUE, here or at
// any call site. `logModule` is kept for the call sites' signature.
inline std::string tokenValue(const char* /*logModule*/) {
  std::string token;
  keystore::get(keystore::Key::GithubToken, token);
  return token;
}

// Built in one place so no call site ever holds the raw token where a log
// line could pick it up. NEVER log the returned value.
inline std::string bearerHeaderValue(const char* logModule) { return std::string("Bearer ") + tokenValue(logModule); }

}  // namespace githubauth
