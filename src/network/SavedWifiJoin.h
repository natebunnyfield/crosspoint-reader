#pragma once

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>

#include <string>
#include <vector>

#include "WifiCredentialStore.h"
#include "network/OtaPreflight.h"
#include "network/SavedWifiPlan.h"
#include "network/WifiDiagnostics.h"

// Brings a saved network up from inside a screen, without a prompt and without
// blocking the loop: start() issues the first WiFi.begin() and returns, and
// poll() is called once per loop() until it stops answering Waiting. The
// policy (which network, how long) is network/SavedWifiPlan.h; readiness is
// TWO conditions, an association and a resolver that answers, for the reason
// network/OtaPreflight.h gives.
//
// THE CALLER LOADS THE STORE, under its RenderLock, before start(): nothing
// loads /.crosspoint/wifi.json at boot, and the SD card shares the SPI bus
// with the panel. Likewise the caller records lastSsidToRecord() under the
// lock once poll() says Ready. Header-only so the iOS source list, which is
// generated from the firmware's compile database, needs no regeneration.
class SavedWifiJoin {
 public:
  enum class Result {
    Waiting,         // still joining or resolving; poll again next loop()
    Ready,           // associated AND the host resolves
    NoSavedNetwork,  // nothing on the card to join
    CouldNotJoin,    // every saved network tried; none associated
    NoDns,           // associated, but the resolver never answered
  };

