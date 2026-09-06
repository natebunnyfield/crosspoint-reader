// The address a peer is told to open for File Transfer.
//
// Owner, 2026-09-06: "get File Transfer on ios app working". On a phone the
// simulator's shim listens on 8080 (a host process cannot bind 80), the
// firmware painted and QR-encoded http://<ip>/ regardless, and crosspoint.local
// is unclaimable on iOS -- so every address on the screen was unreachable
// (crosspoint-simulator ios/WIFI.md, findings 4 and 6). The screen now spells
// the port it learned from the server; this pins the spelling for both ports.
#include <gtest/gtest.h>

#include "network/PeerUrl.h"

namespace {

// Hardware: port 80, and the URL is exactly what it always was.
TEST(PeerUrl, DefaultPortIsOmitted) {
  EXPECT_EQ(peerurl::hostWithPort("192.168.1.20", 80), "192.168.1.20");
  EXPECT_EQ(peerurl::httpUrl("192.168.1.20", 80), "http://192.168.1.20/");
  EXPECT_EQ(peerurl::httpUrl("crosspoint.local", 80), "http://crosspoint.local/");
}

// A host build: the mapped port is part of the address, in the QR code too.
TEST(PeerUrl, MappedPortIsSpelledOut) {
  EXPECT_EQ(peerurl::hostWithPort("10.0.0.7", 8080), "10.0.0.7:8080");
  EXPECT_EQ(peerurl::httpUrl("10.0.0.7", 8080), "http://10.0.0.7:8080/");
  EXPECT_EQ(peerurl::httpUrl("crosspoint.local", 8080), "http://crosspoint.local:8080/");
}

// CROSSPOINT_SIM_HTTP_PORT can move the pair anywhere up to 65534; the widest
// value must fit the suffix buffer.
TEST(PeerUrl, WidestPortFits) {
  EXPECT_EQ(peerurl::hostWithPort("10.0.0.7", 65534), "10.0.0.7:65534");
  EXPECT_EQ(peerurl::httpUrl("10.0.0.7", 65535), "http://10.0.0.7:65535/");
}

}  // namespace
