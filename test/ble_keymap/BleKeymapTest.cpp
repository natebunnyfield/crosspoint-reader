#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "spike/HidKeymap.h"

namespace {

// Feed a sequence of 8-byte boot reports and collect everything decoded.
std::string typeReports(const std::vector<std::vector<uint8_t>>& reports) {
  uint8_t prev[6] = {0};
  std::string out;
  for (const auto& r : reports) {
    char decoded[6];
    const size_t n = hidkeymap::decodeReport(r.data(), prev, decoded, sizeof(decoded));
    out.append(decoded, n);
  }
  return out;
}

std::vector<uint8_t> report(uint8_t mods, std::vector<uint8_t> keys) {
  std::vector<uint8_t> r{mods, 0};
  keys.resize(6, 0);
  r.insert(r.end(), keys.begin(), keys.end());
  return r;
}

}  // namespace

TEST(BleKeymap, LettersAndDigits) {
  EXPECT_EQ(hidkeymap::usageToChar(0x04, 0), 'a');
  EXPECT_EQ(hidkeymap::usageToChar(0x1D, 0), 'z');
  EXPECT_EQ(hidkeymap::usageToChar(0x1E, 0), '1');
  EXPECT_EQ(hidkeymap::usageToChar(0x27, 0), '0');
}

TEST(BleKeymap, ShiftSelectsUpperTable) {
  EXPECT_EQ(hidkeymap::usageToChar(0x04, 0x02), 'A');  // left shift
  EXPECT_EQ(hidkeymap::usageToChar(0x04, 0x20), 'A');  // right shift
  EXPECT_EQ(hidkeymap::usageToChar(0x1E, 0x02), '!');
}

TEST(BleKeymap, ControlCharacters) {
  EXPECT_EQ(hidkeymap::usageToChar(0x28, 0), '\n');  // Enter
  EXPECT_EQ(hidkeymap::usageToChar(0x2A, 0), '\b');  // Backspace
  EXPECT_EQ(hidkeymap::usageToChar(0x2C, 0), ' ');   // Space
}

// Arrow keys and everything past 0x38 are unmapped. 23 such events were logged
// on the X4 (mostly 0x50/0x4F) and must not become garbage characters.
TEST(BleKeymap, UnmappedUsagesProduceNothing) {
  EXPECT_EQ(hidkeymap::usageToChar(0x4F, 0), 0);  // Right arrow
  EXPECT_EQ(hidkeymap::usageToChar(0x50, 0), 0);  // Left arrow
  EXPECT_EQ(hidkeymap::usageToChar(0x00, 0), 0);
  EXPECT_EQ(typeReports({report(0, {0x50}), report(0, {0x4F})}), "");
}

// THE behaviour that makes HOGP usable: a key held across reports must emit
// once. Without this every report while a finger rests on a key repeats it.
TEST(BleKeymap, HeldKeyEmitsOnce) {
  EXPECT_EQ(typeReports({
                report(0, {0x04}),  // 'a' down
                report(0, {0x04}),  // still held
                report(0, {0x04}),  // still held
                report(0, {}),      // released
            }),
            "a");
}

TEST(BleKeymap, ReleaseThenPressEmitsAgain) {
  EXPECT_EQ(typeReports({report(0, {0x04}), report(0, {}), report(0, {0x04})}), "aa");
}

// Real capture from the Geonix on the X4: rolling n-key rollover, where the
// next key arrives before the previous is released.
TEST(BleKeymap, RolloverEmitsEachNewKeyOnce) {
  EXPECT_EQ(typeReports({
                report(0, {0x16}),              // s
                report(0, {0x16, 0x11}),        // s held, n down
                report(0, {0x16, 0x11, 0x17}),  // t down
                report(0, {0x11, 0x17}),        // s released
                report(0, {0x17, 0x0B}),        // n released, h down
                report(0, {}),
            }),
            "snth");
}

TEST(BleKeymap, ErrorRollOverIgnored) { EXPECT_EQ(typeReports({report(0, {0x01, 0x01, 0x01, 0x01, 0x01, 0x01})}), ""); }

// Escape is mapped in the table but must never reach the buffer.
TEST(BleKeymap, EscapeIsNotAnEditorCharacter) { EXPECT_EQ(typeReports({report(0, {0x29})}), ""); }

TEST(BleKeymap, SixSimultaneousKeysAllDecode) {
  EXPECT_EQ(typeReports({report(0, {0x04, 0x05, 0x06, 0x07, 0x08, 0x09})}), "abcdef");
}
