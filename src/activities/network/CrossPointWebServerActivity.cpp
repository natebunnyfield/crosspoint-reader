#include "CrossPointWebServerActivity.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstddef>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#ifndef CROSSPOINT_NO_DEVICE_FLASH
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "network/PendingFirmware.h"
#endif
#include "WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/DeviceId.h"
#include "util/QrUtils.h"
#include "util/TaskWatchdog.h"

namespace {
// AP Mode configuration
constexpr const char* AP_SSID = "CrossPoint-Reader";
constexpr const char* AP_PASSWORD = nullptr;  // Open network for ease of use
constexpr const char* AP_HOSTNAME = "crosspoint";
constexpr uint8_t AP_CHANNEL = 1;
constexpr uint8_t AP_MAX_CONNECTIONS = 4;
constexpr int QR_CODE_WIDTH = 198;
constexpr int QR_CODE_HEIGHT = 198;

// "Device ID: <6 hex chars>" — the unit id per-device SD files use
// (/sleep_<id>.bmp), surfaced on the screen users look at while syncing.
std::string deviceIdLine() {
  char id[8];
  getDeviceIdHex(id, sizeof(id));
  return std::string(tr(STR_DEVICE_ID_PREFIX)) + id;
}

// DNS server for captive portal (redirects all DNS queries to our IP)
DNSServer* dnsServer = nullptr;
constexpr uint16_t DNS_PORT = 53;

void stopDnsServer() {
  if (!dnsServer) return;

  dnsServer->stop();
  delete dnsServer;
  dnsServer = nullptr;
}

void restartMdns(const char* hostname, const char* tag) {
  MDNS.end();
  if (MDNS.begin(hostname)) {
    LOG_DBG(tag, "mDNS started: http://%s.local/", hostname);
  } else {
    LOG_DBG(tag, "WARNING: mDNS failed to start");
  }
}

// 0..4 bars from RSSI (dBm), with 3 dBm hysteresis on currentBars to suppress flicker.
int barsForRssi(int rssi, int currentBars) {
  static constexpr int RISE_DBM[] = {-85, -75, -65, -55};
  static constexpr int FALL_DBM[] = {-88, -78, -68, -58};
  int bars = std::clamp(currentBars, 0, 4);
  while (bars < 4 && rssi >= RISE_DBM[bars]) bars++;
  while (bars > 0 && rssi < FALL_DBM[bars - 1]) bars--;
  return bars;
}
}  // namespace

void CrossPointWebServerActivity::onEnter() {
  Activity::onEnter();

  LOG_DBG("WEBACT", "Free heap at onEnter: %d bytes", ESP.getFreeHeap());

  // Heap-critical transition: WiFi (~45KB) plus the web server have to fit in
  // what's left of the ~380KB parts. SD-font caches retained for the CJK UI
  // fallback (mini glyph/kern arenas, kern class tables) are rebuildable on
  // demand — release them up front instead of aborting in startWebServer()
  // when the heap comes up short (observed on X3 with a Korean SD font).
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->releaseSdFontCaches();
    LOG_DBG("WEBACT", "Free heap after SD font cache release: %d bytes", ESP.getFreeHeap());
  }

  // Reset state
  state = WebServerActivityState::MODE_SELECTION;
  networkMode = NetworkMode::JOIN_NETWORK;
  isApMode = false;
  connectedIP.clear();
  connectedSSID.clear();
  lastHandleClientTime = 0;

#ifdef CROSSPOINT_NO_SOFTAP
  // ONE MODE LEFT, SO THERE IS NO CHOICE TO PRESENT (owner ruling, ios/WIFI.md
  // Phase 4). Create Hotspot needs an 802.11 radio the host will not hand over
  // -- an app cannot bring up an access point on iOS at any entitlement tier,
  // and WiFiClass::softAP() reports that by returning false. A popup offering
  // one option is a keypress that can only go one way, and a one-row version of
  // this screen is not parity with an X3 either: parity ended the moment the
  // second row went.
  //
  // Back out of the WiFi list therefore goes Home rather than here; see
  // onWifiSelectionComplete().
  //
  // The state is moved off MODE_SELECTION BEFORE the dispatch, not by it.
  // `currentActivity` is assigned and the render lock released before
  // onEnter() runs (ActivityManager.cpp), so a render notification left over
  // from the outgoing screen can land in the window between the reset block
  // above and onNetworkModeSelected() reaching its own assignment -- and
  // render()'s MODE_SELECTION branch would then draw a bare "File Transfer"
  // header over a popup this build never shows. Microseconds and cosmetic, but
  // it costs one line to make the state unobservable instead of merely brief.
  LOG_DBG("WEBACT", "Single network mode on this build; joining directly");
  state = WebServerActivityState::WIFI_SELECTION;
  onNetworkModeSelected(NetworkMode::JOIN_NETWORK);
