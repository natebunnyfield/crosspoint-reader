#pragma once

#include <HalStorage.h>

#include <string>

// One-line secret files on the card root.
//
// /claude-key.txt (Claude chat) and /github-token.txt (Update Library) share
// this reader so they behave identically: the first 255 bytes of the file,
// with the trailing newline or spaces an editor leaves behind trimmed off.
// A missing or empty file reads as "not configured" (false) rather than as a
// blank credential, so callers can say where the file goes instead of sending
// an empty header and decoding a 401.
//
// Header-only on purpose: a new translation unit would have to be added to
// the simulator's generated iOS source list as well (see CLAUDE.md), and two
// call sites do not justify that churn.
//
// NEVER log the value read here, at any call site.
namespace cardsecret {

inline bool readOneLine(const char* tag, const char* path, std::string& out) {
  HalFile f;
  if (!Storage.openFileForRead(tag, path, f)) return false;
  char buf[256];
  const int n = f.read(buf, sizeof(buf) - 1);
  if (n <= 0) return false;
  buf[n] = '\0';
  out = buf;
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
  return !out.empty();
}

}  // namespace cardsecret
