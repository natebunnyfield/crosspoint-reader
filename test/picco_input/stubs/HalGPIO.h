// The least of lib/hal/HalGPIO.h that src/MappedInputManager.h needs to
// compile, so test/picco_input can include it for its Button enum. Nothing
// here is called: the suite exercises src/components/PiccoInput.h, which is
// pure. Shadows the real header (it pulls in the SDK's InputManager and
// Arduino).
#pragma once

#include <cstdint>
#include <string>

class HalGPIO {
 public:
  enum class TextEntryLines : uint8_t { Single, Multi };
  void setTextEntryActive(bool, TextEntryLines = TextEntryLines::Single) {}
  bool consumeTypedText(std::string&) { return false; }
  bool isHostKeyboardVisible() const { return false; }
#if FREEINK_DEVICE_PICCO
  void setKeyboardTouch(bool) {}
  void suppressTouchContact() {}
#endif

  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;
};