#else
  // Show the network mode choice in place, rather than pushing a whole activity for it.
  LOG_DBG("WEBACT", "Showing network mode popup...");
  showNetworkModePopup();
  requestUpdate();
#endif
}

void CrossPointWebServerActivity::onExit() {
  Activity::onExit();

  LOG_DBG("WEBACT", "Free heap at onExit start: %d bytes", ESP.getFreeHeap());

  state = WebServerActivityState::SHUTTING_DOWN;
  stopDnsServer();
  MDNS.end();

  // Skip reboot if WiFi was never activated (e.g. user backed out of mode selection).
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    if (isApMode) {
      WiFi.softAPdisconnect(true);
    } else {
      WiFi.disconnect(false);
    }
    delay(30);
    silentRestart();
  }

  LOG_DBG("WEBACT", "Free heap at onExit end: %d bytes", ESP.getFreeHeap());
}

void CrossPointWebServerActivity::onNetworkModeSelected(const NetworkMode mode) {
  const char* modeName = mode == NetworkMode::CREATE_HOTSPOT ? "Create Hotspot" : "Join Network";
  LOG_DBG("WEBACT", "Network mode selected: %s", modeName);

  networkMode = mode;
  isApMode = (mode == NetworkMode::CREATE_HOTSPOT);

  if (mode == NetworkMode::JOIN_NETWORK) {
    // STA mode - launch WiFi selection
    LOG_DBG("WEBACT", "Turning on WiFi (STA mode)...");
    WiFi.mode(WIFI_STA);

    state = WebServerActivityState::WIFI_SELECTION;
    LOG_DBG("WEBACT", "Launching WifiSelectionActivity...");
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& wifi = std::get<WifiResult>(result.data);
                               connectedIP = wifi.ip;
                               connectedSSID = wifi.ssid;
                             }
                             onWifiSelectionComplete(!result.isCancelled);
                           });
  } else {
    // AP mode - start access point
    state = WebServerActivityState::AP_STARTING;
    requestUpdate();
    startAccessPoint();
  }
}

void CrossPointWebServerActivity::onWifiSelectionComplete(const bool connected) {
  LOG_DBG("WEBACT", "WifiSelectionActivity completed, connected=%d", connected);

  if (connected) {
    // Get connection info before exiting subactivity
    isApMode = false;

    // Start mDNS for hostname resolution
    restartMdns(AP_HOSTNAME, "WEBACT");

    // Start the web server
    startWebServer();
  } else {
#ifdef CROSSPOINT_NO_SOFTAP
    // Nothing to go back TO -- the mode popup this build skips was the only
    // thing between the WiFi list and Home. Leaving instead of re-entering the
    // list is what makes Back on that screen mean Back.
    //
    // TURN THE RADIO BACK OFF FIRST, or opening File Transfer and immediately
    // changing your mind RESTARTS THE APP. onExit()'s guard is
    // `WiFi.getMode() != WIFI_MODE_NULL`, and skipping the mode popup means
    // onEnter() now always reaches WiFi.mode(WIFI_STA) -- so a cancel that used
    // to leave the mode NULL and skip silentRestart() would instead take the
    // in-process longjmp reboot for having done nothing at all. Nothing else on
    // this path clears it: disconnect(false) does not, and
    // WifiSelectionActivity::onExit deliberately leaves WiFi state to its
    // parent. This restores exactly the pre-gate behavior for "opened, backed
    // out, never connected", and leaves the post-transfer reboot -- the one
    // ios/WIFI.md finding 8 asks for -- untouched.
    WiFi.mode(WIFI_OFF);
    onGoHome();
#else
    // User cancelled - go back to mode selection
    state = WebServerActivityState::MODE_SELECTION;
    showNetworkModePopup();
    requestUpdate();
#endif
  }
}

