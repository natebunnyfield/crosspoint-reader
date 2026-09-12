// FontManifestParser: the streaming replacement for the buffered manifest parse.
//
// The measurement that motivated it (2026-09-07): the real manifest is 18,108
// bytes held in ONE contiguous block, plus ~14.6 KB of ArduinoJson document
// across 185 blocks, both live at once -- on a device measured refusing 22,049
// contiguous bytes. See B-053.
//
// The tests that matter most here are the ones a buffered parser never needed:
// arbitrary chunk boundaries, and knowing that the document ENDED. The sync is
// a mirror, so a truncated manifest accepted as a short list would delete every
// family the truncation cut off.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "FontManifestParser.h"

namespace {

struct Collected {
  std::vector<FontManifestParser::RawFamily> families;
};

bool collect(void* ctx, FontManifestParser::RawFamily& raw) {
  static_cast<Collected*>(ctx)->families.push_back(std::move(raw));
  return true;
}

std::string oneFile(const char* family, int pt, const char* sha) {
  return std::string("{\"file\":\"") + family + "_" + std::to_string(pt) + ".cpfont\",\"asset\":\"" + family + "_" +
         std::to_string(pt) + ".cpfont\",\"bytes\":" + std::to_string(1000 + pt) + ",\"sha256\":\"" + sha + "\"}";
}

// Thirteen families of six cuts, the shape publish_fonts.py actually writes.
std::string realisticManifest(int familyCount = 13) {
  const std::string sha(64, 'a');
  std::string s = "{\n  \"version\": 1,\n  \"generated\": \"2026-09-07T00:00:00Z\",\n  \"families\": [\n";
  for (int f = 0; f < familyCount; ++f) {
    const std::string name = "Family" + std::to_string(f);
    s += "    {\"family\": \"" + name + "\", \"files\": [";
    const int sizes[6] = {8, 10, 12, 14, 17, 20};
    for (int i = 0; i < 6; ++i) {
      if (i) s += ", ";
      s += oneFile(name.c_str(), sizes[i], sha.c_str());
    }
    s += "]}";
    if (f + 1 < familyCount) s += ",";
    s += "\n";
  }
  s += "  ]\n}\n";
  return s;
}

Collected parseInChunks(const std::string& json, size_t chunk, bool* complete = nullptr, bool* sawFamilies = nullptr,
                        int* version = nullptr, size_t maxFiles = 16) {
  Collected out;
  FontManifestParser parser(collect, &out, maxFiles);
  for (size_t i = 0; i < json.size(); i += chunk) {
    parser.feed(json.data() + i, std::min(chunk, json.size() - i));
  }
  if (complete) *complete = parser.documentComplete();
  if (sawFamilies) *sawFamilies = parser.sawFamiliesArray();
  if (version) *version = parser.version();
  return out;
}

TEST(FontManifest, ParsesEveryFamilyAndFile) {
  bool complete = false;
  const Collected got = parseInChunks(realisticManifest(), 1024, &complete);
  ASSERT_TRUE(complete);
  ASSERT_EQ(13u, got.families.size());
  EXPECT_EQ("Family0", got.families[0].name);
  ASSERT_EQ(6u, got.families[0].files.size());
  EXPECT_EQ("Family0_8.cpfont", got.families[0].files[0].file);
  EXPECT_EQ("Family0_8.cpfont", got.families[0].files[0].asset);
  EXPECT_EQ(1008u, got.families[0].files[0].bytes);
  EXPECT_EQ(std::string(64, 'a'), got.families[0].files[0].sha256);
  EXPECT_EQ("Family12", got.families[12].name);
}

// The one thing a streaming parser gets wrong that a buffered one cannot: a
// token split across two socket reads. One byte at a time is the worst case and
// every field must survive it identically.
TEST(FontManifest, ChunkBoundariesDoNotChangeTheResult) {
  const std::string json = realisticManifest(4);
  const Collected whole = parseInChunks(json, json.size());
  for (const size_t chunk : {size_t{1}, size_t{2}, size_t{3}, size_t{7}, size_t{64}, size_t{997}}) {
    const Collected got = parseInChunks(json, chunk);
    ASSERT_EQ(whole.families.size(), got.families.size()) << "chunk size " << chunk;
    for (size_t f = 0; f < whole.families.size(); ++f) {
      EXPECT_EQ(whole.families[f].name, got.families[f].name) << "chunk size " << chunk;
      ASSERT_EQ(whole.families[f].files.size(), got.families[f].files.size()) << "chunk size " << chunk;
      for (size_t i = 0; i < whole.families[f].files.size(); ++i) {
        EXPECT_EQ(whole.families[f].files[i].file, got.families[f].files[i].file) << "chunk size " << chunk;
        EXPECT_EQ(whole.families[f].files[i].bytes, got.families[f].files[i].bytes) << "chunk size " << chunk;
        EXPECT_EQ(whole.families[f].files[i].sha256, got.families[f].files[i].sha256) << "chunk size " << chunk;
      }
    }
  }
}

// THE load-bearing test. A body cut mid-transfer hands over every family that
// arrived whole, so the family list looks valid and is SHORT. The sync is a
// mirror; accepting it deletes the rest of the card.
TEST(FontManifest, ATruncatedDocumentIsNotComplete) {
  const std::string json = realisticManifest();
  for (const int pct : {10, 25, 50, 75, 90, 99}) {
    const std::string cut = json.substr(0, json.size() * pct / 100);
    bool complete = true;
    const Collected got = parseInChunks(cut, 128, &complete);
    EXPECT_FALSE(complete) << "truncated at " << pct << "% was accepted as a whole document";
    // Families that DID arrive whole are still reported -- that is the parser
    // doing its job. The completeness flag is what the caller must refuse on.
    EXPECT_LE(got.families.size(), 13u);
  }
}

// Cut after the families array closes but before the top-level object does.
// The families are all present and correct; only the last byte is missing.
TEST(FontManifest, ClosingTheFamiliesArrayIsNotEnough) {
  std::string json = realisticManifest(2);
  const size_t brace = json.rfind('}');
  ASSERT_NE(std::string::npos, brace);
  json.erase(brace);
  bool complete = true;
  const Collected got = parseInChunks(json, 64, &complete);
  EXPECT_EQ(2u, got.families.size());
  EXPECT_FALSE(complete);
}

TEST(FontManifest, AWholeDocumentIsComplete) {
  bool complete = false;
  bool sawFamilies = false;
  int version = 0;
  parseInChunks(realisticManifest(1), 16, &complete, &sawFamilies, &version);
  EXPECT_TRUE(complete);
  EXPECT_TRUE(sawFamilies);
  EXPECT_EQ(1, version);
}

// "No families array" and "an empty families array" are different answers, and
// the removal gate turns on the difference: the first is a manifest we do not
// understand, the second is a real instruction.
TEST(FontManifest, NoFamiliesArrayIsDistinctFromAnEmptyOne) {
  bool sawFamilies = true;
  bool complete = false;
  parseInChunks("{\"version\":1}", 4, &complete, &sawFamilies);
  EXPECT_TRUE(complete);
  EXPECT_FALSE(sawFamilies);

  sawFamilies = false;
  complete = false;
  const Collected empty = parseInChunks("{\"version\":1,\"families\":[]}", 4, &complete, &sawFamilies);
  EXPECT_TRUE(complete);
  EXPECT_TRUE(sawFamilies);
  EXPECT_EQ(0u, empty.families.size());
}

TEST(FontManifest, AVersionTheFirmwareDoesNotKnowIsReadBack) {
  int version = 0;
  parseInChunks("{\"version\":99,\"families\":[]}", 8, nullptr, nullptr, &version);
  EXPECT_EQ(99, version);
}

// Absent means 1: manifests predate the field.
TEST(FontManifest, AMissingVersionReadsAsOne) {
  int version = 0;
  parseInChunks("{\"families\":[]}", 8, nullptr, nullptr, &version);
  EXPECT_EQ(1, version);
}

// A hostile manifest must not grow one family without end.
TEST(FontManifest, FilesPerFamilyIsCapped) {
  const std::string sha(64, 'b');
  std::string json = "{\"version\":1,\"families\":[{\"family\":\"Big\",\"files\":[";
  for (int i = 0; i < 40; ++i) {
    if (i) json += ",";
    json += oneFile("Big", i + 1, sha.c_str());
  }
  json += "]}]}";
  const Collected got = parseInChunks(json, 100, nullptr, nullptr, nullptr, /*maxFiles=*/16);
  ASSERT_EQ(1u, got.families.size());
  EXPECT_EQ(16u, got.families[0].files.size());
}

// Keys the firmware does not know, and nested objects inside a file entry, must
// not shift the state machine or leak into a neighbouring field.
TEST(FontManifest, UnknownKeysAndNestingAreIgnored) {
  const std::string sha(64, 'c');
  const std::string json =
      "{\"version\":1,\"generated\":\"x\",\"extra\":{\"a\":[1,2,{\"b\":\"c\"}]},"
      "\"families\":[{\"family\":\"Ok\",\"note\":{\"deep\":[{\"x\":1}]},"
      "\"files\":[{\"file\":\"Ok_10.cpfont\",\"asset\":\"Ok_10.cpfont\",\"bytes\":42,"
      "\"sha256\":\"" +
      sha + "\",\"meta\":{\"nested\":true}}]}]}";
  bool complete = false;
  const Collected got = parseInChunks(json, 5, &complete);
  EXPECT_TRUE(complete);
  ASSERT_EQ(1u, got.families.size());
  EXPECT_EQ("Ok", got.families[0].name);
  ASSERT_EQ(1u, got.families[0].files.size());
  EXPECT_EQ("Ok_10.cpfont", got.families[0].files[0].file);
  EXPECT_EQ(42u, got.families[0].files[0].bytes);
  EXPECT_EQ(sha, got.families[0].files[0].sha256);
}

}  // namespace
