#pragma once

#include <Logging.h>

#include <string>

#include "CrossPointSettings.h"
#include "util/CardSecret.h"
#ifdef SIMULATOR
// The host's own settings surface, where a platform with no way to edit the
// card's settings.json keeps this token instead. Simulator-only by
// construction: the header is part of the simulator library and folds to a
// constant everywhere but a phone. See SimHostSettings.h.
#include <SimHostSettings.h>
#endif

// The GitHub credential shared by every screen that reads a private release on
// natebunnyfield/claude-tools -- Update Library (LibraryUpdater) and Update
// Fonts (FontUpdater). ONE token, not one per feature: both releases live in
// the same private repo, so a second setting would be a second thing to get
// wrong on a phone keyboard for no gain.
//
// Lifted out of LibraryUpdater.cpp's anonymous namespace when Update Fonts
// arrived. A copy in each file would be two places for the "never log this"
// contract and the three-source precedence to drift apart, and credential
// handling is the last thing that should exist twice.
namespace githubauth {

// Where the token lives on the card: one line, hand-placed, the same pattern
// as /claude-key.txt. Read through util/CardSecret.h so the two files behave
// identically (trailing newline trimmed, empty means "not configured").
inline constexpr const char* TOKEN_PATH = "/github-token.txt";

// The token, from wherever this build's owner can actually set one, in order:
//
//   1. the simulator's own settings surface, when it holds anything. A HOST
//      BUILD MAY HAVE NO WAY TO EDIT A FILE ON THE CARD -- an iPhone does not.
//   2. /github-token.txt on the card root. Wins over the settings field
//      because a file the owner placed there is the more deliberate act, and
//      it does not round-trip through a settings save.
//   3. SETTINGS.githubToken, hand-edited into /.crosspoint/settings.json --
//      the original home, kept so existing cards keep working.
//
// Nothing here is copied INTO SETTINGS: that field is persisted by the next
// settings save, and on iOS the directory it saves to is served over the LAN
// by File Transfer and WebDAV. One fewer copy of a credential, for no loss of
// function. (The token file is served the same way; see the doc.)
//
// NEVER LOG THE RETURN VALUE, here or at any call site.
inline std::string tokenValue(const char* logModule) {
#ifdef SIMULATOR
  char hosted[sizeof(SETTINGS.githubToken)] = {};
  const size_t hostedLength = sim_host_settings::githubToken(hosted, sizeof(hosted));
  if (hostedLength != 0) {
    // Length only. A token longer than the field is a paste error, and it will
    // fail authentication with a 401 that says nothing about why -- so the one
    // place that can tell says so, without the bytes.
    if (hostedLength > sizeof(hosted) - 1) {
      LOG_ERR(logModule, "host GitHub token is %u bytes; the field holds %u -- truncated, and it will not authenticate",
              static_cast<unsigned>(hostedLength), static_cast<unsigned>(sizeof(hosted) - 1));
    }
    return std::string(hosted);
  }
#endif
  std::string fromFile;
  if (cardsecret::readOneLine(logModule, TOKEN_PATH, fromFile)) return fromFile;
  return std::string(SETTINGS.githubToken);
}

// Built in one place so no call site ever holds the raw token where a log
// line could pick it up. NEVER log the returned value.
inline std::string bearerHeaderValue(const char* logModule) { return std::string("Bearer ") + tokenValue(logModule); }

}  // namespace githubauth
