// Host WiFi double: only what the activities linked here ask of it.
// LibraryUpdateActivity::onEnter() answers NO_WIFI from WiFi.status(), and
// src/network/SavedWifiJoin.h (since d1f0f33cc, 2026-10-20) also compiles
// against mode/begin/disconnect/scan settings/hostByName. Those are inert here:
// nothing in the suite drives a join, and the resolver answers while connected.
//
// A global named WiFi, like the Arduino one, so the activity source includes
// <WiFi.h> unchanged; stubs/ is first on the include path.
#pragma once

#include <cstdint>

enum wl_status_t { WL_IDLE_STATUS = 0, WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_DISCONNECTED = 6 };
enum wifi_mode_t { WIFI_OFF = 0, WIFI_STA = 1 };
enum wifi_scan_method_t { WIFI_FAST_SCAN = 0, WIFI_ALL_CHANNEL_SCAN = 1 };
enum wifi_sort_method_t { WIFI_CONNECT_AP_BY_SIGNAL = 0, WIFI_CONNECT_AP_BY_SECURITY = 1 };

struct IPAddress {
  uint32_t addr = 0;
};

class HostWiFi {
 public:
  wl_status_t status() const { return connected_ ? WL_CONNECTED : WL_DISCONNECTED; }

  bool mode(wifi_mode_t) { return true; }
  void persistent(bool) {}
  bool disconnect(bool = false, bool = false) { return true; }
  void setScanMethod(wifi_scan_method_t) {}
  void setSortMethod(wifi_sort_method_t) {}
  wl_status_t begin() { return status(); }
  wl_status_t begin(const char*, const char* = nullptr) { return status(); }
  // The resolver answers whenever the radio is up, so a join reaches Ready and
  // the screen under test gets to its first fetch, as on a working network.
  int hostByName(const char*, IPAddress&) { return connected_ ? 1 : 0; }

  // Test seam. Connected by default, since the screen under test is only
  // interesting once it is.
  void simSetConnected(bool connected) { connected_ = connected; }

 private:
  bool connected_ = true;
};

inline HostWiFi WiFi;