#ifndef CROSSPOINT_NO_SOFTAP
void CrossPointWebServerActivity::showNetworkModePopup() {
  static constexpr StrId MODE_OPTIONS[] = {StrId::STR_JOIN_NETWORK, StrId::STR_CREATE_HOTSPOT};
  networkModePopup.show(StrId::STR_FILE_TRANSFER, MODE_OPTIONS, 2, 0, [this](int index) {
    onNetworkModeSelected(index == 0 ? NetworkMode::JOIN_NETWORK : NetworkMode::CREATE_HOTSPOT);
  });
}
#endif

void CrossPointWebServerActivity::startAccessPoint() {
  LOG_DBG("WEBACT", "Starting Access Point mode...");
  LOG_DBG("WEBACT", "Free heap before AP start: %d bytes", ESP.getFreeHeap());

  // Configure and start the AP
  WiFi.mode(WIFI_AP);
  delay(100);

  // Start soft AP
  bool apStarted;
  if (AP_PASSWORD && strlen(AP_PASSWORD) >= 8) {
    apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CONNECTIONS);
  } else {
    // Open network (no password)
    apStarted = WiFi.softAP(AP_SSID, nullptr, AP_CHANNEL, false, AP_MAX_CONNECTIONS);
  }

  if (!apStarted) {
    LOG_ERR("WEBACT", "ERROR: Failed to start Access Point!");
    onGoHome();
    return;
  }

  delay(100);  // Wait for AP to fully initialize

  // Get AP IP address
  const IPAddress apIP = WiFi.softAPIP();
  char ipStr[16];
  snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", apIP[0], apIP[1], apIP[2], apIP[3]);
  connectedIP = ipStr;
  connectedSSID = AP_SSID;

  LOG_DBG("WEBACT", "Access Point started!");
  LOG_DBG("WEBACT", "SSID: %s", AP_SSID);
  LOG_DBG("WEBACT", "IP: %s", connectedIP.c_str());

  // Start mDNS for hostname resolution
  restartMdns(AP_HOSTNAME, "WEBACT");

  // Start DNS server for captive portal behavior
  // This redirects all DNS queries to our IP, making any domain typed resolve to us
  stopDnsServer();
  // nothrow: a bare new aborts on OOM, and the captive portal is a convenience.
  // Without it the AP still works -- the owner types the IP instead of being
  // redirected -- so a failure here is logged and skipped, not fatal.
  dnsServer = new (std::nothrow) DNSServer();
  if (dnsServer) {
    dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer->start(DNS_PORT, "*", apIP);
    LOG_DBG("WEBACT", "DNS server started for captive portal");
  } else {
    LOG_ERR("WEBACT", "OOM: DNS server; captive portal redirect is off");
  }

  LOG_DBG("WEBACT", "Free heap after AP start: %d bytes", ESP.getFreeHeap());

  // Start the web server
  startWebServer();
}

void CrossPointWebServerActivity::startWebServer() {
  LOG_DBG("WEBACT", "Starting web server...");

  // Repeat the release right before the allocation: the WiFi selection screen
  // rendered since onEnter(), and a CJK SSID repopulates the SD-font caches.
  if (auto* fcm = renderer.getFontCacheManager()) {
    LOG_DBG("WEBACT", "Free heap before SD font cache release: %d bytes", ESP.getFreeHeap());
    fcm->releaseSdFontCaches();
    LOG_DBG("WEBACT", "Free heap before server alloc: %d bytes", ESP.getFreeHeap());
  }

  // Create the web server instance. nothrow, and checked: this is the allocation
  // the SD font cache was just released FOR, so it is the one most likely to
  // fail -- and aborting here would take the device down at the exact moment the
  // owner asked for File Transfer.
  webServer = makeUniqueNoThrow<CrossPointWebServer>();
  if (!webServer) {
    // Same exit the failed-to-start branch below takes -- there is no separate
    // error state, and inventing one would leave a screen nothing dismisses.
    LOG_ERR("WEBACT", "OOM: web server instance");
    onGoHome();
    return;
  }
  webServer->begin();

  if (webServer->isRunning()) {
    state = WebServerActivityState::SERVER_RUNNING;
    LOG_DBG("WEBACT", "Web server started successfully");
    lastWifiBars = isApMode ? 0 : barsForRssi(WiFi.RSSI(), 0);

    // Force an immediate render since we're transitioning from a subactivity
    // that had its own rendering task. We need to make sure our display is shown.
    requestUpdate();
  } else {
    LOG_ERR("WEBACT", "ERROR: Failed to start web server!");
    webServer.reset();
    // Go back on error
    onGoHome();
  }
}

