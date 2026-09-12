// Two reader-facing controls whose rules are easy to break silently.
//
// 1. TWO CHOICES IS A TOGGLE (owner ruling 2026-09-11: "whenever an option is
//    only two values, just toggle between them instead of a dialog"). The gate
//    lives in exactly one place, settingrow::PICKER_MIN_CHOICES, and two
//    callers read it -- settingrow::activate() to decide whether to open the
//    popup, and settingrow::opensPicker() to label the button hint. If those
//    two ever disagree the hint says "Toggle" and a dialog opens anyway, which
//    is worse than either behaviour on its own, so they are pinned together.
//
// 2. THE LINE SPACING RAMP IS APPEND-ONLY. Every slot is a persisted index,
//    written into settings.json AND into every cached section file's render
//    spec. Inserting a step in the middle re-points saved preferences at a
//    different leading and silently re-paginates books nobody touched, so the
//    three original slots are pinned to the multipliers they shipped with. The
//    ramp is read out of CrossPointSettings.cpp AS TEXT, the same way
//    test/reader_font_sizes reads the font ramp and for the same reason:
//    linking getReaderLineCompression() drags in the settings singleton,
//    ArduinoJson and HalStorage for a question about a `switch`.
//
// steppedLineSpacing itself needs no case here -- ReaderUtils.h carries
// static_asserts over every end and interior step of the ramp, so a slot
// inserted rather than appended fails the BUILD, in CI, before any test runs.

#include <gtest/gtest.h>

#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "activities/settings/SettingRowUi.h"

namespace {

SettingInfo enumWithChoices(const size_t n) {
  SettingInfo s;
  s.type = SettingType::ENUM;
  s.enumValues.assign(n, StrId::STR_NONE_OPT);
  return s;
}

std::string readSource(const char* relative) {
  std::ifstream in(std::string(CROSSPOINT_REPO_ROOT) + "/" + relative);
  EXPECT_TRUE(in.good()) << "could not open " << relative;
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

TEST(TwoChoicesIsAToggle, OneAndTwoChoiceRowsChangeWhereTheyStand) {
  EXPECT_FALSE(settingrow::opensPicker(enumWithChoices(1))) << "a single-choice row has nothing to pick between";
  EXPECT_FALSE(settingrow::opensPicker(enumWithChoices(2)))
      << "two choices flip in place; a dialog to answer a yes/no question is two e-ink "
         "repaints to show what the row's value column already says";
}

TEST(TwoChoicesIsAToggle, ThreeOrMoreStillOpensThePicker) {
  EXPECT_TRUE(settingrow::opensPicker(enumWithChoices(3)));
  EXPECT_TRUE(settingrow::opensPicker(enumWithChoices(7)))
      << "cycling blind through many choices is the worse interaction";
}

TEST(TwoChoicesIsAToggle, TheGateIsOneConstantReadByBothCallers) {
  // opensPicker only labels the hint; activate() decides what actually happens.
  // A literal in either place is how they drift apart.
  const std::string ui = readSource("src/activities/settings/SettingRowUi.h");
  EXPECT_NE(ui.find("choiceCount >= PICKER_MIN_CHOICES"), std::string::npos)
      << "activate() must gate the popup on the shared constant, not a literal";
  EXPECT_NE(ui.find("enumCount() >= PICKER_MIN_CHOICES"), std::string::npos)
      << "opensPicker() must gate the hint on the same constant";
}

TEST(TwoChoicesIsAToggle, ToggleRowsWereAlreadyInPlaceAndStayThatWay) {
  SettingInfo t;
  t.type = SettingType::TOGGLE;
  EXPECT_FALSE(settingrow::opensPicker(t));
}

// --- The line spacing ramp -------------------------------------------------

namespace {

// The multipliers, in enum order, lifted out of getReaderLineCompression().
std::vector<float> rampFromSource() {
  const std::string src = readSource("src/CrossPointSettings.cpp");
  const size_t begin = src.find("float CrossPointSettings::getReaderLineCompression()");
  EXPECT_NE(begin, std::string::npos) << "getReaderLineCompression() not found";
  const size_t end = src.find("\n}", begin);
  const std::string body = src.substr(begin, end - begin);

  // Every slot appears as `case NAME:` followed by its `return N.NNf;`. Read
  // them in the order the enum declares, so a case that is present but wired to
  // the wrong value is caught rather than averaged away.
  const char* names[] = {"TIGHT", "NORMAL", "WIDE", "WIDER", "WIDEST"};
  std::vector<float> ramp;
  for (const char* name : names) {
    const size_t at = body.find(std::string("case ") + name + ":");
    EXPECT_NE(at, std::string::npos) << "no case for " << name;
    if (at == std::string::npos) continue;
    std::smatch m;
    const std::string rest = body.substr(at);
    // EXPECT, not ASSERT: this helper returns a value, so it cannot use the
    // fatal form. A missing return is reported and the slot simply goes
    // unrecorded, which the size check in the caller then fails on.
    if (!std::regex_search(rest, m, std::regex(R"(return\s+([0-9]*\.?[0-9]+)f;)"))) {
      ADD_FAILURE() << "no return for " << name;
      continue;
    }
    ramp.push_back(std::stof(m[1].str()));
  }
  return ramp;
}

}  // namespace

TEST(LineSpacingRamp, EverySlotIsWiredAndTheRampOnlyWidens) {
  const std::vector<float> ramp = rampFromSource();
  ASSERT_EQ(ramp.size(), static_cast<size_t>(CrossPointSettings::LINE_COMPRESSION_COUNT))
      << "a slot was added to LINE_COMPRESSION without a multiplier, so it falls through to "
         "the default and reads as Normal";
  for (size_t i = 1; i < ramp.size(); i++) {
    EXPECT_GT(ramp[i], ramp[i - 1]) << "slot " << i << " is not wider than the one before it; "
                                    << "the chord steps this ramp by value, so its order IS the enum's";
  }
}

TEST(LineSpacingRamp, TheOriginalThreeSlotsKeepTheMeaningTheyShippedWith) {
  const std::vector<float> ramp = rampFromSource();
  ASSERT_GE(ramp.size(), 3u);
  // These three are in settings.json files and in cached section files in the
  // wild. Changing one re-leads books whose owner changed nothing.
  EXPECT_FLOAT_EQ(ramp[CrossPointSettings::TIGHT], 0.95f);
  EXPECT_FLOAT_EQ(ramp[CrossPointSettings::NORMAL], 1.0f);
  EXPECT_FLOAT_EQ(ramp[CrossPointSettings::WIDE], 1.1f);
}

TEST(LineSpacingRamp, TheWideEndIsWorthHaving) {
  const std::vector<float> ramp = rampFromSource();
  // The reason the ramp grew at all: 1.1 was the whole of "more air", which is
  // tighter than ordinary printed fiction and well short of large-print.
  EXPECT_GE(ramp.back(), 1.4f) << "the widest slot should be a genuinely roomy page, not a nudge";
}
