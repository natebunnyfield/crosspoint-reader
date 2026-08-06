// HID boot-keyboard report decoding, extracted so it can be unit-tested on the
// host. Deliberately free of Arduino/NimBLE dependencies: this is the one part
// of the BLE path that is pure logic, and therefore the one part that does not
// need a keyboard, a radio, or a device to verify.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace hidkeymap {

// US layout, HID usage 0x04..0x38. Index = usage - KEY_FIRST.
inline constexpr char KEYMAP_LOWER[] =
    "abcdefghijklmnopqrstuvwxyz"  // 0x04..0x1D
    "1234567890"                  // 0x1E..0x27
    "\n\x1b\b\t "                 // 0x28..0x2C  enter esc bksp tab space
    "-=[]\\#;'`,./";              // 0x2D..0x38  (0x32 is non-US '#')
inline constexpr char KEYMAP_UPPER[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "!@#$%^&*()"
    "\n\x1b\b\t "
    "_+{}|~:\"~<>?";
static_assert(sizeof(KEYMAP_LOWER) == sizeof(KEYMAP_UPPER), "keymap length mismatch");

inline constexpr uint8_t KEY_FIRST = 0x04;
inline constexpr uint8_t KEY_LAST = KEY_FIRST + sizeof(KEYMAP_LOWER) - 2;
inline constexpr uint8_t MOD_SHIFT_MASK = 0x22;  // left | right shift

// Editing keys that have no character. Emitted as private-use codes above the
// ASCII range so one ring can carry both text and navigation without a second
// channel — the editor switches on these, everything else treats them as text.
enum EditKey : int {
  KEY_RIGHT = 0x100,
  KEY_LEFT,
  KEY_DOWN,
  KEY_UP,
  KEY_HOME,
  KEY_END,
  KEY_PAGE_UP,
  KEY_PAGE_DOWN,
  KEY_DELETE,
};

// HID usages 0x4A..0x52 are the navigation cluster. Returns 0 when not one.
inline int usageToEditKey(uint8_t usage) {
  switch (usage) {
    case 0x4A:
      return KEY_HOME;
    case 0x4B:
      return KEY_PAGE_UP;
    case 0x4C:
      return KEY_DELETE;
    case 0x4D:
      return KEY_END;
    case 0x4E:
      return KEY_PAGE_DOWN;
    case 0x4F:
      return KEY_RIGHT;
    case 0x50:
      return KEY_LEFT;
    case 0x51:
      return KEY_DOWN;
    case 0x52:
      return KEY_UP;
    default:
      return 0;
  }
}

// One decoded character, or 0 when the usage has no mapping.
inline char usageToChar(uint8_t usage, uint8_t modifiers) {
  if (usage < KEY_FIRST || usage > KEY_LAST) return 0;
  const bool shift = (modifiers & MOD_SHIFT_MASK) != 0;
  return shift ? KEYMAP_UPPER[usage - KEY_FIRST] : KEYMAP_LOWER[usage - KEY_FIRST];
}

// Decode one 8-byte boot-protocol report against the previous one, emitting
// only 0->1 key transitions: HOGP has no key-repeat, the host is expected to
// synthesise it, and a held key re-appears in every report until released.
//
// `report` must be the 8-byte [modifiers][reserved][6 usages] block; a 9-byte
// report carrying a leading report ID should be passed at +1. `prevKeys` is the
// previous report's 6 usage bytes and is UPDATED for the next call.
// Returns the number of items written to `out` (at most 6); each is either a
// character or an EditKey code.
inline size_t decodeReport(const uint8_t* report, uint8_t* prevKeys, int* out, size_t outCap) {
  const uint8_t mods = report[0];
  const uint8_t* keys = report + 2;
  size_t written = 0;

  for (size_t i = 0; i < 6; ++i) {
    const uint8_t usage = keys[i];
    if (usage == 0 || usage == 0x01) continue;  // empty slot / ErrorRollOver

    bool wasDown = false;
    for (size_t j = 0; j < 6; ++j) {
      if (prevKeys[j] == usage) {
        wasDown = true;
        break;
      }
    }
    if (wasDown) continue;

    const int edit = usageToEditKey(usage);
    if (edit != 0) {
      if (written < outCap) out[written++] = edit;
      continue;
    }

    const char c = usageToChar(usage, mods);
    if (c == 0 || c == 0x1b) continue;  // unmapped, or Esc (not an editor char)
    if (written < outCap) out[written++] = static_cast<unsigned char>(c);
  }

  for (size_t i = 0; i < 6; ++i) prevKeys[i] = keys[i];
  return written;
}

}  // namespace hidkeymap
