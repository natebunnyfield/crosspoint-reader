// Shared fixtures for the font-commit suite.
//
// WHAT THIS SUITE IS FOR. Every other test of Update Fonts works on pure logic
// (test/font_sync) or on the screen over a doubled updater
// (test/activity_input/FontUpdateFirstFrameTest.cpp). Neither runs a single
// line of the part that writes to the card: staging, verification, the two
// directory renames, the rollback, the recursive delete of the outgoing
// family, and the recovery of a family left absent by an interrupted commit.
// That was the largest gap an adversarial review named on 2026-09-07, and it
// is where a partial install would come from if one ever could.
//
// So this suite compiles the REAL src/network/FontUpdater.cpp and the REAL
// SdCardFontRegistry against a real filesystem in a temp directory, with only
// the network, the digest primitive, the settings struct and the font-system
// facade replaced. Assertions are about what is on disk afterwards.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "stubs/mbedtls/sha256.h"

// The storage stub's root, settable per test so nothing can reach the repo's
// own fs_/ card tree.
std::string& halStorageRootRef();

std::string sha256Hex(const std::string& data);

namespace fakegh {

struct Failure {
  size_t afterBytes = 0;  // deliver this much, then fail
  int error = 1;          // HttpDownloader::HTTP_ERROR
};

struct Server {
  std::map<std::string, std::string> bodies;  // url -> body
  std::map<std::string, Failure> failures;    // url -> how it goes wrong
  std::vector<std::string> requested;         // urls asked for, in order
};

Server& server();
void reset();

}  // namespace fakegh
