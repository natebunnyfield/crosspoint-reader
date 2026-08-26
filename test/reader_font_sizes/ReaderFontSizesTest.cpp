// The reader's SIX size slots — XXS / XS / S / M / L / XL.
//
// XXS and XS were inserted below S on 2026-08-26 (owner: "cut XS and XXS
// versions of every s tiers shipping font"), IN SIZE ORDER and with no
// migration, by the owner's own scope ruling: "the reindexing never matters
// because it is just me using this." So `fontSizeSlot` — a persisted INDEX —
// shifted meaning by two exactly once. There is nothing here asserting a
// migration, deliberately.
//
// What this suite exists for is the failure that ruling does NOT cover: a slot
// the picker OFFERS but nothing renders. The ramp lives in four places that
// must agree — BUILTIN_READER_POINT_SIZES, the SLOT_NAMES arrays that label it,
// the `case` ladder in getReaderFontId() that turns a point size into a
// built-in font id, and the eight installed families' `sizes:` in
// sd-fonts.yaml. Miss the third and a reader picking XXS gets a row labelled
// "XXS (8pt)" that renders at 14 pt, silently, with no log line and no
// compiler complaint. Miss the fourth and the whole picker drops to point-size
// labels, because buildFontSizeSetting() only names slots for a family that
// ships exactly READER_FONT_SLOT_COUNT of them.
//
// Three of those four are read as TEXT rather than linked. That is the same
// trade the simulator's dial_table_test makes: linking getReaderFontId() would
// drag in the settings singleton, ArduinoJson and HalStorage for what is a
// question about a `switch`, and the yaml is not compiled at all.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "ReaderFontSizes.h"

