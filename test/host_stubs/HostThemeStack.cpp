// Definitions the real theme stack expects the firmware's main.cpp to provide.
//
// Everything here is a global the theme sources reference by `extern`. The
// classes themselves are the host stubs beside this file (HalClock.h,
// HalPowerManager.h); these are just the singleton instances, in one TU so two
// suites linking the theme stack cannot end up with two copies.
//
// `display` and `gpio` are deliberately NOT defined here: they are the seams a
// suite scripts (test/activity_input/stubs/HalGPIO.h is scriptable input, and
// the panel size decides the layout under test), so each suite defines its own.

#include "HalClock.h"
#include "HalPowerManager.h"

HalClock halClock;
HalPowerManager powerManager;
