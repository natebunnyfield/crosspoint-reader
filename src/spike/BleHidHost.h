// SPIKE — throwaway. Measures the cost of a resident BLE HID (HOGP) host.
// Never merged to main; see docs/manage-files.md "v2 plan".
//
// Uses the raw ESP-IDF NimBLE C API rather than NimBLE-Arduino: the host is
// already linked into the stock arduino-esp32 libbt.a for the C3
// (CONFIG_BT_NIMBLE_ENABLED=y / ROLE_CENTRAL=y / GATT_CLIENT=y), so this adds
// no library and needs no platformio.ini edit — which matters here because any
// platformio.ini change wipes every env's build dir.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace blespike {

enum class State : uint8_t {
  Off,          // nimble_port_init() not called yet — no BT RAM claimed
  Starting,     // host task up, waiting for controller sync
  Scanning,     // GAP discovery running, looking for a 0x1812 advertiser
  Connecting,   // ble_gap_connect() in flight
  Securing,     // bonding / encryption
  Discovering,  // GATT: HID service, Report chars, CCCDs
  Ready,        // subscribed, keystrokes flowing
  Failed,
};

// Brings up the controller + NimBLE host and starts scanning. Idempotent.
// Returns false if the stack failed to start.
bool begin();

// Tears the whole stack back down (nimble_port_deinit) and returns the RAM.
void end();

State state();
const char* stateName();

// Peer name from the advertisement, or "" until one is found.
const char* peerName();

// Single-producer (NimBLE host task) / single-consumer (main task) ring of
// decoded characters. Returns -1 when empty. '\n' = Enter, '\b' = Backspace.
int popChar();

// millis() stamp taken inside the notify callback for the character most
// recently returned by popChar(). Used to time keystroke -> glyph.
uint32_t lastKeyMillis();

// Diagnostics for the on-screen status line.
uint32_t notifyCount();
uint32_t droppedCount();

}  // namespace blespike