  void start(const char* host) {
    resolveHost = host;
    broughtUp = false;
    order.clear();
    index = 0;
    linkMs = 0;
    lastDnsTryMs = 0;
    recordSsid.clear();
    searchStartMs = millis();

    if (WiFi.status() == WL_CONNECTED) {
      LOG_INF(TAG, "Already associated; waiting for the resolver");
      linkMs = millis();
      return;
    }

    std::vector<std::string> saved;
    const size_t count = WIFI_STORE.getCredentialCount();
    saved.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      if (auto ssid = WIFI_STORE.getSsidAt(i)) saved.push_back(std::move(*ssid));
    }
    lastSsid = WIFI_STORE.getLastConnectedSsid();
    order = savedwifi::joinOrder(saved, lastSsid);

#ifdef SIMULATOR
    // A host build is already on the host's network, and the simulator's
    // WiFi.begin() reports THAT association whatever it is handed (the same
    // rule ClaudeChat's connectWifi() follows), so an empty card -- a phone's
    // normal state -- must not be turned away. One unnamed candidate.
    if (order.empty()) order.emplace_back();
#endif
    if (order.empty()) {
      LOG_INF(TAG, "No link and no saved network to join");
      return;
    }
    beginAttempt();
  }

  Result poll() {
    if (linkMs == 0) {
      if (order.empty()) return Result::NoSavedNetwork;
      const uint32_t now = millis();
      const savedwifi::Step step = savedwifi::decide(linkState(), now - attemptStartMs, now - searchStartMs,
                                                     order.size(), index);
      switch (step) {
        case savedwifi::Step::Wait:
          return Result::Waiting;
        case savedwifi::Step::TryNext:
          LOG_INF(TAG, "\"%s\" did not come up (reason %s); trying the next saved network", order[index].c_str(),
                  WifiDiagnostics::reasonName(WifiDiagnostics::lastDisconnectReason()));
          WiFi.disconnect();
          ++index;
          beginAttempt();
          return Result::Waiting;
        case savedwifi::Step::GiveUp:
          LOG_ERR(TAG, "Could not join any of %u saved network(s) in %lu ms", static_cast<unsigned>(order.size()),
                  millis() - searchStartMs);
          WiFi.disconnect(true);
          return Result::CouldNotJoin;
        case savedwifi::Step::Linked:
          linkMs = now;
          if (!order[index].empty() && order[index] != lastSsid) recordSsid = order[index];
          LOG_INF(TAG, "Joined %s in %lu ms; waiting for the resolver",
                  order[index].empty() ? "the host's network" : order[index].c_str(), now - searchStartMs);
          break;
      }
    }

    // Associated. Ask the resolver for the host the caller will fetch from,
    // no more often than the retry gap: hostByName() blocks.
    const uint32_t now = millis();
    if (WiFi.status() == WL_CONNECTED && (lastDnsTryMs == 0 || now - lastDnsTryMs >= otapreflight::kDnsRetryMs)) {
      lastDnsTryMs = now;
      IPAddress addr;
      if (WiFi.hostByName(resolveHost, addr) == 1) {
        LOG_INF(TAG, "Resolver answered for %s", resolveHost);
        return Result::Ready;
      }
    }
    if (millis() - linkMs >= otapreflight::kDnsTimeoutMs) {
      LOG_ERR(TAG, "Associated but no DNS for %s after %u ms", resolveHost,
              static_cast<unsigned>(otapreflight::kDnsTimeoutMs));
      return Result::NoDns;
    }
    return Result::Waiting;
  }

  // Puts the radio back the way start() found it: off, if this join is what
  // turned it on. For a screen's onExit(), so a library sync does not leave the
  // radio associated (or rescanning for a network out of range) while the owner
  // goes back to reading. A link that was already up is left alone.
  void release() {
    if (!broughtUp) return;
    broughtUp = false;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  // Non-empty once Ready when the network that came up is not the one recorded
  // as last-connected, so the next join tries it first.
  const std::string& lastSsidToRecord() const { return recordSsid; }

 private:
  static constexpr const char* TAG = "WIFIJOIN";

  savedwifi::Link linkState() const {
    const wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) return savedwifi::Link::Up;
    // WL_NO_SSID_AVAIL is NOT an answer here: the core keeps rescanning after
    // NO_AP_FOUND (arduino-esp32 STA.cpp, a reconnectable reason), and this path,
    // unlike the picker, has not scanned first -- one missed all-channel scan
    // must not end the attempt. It waits out the cap instead.
    if (status == WL_CONNECT_FAILED) return savedwifi::Link::Refused;
    // The SDK retries an auth failure in a loop with the status left at
    // WL_DISCONNECTED; three is a wrong password (WifiSelectionActivity's rule).
    if (WifiDiagnostics::authFailureCount() >= 3) return savedwifi::Link::Refused;
    return savedwifi::Link::Pending;
  }

  void beginAttempt() {
    broughtUp = true;
    const std::string& ssid = order[index];
    WifiDiagnostics::begin();
    WifiDiagnostics::resetCounters();
    WiFi.persistent(false);  // credentials live in WifiCredentialStore, not NVS
    WiFi.mode(WIFI_STA);
    // Cycle the radio before EVERY attempt, the picker's own sequence
    // (WifiSelectionActivity::attemptConnection). The core's status is written
    // only by its event handler and reset only on STA_STOP/STA_START, so without
    // this a WL_CONNECT_FAILED left by the previous network, or by a join a
    // previous screen abandoned, is read as this network's answer on the first
    // poll (adversarial review 2026-10-20).
    WiFi.disconnect(true, true);
    delay(100);
    // Strongest BSSID of a multi-AP network, as the picker does.
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    if (ssid.empty()) {
      LOG_INF(TAG, "Joining the host's network");
      WiFi.begin();
    } else {
      const auto cred = WIFI_STORE.findCredential(ssid);
      LOG_INF(TAG, "Joining saved network \"%s\" (%u of %u)", ssid.c_str(), static_cast<unsigned>(index + 1),
              static_cast<unsigned>(order.size()));
      if (cred && !cred->password.empty()) {
        WiFi.begin(ssid.c_str(), cred->password.c_str());
      } else {
        WiFi.begin(ssid.c_str());
      }
    }
    attemptStartMs = millis();
  }

  const char* resolveHost = "";
  std::vector<std::string> order;
  std::string lastSsid;
  std::string recordSsid;
  size_t index = 0;
  uint32_t searchStartMs = 0;
  uint32_t attemptStartMs = 0;
  uint32_t linkMs = 0;
  uint32_t lastDnsTryMs = 0;
  bool broughtUp = false;
};