namespace {

// Repo-relative paths, resolved against REPO_ROOT so the suite can be run from
// anywhere (ctest runs it from the build tree).
std::string repoPath(const std::string& rel) { return std::string(CP_REPO_ROOT) + "/" + rel; }

std::string slurp(const std::string& rel) {
  std::ifstream in(repoPath(rel));
  EXPECT_TRUE(in.good()) << "cannot read " << repoPath(rel);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::vector<uint8_t> builtinRamp() {
  return {std::begin(BUILTIN_READER_POINT_SIZES), std::end(BUILTIN_READER_POINT_SIZES)};
}

// The `sizes: [...]` line of one family block in sd-fonts.yaml. Parsed with a
// line scan rather than a YAML library because the test tree has no PyYAML
// equivalent in C++ and the shape is fixed: "  - name: X" then, within that
// block, "    sizes: [a, b, ...]".
std::vector<int> yamlSizesFor(const std::string& yaml, const std::string& family) {
  std::istringstream in(yaml);
  std::string line;
  bool inBlock = false;
  while (std::getline(in, line)) {
    if (line.rfind("  - name: ", 0) == 0) inBlock = (line.substr(10) == family);
    if (!inBlock || line.rfind("    sizes: [", 0) != 0) continue;
    std::vector<int> out;
    const std::string body = line.substr(12, line.find(']') - 12);
    std::istringstream nums(body);
    std::string tok;
    while (std::getline(nums, tok, ',')) out.push_back(std::atoi(tok.c_str()));
    return out;
  }
  return {};
}

std::vector<std::string> installedFamilies(const std::string& yaml) {
  std::istringstream in(yaml);
  std::string line;
  bool inList = false;
  std::vector<std::string> out;
  while (std::getline(in, line)) {
    if (line.rfind("installed_families:", 0) == 0) {
      inList = true;
      continue;
    }
    if (!inList) continue;
    if (line.rfind("  - ", 0) == 0) {
      out.push_back(line.substr(4));
      continue;
    }
    // The list ends at the first line that is neither an entry nor a comment.
    if (!line.empty() && line[0] != '#') break;
  }
  return out;
}

// ---------------------------------------------------------------------------

TEST(ReaderFontSizes, TheBuiltinRampIsSixSlotsAscendingAndDistinct) {
  const auto ramp = builtinRamp();
  ASSERT_EQ(ramp.size(), READER_FONT_SLOT_COUNT)
      << "BUILTIN_READER_POINT_SIZES and READER_FONT_SLOT_COUNT disagree, so the built-in fallback family would "
         "lose its slot NAMES (buildFontSizeSetting only names a family that ships exactly the slot count)";
  EXPECT_EQ(ramp, (std::vector<uint8_t>{8, 10, 12, 14, 16, 18}));
  EXPECT_TRUE(std::is_sorted(ramp.begin(), ramp.end())) << "the ramp is INSERT-IN-SIZE-ORDER, never appended";
  EXPECT_EQ(std::set<uint8_t>(ramp.begin(), ramp.end()).size(), ramp.size()) << "a duplicated size is a dead slot";
}

// A null registry — no SD family — is the built-in fallback, and it must offer
// every one of the six. This is the configuration a blank card boots into.
TEST(ReaderFontSizes, NoSdFamilyOffersTheWholeBuiltinRamp) {
  EXPECT_EQ(readerFontPointSizes(nullptr, "Edgar"), builtinRamp());
  EXPECT_EQ(readerFontPointSizes(nullptr, ""), builtinRamp());
}

// Stepping the picker: every slot resolves to its own point size, and the ends
// clamp rather than wrapping. `pointSizeForSlot` is what the setter calls.
TEST(ReaderFontSizes, EverySlotResolvesToItsOwnPointSize) {
  const auto ramp = builtinRamp();
  for (uint8_t slot = 0; slot < ramp.size(); ++slot) {
    EXPECT_EQ(pointSizeForSlot(ramp, slot), ramp[slot]) << "slot " << int(slot);
  }
  EXPECT_EQ(pointSizeForSlot(ramp, 200), ramp.back()) << "an out-of-range slot must clamp to XL, not wrap to XXS";
}

// The round trip a family switch performs: pt -> slot -> pt. It has to be the
// identity on the ramp's own values, or switching family walks the reader.
TEST(ReaderFontSizes, SlotAndPointSizeRoundTripOnEverySlot) {
  const auto ramp = builtinRamp();
  for (uint8_t slot = 0; slot < ramp.size(); ++slot) {
    EXPECT_EQ(slotForPointSize(ramp, ramp[slot]), slot) << "pt " << int(ramp[slot]);
    EXPECT_EQ(snapToNearestPointSize(ramp, ramp[slot]), ramp[slot]);
  }
}

// The default has to keep pointing at M. It is stored as an INDEX, so the
// insertion moved it 1 -> 3; get that wrong and a fresh install reads at XS.
TEST(ReaderFontSizes, TheDefaultSlotIsStillFourteenPoint) {
  const std::string h = slurp("src/CrossPointSettings.h");
  std::smatch m;
  ASSERT_TRUE(std::regex_search(h, m, std::regex(R"(DEFAULT_FONT_SIZE_SLOT\s*=\s*(\d+))")))
      << "DEFAULT_FONT_SIZE_SLOT not found in CrossPointSettings.h";
  const auto ramp = builtinRamp();
  const int slot = std::atoi(m[1].str().c_str());
  ASSERT_LT(slot, static_cast<int>(ramp.size()));
  EXPECT_EQ(ramp[slot], 14) << "the default slot must still be M (14 pt on the built-in ramp); it is slot " << slot;

  ASSERT_TRUE(std::regex_search(h, m, std::regex(R"(DEFAULT_FONT_POINT_SIZE\s*=\s*(\d+))")));
  EXPECT_EQ(std::atoi(m[1].str().c_str()), 14) << "the derived default point size must agree with the default slot";
}

// THE SILENT ONE. getReaderFontId() switches on the snapped point size; a ramp
// entry with no case falls through to `default:` and renders 14 pt under
// another size's label.
//
// The assertion is on the PAIRING — `case N:` must be followed by the id for N,
// not merely accompanied somewhere by it. The first version of this test
// checked the two separately over the whole rest of the file, which a
// transposed ladder (`case 8: return ..._10_FONT_ID;`) satisfies completely.
// That is the same class of bug the test is named for, so it is worth the
// regex.
TEST(ReaderFontSizes, EveryBuiltinRampSizeHasAFontIdCase) {
  const std::string src = slurp("src/CrossPointSettings.cpp");
  const size_t fn = src.find("int CrossPointSettings::getReaderFontId()");
  ASSERT_NE(fn, std::string::npos) << "getReaderFontId() not found — this test is reading the wrong thing";
  // Bounded to the function: the next top-level `\n}` closes it. Without this
  // the search runs to EOF and every later function's text counts as a match.
  const size_t end = src.find("\n}", fn);
  ASSERT_NE(end, std::string::npos);
  const std::string body = src.substr(fn, end - fn);

  const std::string ids = slurp("src/fontIds.h");
  for (const uint8_t pt : builtinRamp()) {
    const std::string id = "LIBREFRANKLIN_READER_" + std::to_string(pt) + "_FONT_ID";
    EXPECT_NE(ids.find("#define " + id + " "), std::string::npos)
        << id << " is not defined: the " << int(pt) << " pt built-in cut has no font id";
    if (pt == 14) {
      // 14 is the `default:` arm and carries no `case` of its own.
      EXPECT_TRUE(std::regex_search(body, std::regex(R"(default:\s*\n?\s*return\s+)" + id + R"(\s*;)")))
          << "the default arm of getReaderFontId() does not return " << id;
      continue;
    }
    EXPECT_TRUE(std::regex_search(body, std::regex("case " + std::to_string(pt) + R"(:\s*\n?\s*return\s+)" + id +
                                                   R"(\s*;)")))
        << "getReaderFontId() does not map " << int(pt) << " pt to " << id
        << " — that slot is selectable in the picker and renders as some other size, silently";
  }
}

// The wide-table step-down must never hand back a face LARGER than the body.
// It is a bare `return LIBREFRANKLIN_READER_12_FONT_ID` in the obvious
// implementation, and 12 stopped being the bottom of the ramp on 2026-08-26 —
// so at XXS and XS that "size down" was a size UP, and
// `tableFontForRotation()` takes it unconditionally.
TEST(ReaderFontSizes, TheWideTableStepDownIsClampedToTheBodySize) {
  const std::string src = slurp("src/CrossPointSettings.cpp");
  const size_t fn = src.find("int CrossPointSettings::getSmallestReaderFontId()");
  ASSERT_NE(fn, std::string::npos);
  const size_t end = src.find("\n}", fn);
  ASSERT_NE(end, std::string::npos);
  const std::string body = src.substr(fn, end - fn);

  EXPECT_TRUE(std::regex_search(body, std::regex(R"(bodyPt\s*<\s*12\s*\?)")))
      << "getSmallestReaderFontId() returns a bare 12 pt id. Every ramp entry below 12 — "
         "there are two — then sets a rotated wide table LARGER than the page it sits on.";
  const auto ramp = builtinRamp();
  EXPECT_LT(ramp.front(), 12) << "this test is only meaningful while the ramp reaches below 12 pt";
}

// The built-in cuts have to EXIST as generated headers, and be included, or the
// case above names a symbol that is not there. Cheap because it is a file list.
TEST(ReaderFontSizes, EveryBuiltinRampSizeHasItsGeneratedHeadersIncluded) {
  const std::string all = slurp("lib/EpdFont/builtinFonts/all.h");
  for (const uint8_t pt : builtinRamp()) {
    for (const char* style : {"regular", "bold", "italic", "bolditalic"}) {
      const std::string name = "librefranklin_reader_" + std::to_string(pt) + "_" + style + ".h";
      EXPECT_NE(all.find(name), std::string::npos) << name << " is not included by builtinFonts/all.h";
      std::ifstream f(repoPath("lib/EpdFont/builtinFonts/" + name));
      EXPECT_TRUE(f.good()) << name << " does not exist — run lib/EpdFont/scripts/convert-builtin-fonts.sh";
    }
  }
  const std::string sh = slurp("lib/EpdFont/scripts/convert-builtin-fonts.sh");
  std::string want = "LIBREFRANKLIN_READER_SIZES=(";
  for (const uint8_t pt : builtinRamp()) want += std::to_string(pt) + (pt == builtinRamp().back() ? ")" : " ");
  EXPECT_NE(sh.find(want), std::string::npos)
      << "convert-builtin-fonts.sh would not regenerate the ramp this build declares; expected " << want;
}

// Both SLOT_NAMES arrays. There are two — the settings list and the font
// picker — and they have to carry the same six names in the same order, since
// they label the same stored index on two different screens.
TEST(ReaderFontSizes, BothSlotNameTablesCarryTheSixNamesInOrder) {
  const std::string want = R"({"XXS", "XS", "S", "M", "L", "XL"})";
  for (const char* rel : {"src/SettingsList.h", "src/activities/settings/FontSelectionActivity.cpp"}) {
    const std::string src = slurp(rel);
    EXPECT_NE(src.find("SLOT_NAMES[READER_FONT_SLOT_COUNT] = " + want), std::string::npos)
        << rel << " does not label the six slots XXS..XL in size order";
  }
}

// The eight INSTALLED families must each ship exactly the slot count, or
// buildFontSizeSetting() silently drops to point-size-only labels for them —
// which is not an error, just a picker that stops saying S/M/L.
//
// THIS READS THE RECIPE, NOT THE CARD. sd-fonts.yaml is the build input; the
// .cpfont tree under fs_/fonts/ is only in step with it after
// scripts/install-sim-fonts.py has been re-run. A card still carrying the old
// four-size ramp fails at RUNTIME with this test green, so a green run here
// means "the recipe is right", not "the device is provisioned".
TEST(ReaderFontSizes, EveryInstalledFamilyShipsExactlyTheSlotCount) {
  const std::string yaml = slurp("lib/EpdFont/scripts/sd-fonts.yaml");
  const auto families = installedFamilies(yaml);
  ASSERT_FALSE(families.empty()) << "installed_families: not parsed out of sd-fonts.yaml";
  for (const auto& fam : families) {
    const auto sizes = yamlSizesFor(yaml, fam);
    ASSERT_FALSE(sizes.empty()) << fam << " has no sizes: line";
    EXPECT_EQ(sizes.size(), READER_FONT_SLOT_COUNT)
        << fam << " ships " << sizes.size() << " sizes against " << int(READER_FONT_SLOT_COUNT)
        << " slots, so its picker rows lose their XXS..XL names";
    EXPECT_TRUE(std::is_sorted(sizes.begin(), sizes.end())) << fam << "'s ramp is not ascending";
    EXPECT_EQ(std::set<int>(sizes.begin(), sizes.end()).size(), sizes.size())
        << fam << " repeats a point size, so two slots would resolve to one file";
  }
}

// findClosestReaderSize is what the reader actually selects with, per family.
// Six slots over a six-file family must be one-to-one and monotonic.
TEST(ReaderFontSizes, EachSlotSelectsItsOwnFileInASixSizeFamily) {
  SdCardFontFamilyInfo fam;
  fam.name = "Almendra";
  for (const uint8_t pt : {6, 8, 10, 12, 14, 17}) {
    fam.files.push_back({"/fonts/Almendra/Almendra_" + std::to_string(pt) + ".cpfont", pt, 0});
  }
  uint8_t last = 0;
  for (uint8_t slot = 0; slot < READER_FONT_SLOT_COUNT; ++slot) {
    const auto* f = fam.findClosestReaderSize(slot);
    ASSERT_NE(f, nullptr) << "slot " << int(slot) << " selects nothing";
    EXPECT_GT(f->pointSize, last) << "slot " << int(slot) << " did not select a larger cut than the slot below it";
    last = f->pointSize;
  }
  EXPECT_EQ(fam.availableSizes().size(), READER_FONT_SLOT_COUNT);
}

// A family that has NOT been extended (every buildable-only recipe) still has
// to work: four slots, and the two above them clamp to XL rather than
// selecting nothing.
TEST(ReaderFontSizes, AFourSizeFamilyStillResolvesEverySlot) {
  SdCardFontFamilyInfo fam;
  fam.name = "Arvo";
  for (const uint8_t pt : {12, 14, 16, 18}) {
    fam.files.push_back({"/fonts/Arvo/Arvo_" + std::to_string(pt) + ".cpfont", pt, 0});
  }
  const std::vector<uint8_t> want = {12, 14, 16, 18, 18, 18};
  for (uint8_t slot = 0; slot < READER_FONT_SLOT_COUNT; ++slot) {
    const auto* f = fam.findClosestReaderSize(slot);
    ASSERT_NE(f, nullptr) << "slot " << int(slot) << " selects nothing on a four-size family";
    // Direction matters, not just non-null: the over-range slots must clamp UP
    // to the largest cut. Clamping to the smallest would also be non-null, and
    // would silently shrink the page for anyone on L or XL.
    EXPECT_EQ(f->pointSize, want[slot]) << "slot " << int(slot) << " clamped the wrong way";
  }
}

}  // namespace
