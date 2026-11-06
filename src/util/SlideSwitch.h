#pragma once

// BOOX Picco slide switch (upstream 261726f61, generalized in 8f4d035cb).
// Include from FREEINK_DEVICE_PICCO-gated code only: HalGPIO's switch accessors
// exist on that build alone.
#if FREEINK_DEVICE_PICCO

#include <HalGPIO.h>

#include "CrossPointSettings.h"

// A slide switch owns the state of the function it is bound to, so the UI must
// not offer its own toggle for that function or change it behind the switch.
inline bool slideSwitchOwns(const uint8_t action) {
  return gpio.hasToggleSwitch() && SETTINGS.slideSwitchAction == action;
}

inline bool slideSwitchOwnsLight() { return slideSwitchOwns(CrossPointSettings::SLIDE_SWITCH_FRONTLIGHT); }

#endif  // FREEINK_DEVICE_PICCO
