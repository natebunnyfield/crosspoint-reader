// On-device Anthropic API exchange: a typed prompt in, a timestamped
// transcript appended to /claude-chat.md on the SD card.
#pragma once

#include <string>

namespace claudechat {

// Runs one full exchange while BLE is DOWN (the radios + TLS never fit in heap
// together): connect the configured WiFi network, POST the prompt to the
// Anthropic API, append both sides to the transcript, WiFi fully off.
//
// statusCb fires at each phase with a short human string for the screen.
// Returns true when a response was received and appended; on failure `error`
// carries a one-line reason (also appended to the transcript, so the file
// records failed attempts too).
struct Result {
  bool ok = false;
  std::string error;
  std::string responseText;  // for on-screen display; the file is the record
};

Result runExchange(const std::string& prompt, void (*statusCb)(void* ctx, const char* phase), void* cbCtx);

}  // namespace claudechat
