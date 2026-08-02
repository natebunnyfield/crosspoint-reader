#pragma once

#include <cstdint>

// Passive WiFi event logger for diagnosing connection failures. The Arduino
// status-polling API (WiFi.status()) collapses most failure modes into
// WL_DISCONNECTED, so an auth failure (wrong password, handshake timeout) is
// indistinguishable from a slow association and surfaces only as a timeout.
// This module registers a WiFi event handler that logs every STA lifecycle
// event with its IDF disconnect reason code, and exposes the last reason so
// activities can show it and fail fast on repeated auth errors.
namespace WifiDiagnostics {

// Register the event handler (idempotent). Call before WiFi.begin().
void begin();

// Reason code of the most recent STA disconnect event, 0 if none since the
// last resetCounters(). Values are IDF WIFI_REASON_* codes.
uint8_t lastDisconnectReason();

// Number of auth-flavored disconnects (AUTH_EXPIRE, 4WAY_HANDSHAKE_TIMEOUT,
// GROUP_KEY_UPDATE_TIMEOUT, AUTH_FAIL, HANDSHAKE_TIMEOUT) since the last
// resetCounters(). The SDK retries these in a loop, so >=3 means the
// credentials are almost certainly wrong.
uint8_t authFailureCount();

// Clear lastDisconnectReason() and authFailureCount(). Call at the start of
// each connection attempt.
void resetCounters();

// Short name for an IDF WIFI_REASON_* code ("AUTH_FAIL", ...), "?" if unknown.
// Returned pointers are string literals (flash-resident).
const char* reasonName(uint8_t reason);

}  // namespace WifiDiagnostics
