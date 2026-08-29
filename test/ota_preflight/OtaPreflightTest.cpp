// Getting to a usable network before the update check runs.
//
// Owner 2026-08-28: "improve update firmware to automatically connect to last
// used wifi network and not get any 'github.com not found' or other errors."
//
// The reported error is a DNS failure, and the reason it is worth a truth table
// is that it happens on a link the device believes is UP. WL_CONNECTED means
// ASSOCIATED; the resolver addresses arrive with the DHCP lease, so there is a
// window after every join and every wake in which status() says connected and
// every lookup fails. Ask GitHub inside that window and the fetch reports the
// host name it could not resolve.
//
// Both inversions are silent on a device: a pre-flight that gives up too early
// looks like a network problem the owner does not have, and one that never
// gives up is a screen that says "checking" forever.
#include <gtest/gtest.h>

#include "network/OtaPreflight.h"

using otapreflight::decide;
using otapreflight::kConnectTimeoutMs;
using otapreflight::kDnsTimeoutMs;
using otapreflight::Phase;

namespace {

TEST(OtaPreflight, ReadyOnlyWhenTheLinkIsUpAndTheNameResolves) {
  EXPECT_EQ(decide(true, true, true, 0, 0), Phase::Ready);
  // The half that was missing before this existed: associated, so the old code
  // went straight to the fetch, and the fetch is what said "not found".
  EXPECT_EQ(decide(true, true, false, 0, 0), Phase::Resolving);
  EXPECT_NE(decide(true, true, false, 0, 0), Phase::Ready) << "an association is not a resolver";
}

TEST(OtaPreflight, NoSavedNetworkIsNotAFailureButItsOwnAnswer) {
  // Nothing to join: the old NO_WIFI screen, and still correct. Distinct from
  // a join that was tried and failed, because the remedy differs -- one needs
  // a network chosen, the other needs the network fixed.
  EXPECT_EQ(decide(false, false, false, 0, 0), Phase::NoCredential);
  EXPECT_EQ(decide(false, false, false, kConnectTimeoutMs * 10, 0), Phase::NoCredential)
      << "with nothing to try, waiting longer changes nothing";
}

TEST(OtaPreflight, WaitsForTheAssociationThenGivesUp) {
  EXPECT_EQ(decide(true, false, false, 0, 0), Phase::Connecting);
  EXPECT_EQ(decide(true, false, false, kConnectTimeoutMs - 1, 0), Phase::Connecting);
  EXPECT_EQ(decide(true, false, false, kConnectTimeoutMs, 0), Phase::ConnectFailed);
}

TEST(OtaPreflight, WaitsForTheResolverThenGivesUpSeparately) {
  EXPECT_EQ(decide(true, true, false, 0, 0), Phase::Resolving);
  EXPECT_EQ(decide(true, true, false, 0, kDnsTimeoutMs - 1), Phase::Resolving);
  EXPECT_EQ(decide(true, true, false, 0, kDnsTimeoutMs), Phase::DnsFailed);
}

// THE BUG, stated as a test. The DNS budget runs from the LINK, not from the
// join: a join that took nineteen seconds must still get the resolver its full
// window, or a slow network fails for the wrong reason and the screen blames
// GitHub.
TEST(OtaPreflight, TheDnsBudgetStartsAtTheLinkNotAtTheJoin) {
  const uint32_t slowJoin = kConnectTimeoutMs - 1;
  EXPECT_EQ(decide(true, true, false, slowJoin, 0), Phase::Resolving);
  EXPECT_EQ(decide(true, true, false, slowJoin, kDnsTimeoutMs - 1), Phase::Resolving);
  EXPECT_EQ(decide(true, true, true, slowJoin, kDnsTimeoutMs - 1), Phase::Ready);
}

// Once associated, whether we had a credential stops mattering: the link is up
// however it got there, including a network joined on another screen.
TEST(OtaPreflight, AnAlreadyUpLinkNeedsNoCredential) {
  EXPECT_EQ(decide(false, true, false, 0, 0), Phase::Resolving);
  EXPECT_EQ(decide(false, true, true, 0, 0), Phase::Ready);
}

// A link that drops mid-resolve goes back to waiting for the link, not to
// DnsFailed -- the resolver was never the problem.
TEST(OtaPreflight, LosingTheLinkMidResolveReturnsToConnecting) {
  EXPECT_EQ(decide(true, false, false, 1000, 5000), Phase::Connecting);
}

TEST(OtaPreflight, WaitingPhasesAreExactlyTheTwo) {
  EXPECT_TRUE(otapreflight::isWaiting(Phase::Connecting));
  EXPECT_TRUE(otapreflight::isWaiting(Phase::Resolving));
  for (Phase p : {Phase::Ready, Phase::NoCredential, Phase::ConnectFailed, Phase::DnsFailed})
    EXPECT_FALSE(otapreflight::isWaiting(p)) << otapreflight::phaseName(p);
}

TEST(OtaPreflight, TheBudgetsAreOrderedAndNonZero) {
  EXPECT_GT(kConnectTimeoutMs, 0u);
  EXPECT_GT(kDnsTimeoutMs, 0u);
  EXPECT_GT(otapreflight::kDnsRetryMs, 0u) << "a zero retry gap is a tight loop on a blocking call";
  EXPECT_LT(otapreflight::kDnsRetryMs, kDnsTimeoutMs) << "at least one retry must fit in the window";
}

}  // namespace
