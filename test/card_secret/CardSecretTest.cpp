// src/util/CardSecret.h -- the one-line secret reader behind /claude-key.txt
// and /github-token.txt.
//
// The contract both files rely on: a missing or empty file is "not configured"
// (false, so the screen can say where the file goes), and the trailing newline
// or spaces an editor leaves behind never reach the Authorization header. A
// stray "\n" on the end of a token is the classic silent 401.
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "util/CardSecret.h"

namespace {

class CardSecretTest : public ::testing::Test {
 protected:
  std::filesystem::path root;

  void SetUp() override {
    root = std::filesystem::temp_directory_path() / "crosspoint-card-secret-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    setenv("CROSSPOINT_TEST_SD", root.string().c_str(), 1);
  }
  void TearDown() override { std::filesystem::remove_all(root); }

  void put(const char* devicePath, const std::string& bytes) {
    std::ofstream out(root / (devicePath + 1), std::ios::binary);
    out << bytes;
  }
};

TEST_F(CardSecretTest, MissingFileIsNotConfigured) {
  std::string out = "untouched";
  EXPECT_FALSE(cardsecret::readOneLine("T", "/github-token.txt", out));
  EXPECT_EQ(out, "untouched");
}

TEST_F(CardSecretTest, EmptyFileIsNotConfigured) {
  put("/github-token.txt", "");
  std::string out;
  EXPECT_FALSE(cardsecret::readOneLine("T", "/github-token.txt", out));
}

TEST_F(CardSecretTest, WhitespaceOnlyIsNotConfigured) {
  put("/github-token.txt", "\n \r\n");
  std::string out;
  EXPECT_FALSE(cardsecret::readOneLine("T", "/github-token.txt", out));
}

TEST_F(CardSecretTest, TrailingNewlineIsTrimmed) {
  put("/github-token.txt", "github_pat_ABCDEFGHIJKLMNOP\n");
  std::string out;
  ASSERT_TRUE(cardsecret::readOneLine("T", "/github-token.txt", out));
  EXPECT_EQ(out, "github_pat_ABCDEFGHIJKLMNOP");
}

TEST_F(CardSecretTest, TrailingCrLfAndSpacesAreTrimmed) {
  put("/github-token.txt", "ghp_0123456789abcdef  \r\n");
  std::string out;
  ASSERT_TRUE(cardsecret::readOneLine("T", "/github-token.txt", out));
  EXPECT_EQ(out, "ghp_0123456789abcdef");
}

TEST_F(CardSecretTest, ClaudeKeyReadsThroughTheSameReader) {
  put("/claude-key.txt", "sk-ant-example-key\n");
  std::string out;
  ASSERT_TRUE(cardsecret::readOneLine("CLAUDE", "/claude-key.txt", out));
  EXPECT_EQ(out, "sk-ant-example-key");
}

TEST_F(CardSecretTest, LeadingContentIsKeptVerbatim) {
  // Only the trailing edge is trimmed: a token never legitimately starts with
  // whitespace, and the reader must not invent rules beyond what
  // /claude-key.txt has always done.
  put("/github-token.txt", " token");
  std::string out;
  ASSERT_TRUE(cardsecret::readOneLine("T", "/github-token.txt", out));
  EXPECT_EQ(out, " token");
}

}  // namespace
