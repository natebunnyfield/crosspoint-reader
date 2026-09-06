// Host WiFi double: only what the activities linked here ask of it, which is
// WiFi.status(). LibraryUpdateActivity::onEnter() answers NO_WIFI from it and
// nothing else in the suite reaches the radio.
//
// A global named WiFi, like the Arduino one, so the activity source includes
// <WiFi.h> unchanged; stubs/ is first on the include path.
#pragma once

enum wl_status_t { WL_IDLE_STATUS = 0, WL_CONNECTED = 3, WL_DISCONNECTED = 6 };

class HostWiFi {
 public:
  wl_status_t status() const { return connected_ ? WL_CONNECTED : WL_DISCONNECTED; }

  // Test seam. Connected by default, since the screen under test is only
  // interesting once it is.
  void simSetConnected(bool connected) { connected_ = connected; }

 private:
  bool connected_ = true;
};

inline HostWiFi WiFi;