void CrossPointWebServerActivity::loop() {
#ifndef CROSSPOINT_NO_SOFTAP
  if (state == WebServerActivityState::MODE_SELECTION) {
    networkModePopup.handleInput(mappedInput, [this] { requestUpdate(); });
    if (!networkModePopup.isActive() && state == WebServerActivityState::MODE_SELECTION) {
      // Dismissed (Back / tap outside) without a selection -- same cancel path
      // the old NetworkModeSelectionActivity took.
      onGoHome();
    }
    return;
  }
#endif

  // Handle different states
  if (state == WebServerActivityState::SERVER_RUNNING) {
    // A firmware image arrived over WebDAV. The PUT handler only recorded it --
    // flashing there would have killed the connection mid-response -- so the
    // hand-off completes here, one tick later, with the 201 already sent.
    //
    // It goes through the normal confirm-then-flash activity rather than
    // flashing outright: the transfer server has no authentication and AP mode
    // is an open network, so a press on the device is the only thing standing
    // between "anyone in radio range" and permanently running their own code
    // on it. take() clears the slot, so a decline does not re-prompt forever.
    // Not on iOS: SdFirmwareUpdateActivity.cpp is excluded from that build
    // (cmake/CrossPointIOSExclusions.cmake) because a phone has no OTA
    // partition to flash, so calling it here would be an undefined symbol at
    // link time -- which is exactly how this guard came to be written.
#ifndef CROSSPOINT_NO_DEVICE_FLASH
    if (pending_firmware::waiting()) {
      const std::string image = pending_firmware::take();
      LOG_INF("WEBACT", "offering uploaded firmware: %s", image.c_str());
      startActivityForResult(std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInput, false, image),
                             [](const ActivityResult&) {});
      return;
    }
#endif

    // Handle DNS requests for captive portal (AP mode only)
    if (isApMode && dnsServer) {
      dnsServer->processNextRequest();
    }

    // STA mode: Monitor WiFi connection health
    if (!isApMode && webServer && webServer->isRunning()) {
      static unsigned long lastWifiCheck = 0;
      if (millis() - lastWifiCheck > 2000) {  // Check every 2 seconds
        lastWifiCheck = millis();
        const wl_status_t wifiStatus = WiFi.status();
        // Driver auto-reconnect handles retries; abandon (via onGoHome) only
        // after WIFI_ABANDON_MS, otherwise the activity freezes on a blip.
        bool repaint = false;
        if (wifiStatus != WL_CONNECTED) {
          if (consecutiveDisconnects == 0) {
            firstDisconnectAt = millis();
            repaint = true;
          }
          consecutiveDisconnects++;
          LOG_DBG("WEBACT", "WiFi not connected (status=%d, consecutive=%d, total=%lu ms)", wifiStatus,
                  consecutiveDisconnects, millis() - firstDisconnectAt);
          if (millis() - firstDisconnectAt > WIFI_ABANDON_MS) {
            LOG_DBG("WEBACT", "WiFi unavailable for >%lu s; returning to network selection", WIFI_ABANDON_MS / 1000UL);
            state = WebServerActivityState::SHUTTING_DOWN;
            onGoHome();
            return;
          }
        } else {
          if (consecutiveDisconnects > 0) {
            LOG_DBG("WEBACT", "WiFi recovered after %d failed checks (%lu ms)", consecutiveDisconnects,
                    millis() - firstDisconnectAt);
            repaint = true;
          }
          consecutiveDisconnects = 0;
          firstDisconnectAt = 0;
          const int rssi = WiFi.RSSI();
          if (rssi < -75) {
            LOG_DBG("WEBACT", "Warning: Weak WiFi signal: %d dBm", rssi);
          }
          const int bars = barsForRssi(rssi, lastWifiBars);
          if (bars != lastWifiBars) {
            lastWifiBars = bars;
            repaint = true;
          }
        }
        if (repaint) requestUpdate();
      }
    }

    // Handle web server requests - maximize throughput with watchdog safety
    if (webServer && webServer->isRunning()) {
      const unsigned long timeSinceLastHandleClient = millis() - lastHandleClientTime;

      // Log if there's a significant gap between handleClient calls (>100ms)
      if (lastHandleClientTime > 0 && timeSinceLastHandleClient > 100) {
        LOG_DBG("WEBACT", "WARNING: %lu ms gap since last handleClient", timeSinceLastHandleClient);
      }

      // Reset watchdog BEFORE processing - HTTP header parsing can be slow
      resetTaskWatchdogIfSubscribed();

      // Process HTTP requests in tight loop for maximum throughput
      // More iterations = more data processed per main loop cycle.
      //
      // No input polling in here. The old shape called mappedInput.update()
      // every 64 iterations "for responsiveness", but update() recomputes and
      // CLEARS the frame's press/release edges — a mid-frame call destroyed
      // them for every consumer outside this loop (main.cpp's activity check,
      // the FORCE_REFRESH power-release read; input-edge audit 2026-08-21,
      // finding 7). This loop only ever needed Back, and Back is read by the
      // per-frame check below; the batch is time-bounded instead, so under a
      // busy transfer the main loop gets control back within ~MAX_BATCH_MS and
      // the Back check runs no later than the old every-64-busy-iterations
      // cadence, while an idle pass still runs its full MAX_ITERATIONS.
      constexpr int MAX_ITERATIONS = 500;
      constexpr unsigned long MAX_BATCH_MS = 100;
      const unsigned long batchStart = millis();
      for (int i = 0; i < MAX_ITERATIONS && webServer->isRunning(); i++) {
        webServer->handleClient();
        // Reset watchdog every 32 iterations
        if ((i & 0x1F) == 0x1F) {
          resetTaskWatchdogIfSubscribed();
        }
        // Yield every 64 iterations; hand the batch back to the main loop once
        // it has run long enough that input may be waiting on it.
        if ((i & 0x3F) == 0x3F) {
          yield();
          if (millis() - batchStart >= MAX_BATCH_MS) break;
        }
      }
      lastHandleClientTime = millis();
    }

    // Handle exit on Back button (per-frame edge from the main loop's update)
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      onGoHome();
      return;
    }
  }
}

