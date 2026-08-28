#pragma once

#include <cstdint>

// GETTING TO A USABLE NETWORK BEFORE THE UPDATE CHECK RUNS.
//
// Owner 2026-08-28: "improve update firmware to automatically connect to last
// used wifi network and not get any 'github.com not found' or other errors."
//
// TWO SEPARATE FAILURES SHARE ONE SCREEN, and the second is the one that reads
// as a bug rather than as a limitation.
//
//  1. NO LINK. OnlineFirmwareUpdateActivity used to answer WL_CONNECTED == false
//     with "no wifi, go to Settings" and stop. Deliberate at the time -- joining
//     a network is Settings' job -- but the device already knows the last
//     network it was on and has the password saved, so refusing to use them
//     made the owner walk to another screen to supply something already stored.
//
//  2. LINK BUT NO DNS. This is where "github.com not found" comes from, and why
//     the first failure's fix alone would not have fixed it. `WL_CONNECTED`
//     means ASSOCIATED. DHCP may not have returned yet, and the resolver
//     addresses arrive with the lease -- so there is a window, right after a
//     join and again after a wake, in which the status says connected and every
//     lookup fails. The fetch then reports the host name it could not resolve,
//     which reads as "GitHub is down" and is really "we asked too early".
//
// So readiness is TWO conditions, not one, and the check must not start until
// both hold. That is the whole model here.
//
// Pure and clock-free: the caller supplies elapsed milliseconds and the two
// observations. Every failure mode of this logic is a screen that either hangs
// forever or gives up while the network was still coming up, and neither is
// visible in a compile.
namespace otapreflight {

// How long to wait for an association after WiFi.begin(). Chosen against the
// ESP32 behaviour the rest of this firmware already assumes: WifiSelection's
// own join budget is the same order, and a network that has not associated in
// this long is not going to.
inline constexpr uint32_t kConnectTimeoutMs = 20000;

// How long to keep asking the resolver once the link IS up. Short, because the
// only thing being waited for is a DHCP lease that has already been requested;
// if it has not landed in this long something is wrong with the network rather
// than slow about it.
inline constexpr uint32_t kDnsTimeoutMs = 8000;

// Gap between resolver attempts. Not zero: a tight loop on hostByName() burns
// the radio and the CPU for a condition that changes on DHCP's schedule, not
// ours.
inline constexpr uint32_t kDnsRetryMs = 500;

enum class Phase {
  Connecting,   // WiFi.begin() issued, waiting for association
  Resolving,    // associated; waiting for the resolver to answer
  Ready,        // both hold -- start the check
  NoCredential, // nothing saved to try; this is the old NO_WIFI case
  ConnectFailed,// never associated inside kConnectTimeoutMs
  DnsFailed,    // associated but never resolved inside kDnsTimeoutMs
};

// What the screen should be doing, from what it can observe.
//
// `hasCredential` is false only when there is no last-used network to try at
// all. `linkUp` is WiFi.status() == WL_CONNECTED. `dnsOk` is a successful
// hostByName() for the host the check will use -- the SAME host, because
// resolving a different one proves nothing about this one's record.
//
// `sinceBeginMs` runs from the join attempt; `sinceLinkMs` from the moment the
// link came up, and is only read once it has.
constexpr Phase decide(bool hasCredential, bool linkUp, bool dnsOk, uint32_t sinceBeginMs, uint32_t sinceLinkMs) {
  if (dnsOk && linkUp) return Phase::Ready;
  if (!linkUp) {
    if (!hasCredential) return Phase::NoCredential;
    return sinceBeginMs >= kConnectTimeoutMs ? Phase::ConnectFailed : Phase::Connecting;
  }
  // Associated. The link is up whether we joined it or found it already up, so
  // a missing credential is irrelevant from here on.
  return sinceLinkMs >= kDnsTimeoutMs ? Phase::DnsFailed : Phase::Resolving;
}

// A phase the screen sits in rather than reports.
constexpr bool isWaiting(Phase p) { return p == Phase::Connecting || p == Phase::Resolving; }

constexpr const char* phaseName(Phase p) {
  switch (p) {
    case Phase::Connecting: return "connecting";
    case Phase::Resolving: return "resolving";
    case Phase::Ready: return "ready";
    case Phase::NoCredential: return "no-credential";
    case Phase::ConnectFailed: return "connect-failed";
    case Phase::DnsFailed: return "dns-failed";
  }
  return "?";
}

}  // namespace otapreflight
