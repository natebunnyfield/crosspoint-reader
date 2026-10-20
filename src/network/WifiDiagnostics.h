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
//
// ASSOC_LEAVE (8) is NOT recorded. It is what the STATION sends when it
// leaves, so on this device it is almost always our own doing (an AP shutting
// down can send it too, and is hidden with the rest): every
// WiFi.disconnect(), and the core's own retry, which disconnects before it
// reconnects (arduino-esp32 STA.cpp, DoReconnect). Recording it overwrote the
// real cause, and the owner's failure screen read "[8 ASSOC_LEAVE]" whatever
// had actually gone wrong. It is still logged.
uint8_t lastDisconnectReason();

// Number of auth-flavored disconnects (AUTH_EXPIRE, 4WAY_HANDSHAKE_TIMEOUT,
// GROUP_KEY_UPDATE_TIMEOUT, AUTH_FAIL, HANDSHAKE_TIMEOUT) since the last
// resetCounters(). The SDK retries these in a loop, so >=3 means the
// credentials are almost certainly wrong.
uint8_t authFailureCount();

// Clear lastDisconnectReason() and authFailureCount(). Call at the start of
// each connection attempt.
void resetCounters();

// True between an STA_CONNECTED event and the next disconnect or stop: the
// radio has JOINED the network. The Arduino core reports WL_CONNECTED only
// once DHCP has handed out an address, so "associated but not WL_CONNECTED"
// is the window in which the lease is still being waited for -- a join that is
// working, which a connect timeout must not cut off as if it were not.
bool isAssociated();

// Writes a header naming `what` and the in-memory log ring (its last 16 lines,
// MAX_LOG_LINES in Logging.cpp; the "EVT ..." lines are INF) to /wifi.log on the card, replacing the last
// one. The ring is otherwise dumped only by a panic, so without this a failed
// join on the device leaves no trace anyone can read (owner report
// 2026-10-20, "Connection timeout [8 ASSOC_LEAVE]"; see
// docs/saved-wifi-join-2026-10-20.md). The caller must hold its RenderLock:
// the card shares the SPI bus with the panel.
//
// `associated` is the state at the moment of failure, read by the caller
// BEFORE it disconnected: the disconnect's own event clears isAssociated()
// before this runs (adversarial review 2026-10-20).
void writeFailureLog(const char* what, bool associated);

// Short name for an IDF WIFI_REASON_* code ("AUTH_FAIL", ...), "?" if unknown.
// Returned pointers are string literals (flash-resident).
const char* reasonName(uint8_t reason);

}  // namespace WifiDiagnostics
