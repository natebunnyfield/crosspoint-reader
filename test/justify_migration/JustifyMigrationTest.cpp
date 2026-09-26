// The one-time justification-threshold migration, 40 -> 34 (owner ruling
// 2026-09-26, "One-time migrate 40->34"). The 34 default reaches only a
// settings.json with no threshold key; every device that ever saved settings
// holds a 40 the old default wrote. This rewrites that 40 once, marks the file
// so it never runs again, and leaves every other value alone -- so a 40 chosen
// AFTER the migration sticks.
#include <JustifyThresholdMigration.h>
#include <gtest/gtest.h>

namespace {

justifysetting::Loaded load(const char* json) {
  JsonDocument doc;
  EXPECT_FALSE(deserializeJson(doc, json));
  return justifysetting::read(doc.as<JsonVariantConst>());
}

// save(threshold) then load, the way CrossPointSettings round-trips.
justifysetting::Loaded roundTrip(const uint8_t threshold) {
  JsonDocument out;
  justifysetting::write(out, threshold);
  std::string text;
  serializeJson(out, text);
  EXPECT_NE(text.find("\"justifyMigrated34\":true"), std::string::npos) << text << ": the flag is not written";
  return load(text.c_str());
}

}  // namespace

TEST(JustifyMigration, ASaved40WithNoFlagBecomes34) {
  const auto l = load(R"({"justifyThreshold":40})");
  EXPECT_EQ(l.threshold, 34);
  EXPECT_TRUE(l.needsResave) << "the migration must be persisted, or it is redone on every boot";
}

TEST(JustifyMigration, A40SavedAfterTheMigrationSticks) {
  const auto l = load(R"({"justifyThreshold":40,"justifyMigrated34":true})");
  EXPECT_EQ(l.threshold, 40);
  EXPECT_FALSE(l.needsResave);
}

TEST(JustifyMigration, AnyOtherSavedValueIsUntouchedButGetsTheFlag) {
  for (const int v : {32, 36, 45, 50}) {
    const std::string json = "{\"justifyThreshold\":" + std::to_string(v) + "}";
    const auto l = load(json.c_str());
    EXPECT_EQ(l.threshold, v);
    EXPECT_TRUE(l.needsResave) << v << ": the flag must be written so a later 40 is never migrated";
  }
}

TEST(JustifyMigration, AFileWithNoThresholdGetsTheNewDefault) {
  // A settings.json predating the row, or a fresh one: the default, 34.
  const auto l = load(R"({"fontFamily":0})");
  EXPECT_EQ(l.threshold, 34);
  EXPECT_TRUE(l.needsResave);
}

TEST(JustifyMigration, SaveAndLoadRoundTripsEveryRungWithoutRemigrating) {
  for (int i = 0; i < autojustify::THRESHOLD_CHOICE_COUNT; i++) {
    const uint8_t v = static_cast<uint8_t>(autojustify::THRESHOLD_CHOICES[i]);
    const auto l = roundTrip(v);
    EXPECT_EQ(l.threshold, v) << "rung " << int(v) << " changed across save/load";
    EXPECT_FALSE(l.needsResave);
  }
}

TEST(JustifyMigration, ItRunsExactlyOnce) {
  // 40, no flag -> 34 and resave; the user then picks 40 again; that save and
  // every later load keep 40.
  const auto first = load(R"({"justifyThreshold":40})");
  ASSERT_EQ(first.threshold, 34);
  const auto chosen = roundTrip(40);
  EXPECT_EQ(chosen.threshold, 40);
  EXPECT_EQ(roundTrip(chosen.threshold).threshold, 40);
}
