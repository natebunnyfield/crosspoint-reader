#include "WifiDiagnostics.h"

#include <Logging.h>

#ifdef SIMULATOR

// The simulator's WiFi shim has no event API; connection outcomes are driven
// by CROSSPOINT_SIM_WIFI_CONNECT, so there is nothing to observe here.
namespace WifiDiagnostics {
void begin() {}
uint8_t lastDisconnectReason() { return 0; }
uint8_t authFailureCount() { return 0; }
void resetCounters() {}
const char* reasonName(uint8_t) { return "?"; }
}  // namespace WifiDiagnostics

#else

#include <WiFi.h>

namespace {

// Written from the WiFi event task, read from the main loop. Single-byte
// volatile is sufficient: no read-modify-write is shared across tasks
// (resetCounters() races only cost one stale event, not corruption).
volatile uint8_t lastReason = 0;
volatile uint8_t authFailures = 0;
bool handlerRegistered = false;

bool isAuthReason(const uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return true;
    default:
      return false;
  }
}

// WiFiEventSysCb shape (raw function pointer): avoids the std::function
// overload of WiFi.onEvent() and its heap-allocated closure.
void onWifiEvent(arduino_event_t* e) {
  const arduino_event_id_t event = e->event_id;
  const arduino_event_info_t& info = e->event_info;
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      LOG_INF("WIFI", "EVT sta_start");
      break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:
      LOG_INF("WIFI", "EVT scan_done: status=%u found=%u", static_cast<unsigned>(info.wifi_scan_done.status),
              static_cast<unsigned>(info.wifi_scan_done.number));
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      LOG_INF("WIFI", "EVT connected: channel=%u authmode=%u", static_cast<unsigned>(info.wifi_sta_connected.channel),
              static_cast<unsigned>(info.wifi_sta_connected.authmode));
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      LOG_INF("WIFI", "EVT got_ip");
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      LOG_INF("WIFI", "EVT lost_ip");
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      const uint8_t reason = info.wifi_sta_disconnected.reason;
      lastReason = reason;
      if (isAuthReason(reason) && authFailures < UINT8_MAX) {
        authFailures = authFailures + 1;
      }
      LOG_INF("WIFI", "EVT disconnected: reason=%u (%s)", static_cast<unsigned>(reason),
              WifiDiagnostics::reasonName(reason));
      break;
    }
    default:
      break;
  }
}

}  // namespace

namespace WifiDiagnostics {

void begin() {
  if (handlerRegistered) {
    return;
  }
  handlerRegistered = true;
  WiFi.onEvent(onWifiEvent);
}

uint8_t lastDisconnectReason() { return lastReason; }

uint8_t authFailureCount() { return authFailures; }

void resetCounters() {
  lastReason = 0;
  authFailures = 0;
}

const char* reasonName(const uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_UNSPECIFIED:
      return "UNSPECIFIED";
    case WIFI_REASON_AUTH_EXPIRE:
      return "AUTH_EXPIRE";
    case WIFI_REASON_AUTH_LEAVE:
      return "AUTH_LEAVE";
    case WIFI_REASON_ASSOC_EXPIRE:
      return "ASSOC_EXPIRE";
    case WIFI_REASON_ASSOC_TOOMANY:
      return "ASSOC_TOOMANY";
    case WIFI_REASON_ASSOC_LEAVE:
      return "ASSOC_LEAVE";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
      return "4WAY_HANDSHAKE_TIMEOUT";
    case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
      return "GROUP_KEY_UPDATE_TIMEOUT";
    case WIFI_REASON_802_1X_AUTH_FAILED:
      return "802_1X_AUTH_FAILED";
    case WIFI_REASON_BEACON_TIMEOUT:
      return "BEACON_TIMEOUT";
    case WIFI_REASON_NO_AP_FOUND:
      return "NO_AP_FOUND";
    case WIFI_REASON_AUTH_FAIL:
      return "AUTH_FAIL";
    case WIFI_REASON_ASSOC_FAIL:
      return "ASSOC_FAIL";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return "HANDSHAKE_TIMEOUT";
    case WIFI_REASON_CONNECTION_FAIL:
      return "CONNECTION_FAIL";
    case WIFI_REASON_AP_TSF_RESET:
      return "AP_TSF_RESET";
    case WIFI_REASON_ROAMING:
      return "ROAMING";
    default:
      return "?";
  }
}

}  // namespace WifiDiagnostics

#endif  // SIMULATOR
