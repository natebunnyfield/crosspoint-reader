#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// REJOINING A SAVED NETWORK WITHOUT ASKING.
//
// Owner 2026-10-20: "not say 'Need wifi' but just connect to the last wifi on
// its own smoothly without blocking prompts."
//
// Library Update and Font Update answered "not associated" with "No Wi-Fi
// connection / Join a network in Settings" and stopped, and Firmware Update,
// which did try the last network, could not find it: nothing loads
// /.crosspoint/wifi.json at boot (main.cpp loads SETTINGS, APP_STATE and
// RECENT_BOOKS only), so after a fresh boot the store was EMPTY until the Wi-Fi
// picker had been opened once. Every one of those screens therefore asked the
// owner to go and supply a network the card already held.
//
// The policy is the picker's own auto-connect, minus the picker: the last
// network that worked goes first, then every other saved network in the order
// it was saved, each under its own cap and all of them under one budget, so a
// card with eight saved networks out of range cannot stall a screen for
// minutes. Nothing here asks a question; the only screen a reader can still
// see is the old one, and only when there is truly nothing to join.
//
// Pure and clock-free, like network/OtaPreflight.h, for the same reason: both
// of its failure modes -- giving up while a network was still coming up, and
// never giving up -- are invisible in a compile. The WiFi glue is
// network/SavedWifiJoin.h.
namespace savedwifi {

// One saved network on its own gets the whole connect window the firmware
// update screen always gave its single network (otapreflight::kConnectTimeoutMs).
inline constexpr uint32_t kSoleAttemptMs = 20000;

// With several saved, each gets this much before the next is tried. The
// all-channel scan, WPA handshake and DHCP take ~3-6 s on a network that is in
// range; one that has not associated in 10 s is very probably not here.
inline constexpr uint32_t kEachAttemptMs = 10000;

// The whole search, however many networks are saved.
inline constexpr uint32_t kSearchBudgetMs = 30000;

// The last network that worked first, then the rest in saved order, each once.
// An empty or stale `last` (forgotten since) simply contributes nothing.
inline std::vector<std::string> joinOrder(const std::vector<std::string>& saved, const std::string& last) {
  std::vector<std::string> order;
  order.reserve(saved.size());
  for (const std::string& ssid : saved) {
    if (!last.empty() && ssid == last) {
      order.push_back(ssid);
      break;
    }
  }
  for (const std::string& ssid : saved) {
    if (ssid.empty()) continue;
    bool seen = false;
    for (const std::string& have : order) seen = seen || have == ssid;
    if (!seen) order.push_back(ssid);
  }
  return order;
}

constexpr uint32_t attemptBudgetMs(size_t candidates) { return candidates > 1 ? kEachAttemptMs : kSoleAttemptMs; }

// What the radio says about the attempt in hand. `Refused` is
// WL_CONNECT_FAILED or repeated auth failures -- an answer, so there is no point
// waiting out the rest of the cap. (WL_NO_SSID_AVAIL is Pending: see
// SavedWifiJoin::linkState.)
enum class Link { Pending, Up, Refused };

enum class Step {
  Wait,     // keep polling this network
  Linked,   // associated -- go on to the resolver
  TryNext,  // this one is done; begin the next saved network
  GiveUp,   // nothing left, or the budget is spent
};

// `index` is the position in joinOrder() of the network being tried.
constexpr Step decide(Link link, uint32_t sinceAttemptMs, uint32_t sinceSearchMs, size_t candidates, size_t index) {
  if (link == Link::Up) return Step::Linked;
  const bool attemptOver = link == Link::Refused || sinceAttemptMs >= attemptBudgetMs(candidates);
  const bool budgetSpent = sinceSearchMs >= kSearchBudgetMs;
  if (!attemptOver) return budgetSpent ? Step::GiveUp : Step::Wait;
  if (index + 1 < candidates && !budgetSpent) return Step::TryNext;
  return Step::GiveUp;
}

}  // namespace savedwifi
