#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "notes/HidKeymap.h"

namespace {

// Feed a sequence of 8-byte boot reports and collect everything decoded.
std::string typeReports(const std::vector<std::vector<uint8_t>>& reports) {
  uint8_t prev[6] = {0};
  std::string out;
  for (const auto& r : reports) {
    int decoded[6];
    const size_t n = hidkeymap::decodeReport(r.data(), prev, decoded, sizeof(decoded));
    for (size_t i = 0; i < n; ++i) {
      if (decoded[i] < 0x100) out.push_back(static_cast<char>(decoded[i]));
    }
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
  EXPECT_EQ(hidkeymap::usageToChar(0x4F, 0), 0);  // Right arrow: no character
  EXPECT_EQ(hidkeymap::usageToChar(0x50, 0), 0);  // Left arrow: no character
  EXPECT_EQ(hidkeymap::usageToChar(0x00, 0), 0);
  // ...and they contribute no TEXT, though they now emit edit keys (below).
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

// Arrow/nav keys become EditKey codes rather than being dropped. 23 of these
// were logged on the X4 with no cursor to move.
TEST(BleKeymap, NavigationClusterBecomesEditKeys) {
  EXPECT_EQ(hidkeymap::usageToEditKey(0x4F), hidkeymap::KEY_RIGHT);
  EXPECT_EQ(hidkeymap::usageToEditKey(0x50), hidkeymap::KEY_LEFT);
  EXPECT_EQ(hidkeymap::usageToEditKey(0x51), hidkeymap::KEY_DOWN);
  EXPECT_EQ(hidkeymap::usageToEditKey(0x52), hidkeymap::KEY_UP);
  EXPECT_EQ(hidkeymap::usageToEditKey(0x4A), hidkeymap::KEY_HOME);
  EXPECT_EQ(hidkeymap::usageToEditKey(0x4D), hidkeymap::KEY_END);
  EXPECT_EQ(hidkeymap::usageToEditKey(0x4B), hidkeymap::KEY_PAGE_UP);
  EXPECT_EQ(hidkeymap::usageToEditKey(0x4E), hidkeymap::KEY_PAGE_DOWN);
  EXPECT_EQ(hidkeymap::usageToEditKey(0x4C), hidkeymap::KEY_DELETE);
  EXPECT_EQ(hidkeymap::usageToEditKey(0x04), 0);  // 'a' is not a nav key
}

TEST(BleKeymap, EditKeysAreAboveAscii) {
  uint8_t prev[6] = {0};
  int out[6];
  const auto r = report(0, {0x50});
  const size_t n = hidkeymap::decodeReport(r.data(), prev, out, 6);
  ASSERT_EQ(n, 1u);
  EXPECT_EQ(out[0], hidkeymap::KEY_LEFT);
  EXPECT_GE(out[0], 0x100);  // never collides with a character
}
