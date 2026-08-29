#pragma once

#include <functional>
#include <memory>
#include <string>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "network/CrossPointWebServer.h"

enum class NetworkMode { JOIN_NETWORK, CREATE_HOTSPOT };

// Web server activity states
enum class WebServerActivityState {
  MODE_SELECTION,  // Choosing between Join Network and Create Hotspot
  WIFI_SELECTION,  // WiFi selection subactivity is active (for Join Network mode)
  AP_STARTING,     // Starting Access Point mode
  SERVER_RUNNING,  // Web server is running and handling requests
  SHUTTING_DOWN    // Shutting down server and WiFi
};

/**
 * CrossPointWebServerActivity is the entry point for file transfer functionality.
 * It:
 * - First presents a choice between "Join a Network" (STA) and "Create Hotspot" (AP)
 * - For STA mode: Launches WifiSelectionActivity to connect to an existing network
 * - For AP mode: Creates an Access Point that clients can connect to
 * - Starts the CrossPointWebServer when connected
 * - Handles client requests in its loop() function
 * - Cleans up the server and shuts down WiFi on exit
 */
class CrossPointWebServerActivity final : public Activity {
  WebServerActivityState state = WebServerActivityState::MODE_SELECTION;

  // Network mode
  NetworkMode networkMode = NetworkMode::JOIN_NETWORK;
  bool isApMode = false;

  // In-place replacement for the old NetworkModeSelectionActivity screen.
  OptionPopup networkModePopup;

  // Web server - owned by this activity
  std::unique_ptr<CrossPointWebServer> webServer;

  // Server status
  std::string connectedIP;
  std::string connectedSSID;  // For STA mode: network name, For AP mode: AP name

  // Performance monitoring
  unsigned long lastHandleClientTime = 0;

  // Sustained WiFi-loss tracking; abandon only after WIFI_ABANDON_MS.
  int consecutiveDisconnects = 0;
  unsigned long firstDisconnectAt = 0;
  static constexpr unsigned long WIFI_ABANDON_MS = 5UL * 60UL * 1000UL;

  // Cached signal-strength bracket (0..4) for the WiFi indicator.
  int lastWifiBars = 0;

  void renderServerRunning() const;
  void renderWifiIndicator(int subHeaderTop) const;

  // Absent under CROSSPOINT_NO_SOFTAP: with Create Hotspot gone there is one
  // mode left and onEnter() goes straight to it. The popup MEMBER stays, so the
  // state enum and the two OptionPopup call sites keep one shape across builds.
#ifndef CROSSPOINT_NO_SOFTAP
  void showNetworkModePopup();
#endif
  void onNetworkModeSelected(NetworkMode mode);
  void onWifiSelectionComplete(bool connected);
  void startAccessPoint();
  void startWebServer();

 public:
  explicit CrossPointWebServerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CrossPointWebServer", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return webServer && webServer->isRunning(); }
  bool preventAutoSleep() override { return webServer && webServer->isRunning(); }
};