void CrossPointWebServerActivity::render(RenderLock&&) {
  if (state == WebServerActivityState::MODE_SELECTION) {
    renderer.clearScreen();
    const auto& metrics = UITheme::getInstance().getMetrics();
    const auto pageWidth = renderer.getScreenWidth();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FILE_TRANSFER));
    if (networkModePopup.processRender(renderer, mappedInput)) return;
    renderer.displayBuffer();
    return;
  }

  // Only render our own UI when server is running
  // Subactivities handle their own rendering
  if (state == WebServerActivityState::SERVER_RUNNING || state == WebServerActivityState::AP_STARTING) {
    renderer.clearScreen();
    const auto& metrics = UITheme::getInstance().getMetrics();
    const auto pageWidth = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();

    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   isApMode ? tr(STR_HOTSPOT_MODE) : tr(STR_FILE_TRANSFER), nullptr);

    if (state == WebServerActivityState::SERVER_RUNNING) {
      GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                        connectedSSID.c_str());
      renderServerRunning();
    } else {
      const auto height = renderer.getLineHeight(UI_10_FONT_ID);
      const auto top = (pageHeight - height) / 2;
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_STARTING_HOTSPOT));
    }
    renderer.displayBuffer();
  }
}

void CrossPointWebServerActivity::renderServerRunning() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 isApMode ? tr(STR_HOTSPOT_MODE) : tr(STR_FILE_TRANSFER), nullptr);
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    connectedSSID.c_str());

  if (!isApMode) {
    renderWifiIndicator(metrics.topPadding + metrics.headerHeight);
  }

  int startY = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing * 2;
  int height10 = renderer.getLineHeight(UI_10_FONT_ID);
  if (isApMode) {
    // AP mode display
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, startY, tr(STR_CONNECT_WIFI_HINT), true,
                      EpdFontFamily::BOLD);
    startY += height10 + metrics.verticalSpacing * 2;

    // Show QR code for Wifi
    // follows spec at https://github.com/zxing/zxing/wiki/Barcode-Contents#wi-fi-network-config-android-ios-11
    const std::string wifiConfig = std::string("WIFI:T:nopass;S:") + connectedSSID + ";;";
    const Rect qrBoundsWifi(metrics.contentSidePadding, startY, QR_CODE_WIDTH, QR_CODE_HEIGHT);
    QrUtils::drawQrCode(renderer, qrBoundsWifi, wifiConfig);

    // Show network name
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding + QR_CODE_WIDTH + metrics.verticalSpacing, startY + 80,
                      connectedSSID.c_str());

    startY += QR_CODE_HEIGHT + 2 * metrics.verticalSpacing;

    // Show primary URL (hostname)
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, startY, tr(STR_OPEN_URL_HINT), true,
                      EpdFontFamily::BOLD);
    startY += height10 + metrics.verticalSpacing * 2;

    std::string hostnameUrl = std::string("http://") + AP_HOSTNAME + ".local/";
    std::string ipUrl = tr(STR_OR_HTTP_PREFIX) + connectedIP + "/";

    // Show QR code for URL
    const Rect qrBoundsUrl(metrics.contentSidePadding, startY, QR_CODE_WIDTH, QR_CODE_HEIGHT);
    QrUtils::drawQrCode(renderer, qrBoundsUrl, hostnameUrl);

    // Show IP address as fallback
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding + QR_CODE_WIDTH + metrics.verticalSpacing, startY + 80,
                      hostnameUrl.c_str());
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding + QR_CODE_WIDTH + metrics.verticalSpacing, startY + 100,
                      ipUrl.c_str());
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding + QR_CODE_WIDTH + metrics.verticalSpacing, startY + 120,
                      deviceIdLine().c_str());
  } else {
    startY += metrics.verticalSpacing * 2;

    // STA mode display (original behavior)
    // std::string ipInfo = "IP Address: " + connectedIP;
    renderer.drawCenteredText(UI_10_FONT_ID, startY, tr(STR_OPEN_URL_HINT), true, EpdFontFamily::BOLD);
    startY += height10;
    renderer.drawCenteredText(UI_10_FONT_ID, startY, tr(STR_SCAN_QR_HINT), true, EpdFontFamily::BOLD);
    startY += height10 + metrics.verticalSpacing * 2;

    // Show QR code for URL
    std::string webInfo = "http://" + connectedIP + "/";
    const Rect qrBounds((pageWidth - QR_CODE_WIDTH) / 2, startY, QR_CODE_WIDTH, QR_CODE_HEIGHT);
    QrUtils::drawQrCode(renderer, qrBounds, webInfo);
    startY += QR_CODE_HEIGHT + metrics.verticalSpacing * 2;

    // Show web server URL prominently
    renderer.drawCenteredText(UI_10_FONT_ID, startY, webInfo.c_str(), true);
    startY += height10 + 5;

    // Also show hostname URL
    std::string hostnameUrl = std::string(tr(STR_OR_HTTP_PREFIX)) + AP_HOSTNAME + ".local/";
    renderer.drawCenteredText(SMALL_FONT_ID, startY, hostnameUrl.c_str(), true);
    startY += renderer.getLineHeight(SMALL_FONT_ID) + 5;

    // Unit ID for per-device SD files (/sleep_<id>.bmp) — shown here because
    // this screen is where card syncs happen.
    renderer.drawCenteredText(SMALL_FONT_ID, startY, deviceIdLine().c_str(), true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void CrossPointWebServerActivity::renderWifiIndicator(int subHeaderTop) const {
  constexpr int BAR_COUNT = 4;
  constexpr int BAR_WIDTH = 4;
  constexpr int BAR_GAP = 2;
  constexpr int ICON_HEIGHT = 14;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int iconWidth = BAR_COUNT * BAR_WIDTH + (BAR_COUNT - 1) * BAR_GAP;
  const int iconRight = renderer.getScreenWidth() - metrics.contentSidePadding;
  const int iconLeft = iconRight - iconWidth;
  const int iconBottom = subHeaderTop + metrics.tabBarHeight - metrics.verticalSpacing;

  const bool wifiUp = (WiFi.status() == WL_CONNECTED) && (consecutiveDisconnects == 0);
  if (wifiUp) {
    for (int i = 0; i < BAR_COUNT; i++) {
      const int barHeight = (i + 1) * ICON_HEIGHT / BAR_COUNT;
      const int x = iconLeft + i * (BAR_WIDTH + BAR_GAP);
      const int y = iconBottom - barHeight;
      if (i < lastWifiBars) {
        renderer.fillRect(x, y, BAR_WIDTH, barHeight, true);
      } else {
        renderer.drawRect(x, y, BAR_WIDTH, barHeight, true);
      }
    }
  } else {
    const int xSize = ICON_HEIGHT;
    const int x0 = iconRight - xSize;
    const int y0 = iconBottom - xSize;
    renderer.drawLine(x0, y0, x0 + xSize, y0 + xSize, 2, true);
    renderer.drawLine(x0, y0 + xSize, x0 + xSize, y0, 2, true);
  }
}
