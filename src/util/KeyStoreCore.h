#pragma once

// Keys and tokens the firmware sends to services, the pure half: which keys
// exist, and how a value is cleaned and shown. No storage here, so it is host-tested
// (test/key_store) against fakes; util/KeyStore.h binds it to NVS on the device
// and to the host's settings surface in the simulator.
//
// Owner 2026-11-26: keys get Settings rows of their own, a saved value shows
// only its last 4 characters, and NO key may ever land in settings.json on the
// card (File Transfer and WebDAV serve the card to the whole network), appear in
// a log line, the web settings API, or a screenshot. Storage ruling the same
// day: "NVS + NSUserDefaults". docs/keys-and-paste-2026-11-26.md.
//
// NEVER LOG A VALUE. This file logs nothing; every log line in KeyStore.h
// names a key and, at most, a length.
//
// Header-only: a new translation unit would have to be added to the
// simulator's generated iOS source list (CLAUDE.md).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace keystore {

enum class Key : uint8_t { GithubToken = 0, ClaudeApiKey = 1 };
inline constexpr int kKeyCount = 2;
inline constexpr Key kAllKeys[kKeyCount] = {Key::GithubToken, Key::ClaudeApiKey};

// Longest value kept. A GitHub fine-grained token is ~93 characters and an
// Anthropic key ~108; 255 is the cap the old card files were read with.
inline constexpr size_t kMaxLength = 255;

// The one name a key goes by in NVS, in the host store (the iOS NSUserDefaults
// keys the Settings.app rows already write) and in the web API.
constexpr const char* storageName(const Key key) { return key == Key::GithubToken ? "githubToken" : "claudeApiKey"; }

// NVS keys are capped at 15 characters; these are the device's own.
constexpr const char* nvsName(const Key key) { return key == Key::GithubToken ? "gh" : "claude"; }

constexpr bool keyFromName(const char* name, Key& out) {
  if (name == nullptr) return false;
  for (const Key k : kAllKeys) {
    const char* a = storageName(k);
    const char* b = name;
    while (*a != '\0' && *a == *b) {
      ++a;
      ++b;
    }
    if (*a == '\0' && *b == '\0') {
      out = k;
      return true;
    }
  }
  return false;
}

// A value as it is kept: surrounding whitespace dropped (a paste or a file
// often carries a newline), spaces and control characters inside refused (no
// key contains one; a space inside is a paste of more than the key, and a
// control character would break the HTTP header it is sent in), and the length
// capped. Returns false when nothing usable is left or the input is too long.
inline bool normalize(const std::string& in, std::string& out) {
  size_t begin = 0;
  size_t end = in.size();
  const auto space = [](const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (begin < end && space(in[begin])) ++begin;
  while (end > begin && space(in[end - 1])) --end;
  if (end == begin || end - begin > kMaxLength) return false;
  for (size_t i = begin; i < end; ++i) {
    const auto c = static_cast<unsigned char>(in[i]);
    if (c <= 0x20 || c == 0x7f) return false;
  }
  out.assign(in, begin, end - begin);
  return true;
}

// Showing 4 characters of a short value would give most of it away, so a value
// shorter than this shows as saved with no characters at all. Every real key
// and token is far longer.
inline constexpr size_t kMinLengthForLast4 = 12;

// The last 4 characters of `value` into `out` (5 bytes, NUL-terminated), or an
// empty string when the value is empty or shorter than kMinLengthForLast4. The
// only part of a key any screen, page or API ever shows.
inline void last4(const std::string& value, char out[5]) {
  out[0] = '\0';
  if (value.size() < kMinLengthForLast4) return;
  for (int i = 0; i < 4; ++i) out[i] = value[value.size() - 4 + i];
  out[4] = '\0';
}

// What a text field shows while a key is typed or pasted (InputType::Secret).
// Owner 2026-11-26: "do not wrap long secret inputs or outputs". So the field
// is ONE line whatever the length: a fixed run of '*' and the length, never one
// '*' per byte, and no character of the key at all (a last 4 shown while typing
// moved with each keystroke, so across frames it revealed everything from the
// ninth character on; pass 3). The saved row still shows the last 4 (last4).
// Empty for an empty value. `oneText` is the singular ("1 character"),
// `manyFormat` the plural with one %u; both come from tr().
inline constexpr size_t kEntryMaskStars = 8;

inline size_t codePointCount(const std::string& value) {
  size_t n = 0;
  for (const char c : value) {
    if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
  }
  return n;
}

inline std::string secretEntryText(const std::string& value, const char* oneText, const char* manyFormat) {
  if (value.empty()) return std::string();
  const size_t n = codePointCount(value);
  char length[48];
  if (n == 1) {
    snprintf(length, sizeof(length), "%s", oneText);
  } else {
    snprintf(length, sizeof(length), manyFormat, static_cast<unsigned>(n));
  }
  std::string shown(kEntryMaskStars, '*');
  shown += "  ";
  shown += length;
  return shown;
}

}  // namespace keystore
