// The key store's pure half: src/util/KeyStoreCore.h. Owner 2026-11-26:
// keys get Settings rows, show only their last 4 characters, never land in
// settings.json, and never appear in a log. Every value here is an obvious
// fake; none is a real key.
#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "util/KeyStoreCore.h"

namespace {

using keystore::Key;

// Obvious fakes, long enough (>= 12) to show a last 4.
const std::string kFakeGithub = "FAKE-GITHUB-TOKEN-NOT-REAL-0000000000-WXYZ";
const std::string kFakeClaude = "FAKE-CLAUDE-KEY-NOT-REAL-1111111111-ABCD";

std::string slurp(const std::string& relative) {
  std::ifstream f(std::string(REPO_ROOT_DIR) + "/" + relative);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

}  // namespace

// --- names -----------------------------------------------------------------

TEST(KeyStoreNames, EveryKeyRoundTripsAndFitsNvs) {
  for (const Key k : keystore::kAllKeys) {
    Key back{};
    ASSERT_TRUE(keystore::keyFromName(keystore::storageName(k), back));
    EXPECT_EQ(back, k);
    EXPECT_LE(std::string(keystore::nvsName(k)).size(), 15u) << "NVS keys are capped at 15 characters";
  }
  Key unused{};
  EXPECT_FALSE(keystore::keyFromName("githubTokenX", unused));
  EXPECT_FALSE(keystore::keyFromName("", unused));
  EXPECT_FALSE(keystore::keyFromName(nullptr, unused));
}

// The iOS backend reads the same NSUserDefaults keys the Settings.app rows
// write; renaming one here would orphan every stored token silently.
TEST(KeyStoreNames, StorageNamesMatchTheHostSettingsKeys) {
  EXPECT_STREQ(keystore::storageName(Key::GithubToken), "githubToken");
  EXPECT_STREQ(keystore::storageName(Key::ClaudeApiKey), "claudeApiKey");
}

// --- normalize ---------------------------------------------------------------

TEST(KeyStoreNormalize, TrimsWhatAPasteBrings) {
  std::string out;
  ASSERT_TRUE(keystore::normalize("  \t" + kFakeClaude + "\r\n", out));
  EXPECT_EQ(out, kFakeClaude);
}

TEST(KeyStoreNormalize, RefusesWhatNoKeyContains) {
  std::string out = "untouched";
  EXPECT_FALSE(keystore::normalize("", out));
  EXPECT_FALSE(keystore::normalize(" \n ", out));
  EXPECT_FALSE(keystore::normalize("FAKE KEY WITH SPACES", out));
  EXPECT_FALSE(keystore::normalize(std::string("FAKE\x01KEY"), out));
  EXPECT_FALSE(keystore::normalize(std::string("FAKE\x7fKEY"), out));
  EXPECT_FALSE(keystore::normalize("FAKE\nSECOND-LINE", out));
  EXPECT_FALSE(keystore::normalize(std::string(keystore::kMaxLength + 1, 'F'), out));
  EXPECT_EQ(out, "untouched") << "a refused value leaves the output alone";
  EXPECT_TRUE(keystore::normalize(std::string(keystore::kMaxLength, 'F'), out));
}

// --- what any screen, page or API may show ------------------------------------

TEST(KeyStoreDisplay, LastFourOnlyAndOnlyWhenLongEnough) {
  char tail[5];
  keystore::last4(kFakeClaude, tail);
  EXPECT_STREQ(tail, "ABCD");
  keystore::last4("", tail);
  EXPECT_STREQ(tail, "");
  keystore::last4("FAKE-SHORT1", tail);  // 11 < 12: four would give most of it away
  EXPECT_STREQ(tail, "");
  keystore::last4("FAKE-SHORT12", tail);  // exactly 12
  EXPECT_STREQ(tail, "RT12");
}

// Owner 2026-11-26: "do not wrap long secret inputs or outputs". The entry
// field shows a fixed mask and the length: one line, no character of the key,
// and a display length bounded whatever the input length.
TEST(KeyStoreDisplay, TheEntryTextIsOneLineWithNoKeyCharacters) {
  const char* one = "1 character";
  const char* many = "%u characters";
  EXPECT_EQ(keystore::secretEntryText("", one, many), "") << "an empty field shows nothing";
  EXPECT_EQ(keystore::secretEntryText("K", one, many), "********  1 character");
  EXPECT_EQ(keystore::secretEntryText(kFakeGithub, one, many),
            "********  " + std::to_string(kFakeGithub.size()) + " characters");
  size_t longest = 0;
  for (size_t n = 1; n <= keystore::kMaxLength + 64; ++n) {
    std::string v;
    for (size_t i = 0; i < n; ++i) v += static_cast<char>('A' + i % 26);
    const std::string shown = keystore::secretEntryText(v, one, many);
    EXPECT_EQ(shown.find('\n'), std::string::npos) << n;
    for (const char c : shown) {
      // Only the mask, the separator, digits and the length words: no key byte.
      const bool allowed =
          c == '*' || c == ' ' || (c >= '0' && c <= '9') || std::string("characters").find(c) != std::string::npos;
      EXPECT_TRUE(allowed) << n << ": " << shown;
    }
    EXPECT_NE(shown.find(std::to_string(n)), std::string::npos) << "the length is shown: " << shown;
    longest = std::max(longest, shown.size());
  }
  EXPECT_LE(longest, keystore::kEntryMaskStars + 2 + std::string("319 characters").size())
      << "bounded whatever the input length";
  // Code points, not bytes.
  EXPECT_EQ(keystore::secretEntryText("\xc3\xa9\xc3\xa9", one, many), "********  2 characters");
}

// --- never on the card -----------------------------------------------------

// Source gates (owner 2026-11-26): no key is ever read from or written to the
// card. settings.json has no token field at all, and nothing names the card
// files older builds read (they are ignored, not migrated: the owner's
// no-backwards-compatibility ruling the same day).
TEST(KeyStoreNeverOnCard, SettingsCarryNoToken) {
  for (const char* file : {"src/CrossPointSettings.cpp", "src/CrossPointSettings.h"}) {
    const std::string src = slurp(file);
    ASSERT_FALSE(src.empty()) << file;
    EXPECT_EQ(src.find("githubToken"), std::string::npos) << file;
    EXPECT_EQ(src.find("claudeApiKey"), std::string::npos) << file;
  }
}

TEST(KeyStoreNeverOnCard, NothingReadsTheOldCardFiles) {
  for (const char* file :
       {"src/util/KeyStoreCore.h", "src/network/GithubAuth.h", "src/notes/ClaudeChat.cpp", "src/main.cpp"}) {
    const std::string src = slurp(file);
    ASSERT_FALSE(src.empty()) << file;
    EXPECT_EQ(src.find("\"/github-token.txt\""), std::string::npos) << file;
    EXPECT_EQ(src.find("\"/claude-key.txt\""), std::string::npos) << file;
    EXPECT_EQ(src.find("cardsecret::"), std::string::npos) << file;
  }
  for (const char* file : {"src/util/KeyStoreCore.h", "src/network/GithubAuth.h"}) {
    EXPECT_EQ(slurp(file).find("Storage."), std::string::npos) << file << ": never touches the card";
  }
  // KeyStore.h names the old files only to DELETE them at boot (owner
  // 2026-11-26, "Delete at boot"): it may ask whether they exist and remove
  // them, and never open or read one.
  const std::string ks = slurp("src/util/KeyStore.h");
  ASSERT_FALSE(ks.empty());
  EXPECT_EQ(ks.find("openFileForRead"), std::string::npos);
  EXPECT_EQ(ks.find("readOneLine"), std::string::npos);
  EXPECT_EQ(ks.find(".read("), std::string::npos);
  size_t at = 0;
  while ((at = ks.find("Storage.", at)) != std::string::npos) {
    if (at > 0 && std::isalnum(static_cast<unsigned char>(ks[at - 1]))) {  // HalStorage.h, not a call
      ++at;
      continue;
    }
    const std::string call = ks.substr(at, 16);
    EXPECT_TRUE(call.rfind("Storage.exists(", 0) == 0 || call.rfind("Storage.remove(", 0) == 0)
        << "KeyStore.h may only check for and remove the old files: " << call;
    ++at;
  }
}
