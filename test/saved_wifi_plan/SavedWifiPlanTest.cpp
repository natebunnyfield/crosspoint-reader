// Rejoining a saved network without asking.
//
// Owner 2026-10-20: "not say 'Need wifi' but just connect to the last wifi on
// its own smoothly without blocking prompts."
//
// Both inversions of this policy are silent on a device: one that gives up
// early is "No Wi-Fi connection" for a network the card holds, and one that
// never gives up is a screen that says "Connecting to saved Wi-Fi..." forever.
#include <gtest/gtest.h>

#include "network/SavedWifiPlan.h"

using savedwifi::decide;
using savedwifi::joinOrder;
using savedwifi::kDhcpGraceMs;
using savedwifi::kEachAttemptMs;
using savedwifi::kSearchBudgetMs;
using savedwifi::kSoleAttemptMs;
using savedwifi::Link;
using savedwifi::Step;

namespace {

TEST(SavedWifiPlan, LastNetworkGoesFirstThenTheRestInSavedOrder) {
  EXPECT_EQ(joinOrder({"home", "office", "cafe"}, "cafe"), (std::vector<std::string>{"cafe", "home", "office"}));
  EXPECT_EQ(joinOrder({"home", "office"}, "home"), (std::vector<std::string>{"home", "office"}));
}

TEST(SavedWifiPlan, NoLastNetworkStillTriesEverySavedOne) {
  // The case the firmware update screen got wrong: it tried ONLY the last
  // network, so a card with saved networks and no last one said "No Wi-Fi".
  EXPECT_EQ(joinOrder({"home", "office"}, ""), (std::vector<std::string>{"home", "office"}));
}

TEST(SavedWifiPlan, AForgottenLastNetworkContributesNothing) {
  EXPECT_EQ(joinOrder({"home"}, "gone"), (std::vector<std::string>{"home"}));
  EXPECT_TRUE(joinOrder({}, "gone").empty());
}

TEST(SavedWifiPlan, EachNetworkOnceAndNoEmptyNames) {
  EXPECT_EQ(joinOrder({"home", "", "home", "cafe"}, "cafe"), (std::vector<std::string>{"cafe", "home"}));
}

TEST(SavedWifiPlan, UpIsLinkedWhateverTheClocksSay) {
  EXPECT_EQ(decide(Link::Up, 0, 0, 1, 0), Step::Linked);
  EXPECT_EQ(decide(Link::Up, kSoleAttemptMs * 5, kSearchBudgetMs * 5, 3, 2), Step::Linked);
}

TEST(SavedWifiPlan, ASoleNetworkGetsTheWholeConnectWindow) {
  EXPECT_EQ(decide(Link::Pending, kSoleAttemptMs - 1, kSoleAttemptMs - 1, 1, 0), Step::Wait);
  EXPECT_EQ(decide(Link::Pending, kSoleAttemptMs, kSoleAttemptMs, 1, 0), Step::GiveUp);
}

TEST(SavedWifiPlan, SeveralNetworksEachGetTheirCapThenTheNext) {
  EXPECT_EQ(decide(Link::Pending, kEachAttemptMs - 1, kEachAttemptMs - 1, 3, 0), Step::Wait);
  EXPECT_EQ(decide(Link::Pending, kEachAttemptMs, kEachAttemptMs, 3, 0), Step::TryNext);
  EXPECT_EQ(decide(Link::Pending, kEachAttemptMs, 2 * kEachAttemptMs, 3, 1), Step::TryNext);
  // The last one in the list has nowhere to go.
  EXPECT_EQ(decide(Link::Pending, kEachAttemptMs, kSearchBudgetMs - 1, 3, 2), Step::GiveUp);
}

TEST(SavedWifiPlan, ARefusalMovesOnAtOnce) {
  // WL_CONNECT_FAILED is an answer; waiting out the cap would only delay the
  // network that is actually in range. (WL_NO_SSID_AVAIL is mapped to Pending
  // by SavedWifiJoin, so it never reaches here as a refusal.)
  EXPECT_EQ(decide(Link::Refused, 0, 0, 2, 0), Step::TryNext);
  EXPECT_EQ(decide(Link::Refused, 0, 0, 1, 0), Step::GiveUp);
}

TEST(SavedWifiPlan, TheWholeSearchIsBounded) {
  // Eight saved networks out of range must not hold a screen for 80 s.
  EXPECT_EQ(decide(Link::Pending, 0, kSearchBudgetMs, 8, 3), Step::GiveUp);
  EXPECT_EQ(decide(Link::Refused, 0, kSearchBudgetMs, 8, 3), Step::GiveUp);
  EXPECT_EQ(decide(Link::Pending, kEachAttemptMs, kSearchBudgetMs - 1, 8, 2), Step::TryNext);
}

TEST(SavedWifiPlan, AJoinWaitingForItsLeaseGetsTheGrace) {
  // Owner report 2026-10-20: "Connection timeout [8 ASSOC_LEAVE]". WL_CONNECTED
  // arrives only with the DHCP address, so a join that had associated was cut
  // off at the cap as if it had failed.
  EXPECT_EQ(decide(Link::Associated, kSoleAttemptMs, kSoleAttemptMs, 1, 0), Step::Wait);
  EXPECT_EQ(decide(Link::Associated, kSoleAttemptMs + kDhcpGraceMs - 1, kSoleAttemptMs + kDhcpGraceMs - 1, 1, 0),
            Step::Wait);
  EXPECT_EQ(decide(Link::Associated, kSoleAttemptMs + kDhcpGraceMs, kSoleAttemptMs + kDhcpGraceMs, 1, 0),
            Step::GiveUp);
  // Past the search budget too, which a sole attempt plus grace exceeds.
  EXPECT_EQ(decide(Link::Associated, kEachAttemptMs, kSearchBudgetMs, 3, 2), Step::Wait);
  EXPECT_EQ(decide(Link::Associated, kEachAttemptMs, kSearchBudgetMs + kDhcpGraceMs, 3, 2), Step::GiveUp);
  // Without the association the same clocks are over.
  EXPECT_EQ(decide(Link::Pending, kSoleAttemptMs, kSoleAttemptMs, 1, 0), Step::GiveUp);
}

TEST(SavedWifiPlan, TheBudgetCoversAtLeastOneFullSoleAttempt) {
  static_assert(kSearchBudgetMs > kSoleAttemptMs, "a lone network would be cut short");
  static_assert(kSoleAttemptMs > kEachAttemptMs, "a lone network should get the longer window");
}

}  // namespace
