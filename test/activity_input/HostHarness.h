// Host harness for the activity/input layer.
//
// Owns every global the firmware expects to already exist (display, gpio,
// renderer, mappedInput, activityManager, CrossPointSettings::instance,
// sdFontSystem) plus the frame driver that reproduces src/main.cpp's loop
// shape. UITheme is NOT among them — the real one is linked
// (crosspoint_add_theme_stack). Implementation and rationale for each test
// double: HostHarness.cpp.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

class Activity;
class GfxRenderer;
class HalGPIO;
class MappedInputManager;

namespace host {

// ---- the firmware's collaborators, host-side ----

GfxRenderer& renderer();
MappedInputManager& input();
HalGPIO& buttons();

// ---- frame driving ----

// One iteration of src/main.cpp's loop(): gpio.update() (main.cpp:505) followed
// by activityManager.loop() (main.cpp:599). Button edges are computed inside
// update(), so an edge set up before frame() is visible to exactly this frame's
// loop() and no other — which is the whole point.
void frame();
void frames(int count);

// A complete physical tap of one HARDWARE button (HalGPIO::BTN_*), as two
// frames: press edge, then release edge. Mirrors what a user's finger produces.
void tap(uint8_t hardwareButton);

// The two halves of tap(), for tests that must assert state between the edges.
void pressFrame(uint8_t hardwareButton);
void releaseFrame(uint8_t hardwareButton);

// ---- activity stack ----

// Installs `activity` as the current activity and runs its onEnter(). Only
// valid when nothing is current (call reset() first).
void setRootActivity(std::unique_ptr<Activity>&& activity);

Activity* currentActivity();
// Activity::name of whatever is current ("" when nothing is). Captured by the
// ActivityManager double, since Activity::name is protected.
const std::string& currentActivityName();

// ---- observations ----

struct Counters {
  int transitions = 0;  // applied push + pop + replace: one per activity swap
  int pushes = 0;
  int pops = 0;
  int replaces = 0;
  int goHome = 0;      // ActivityManager::goHome() calls — "user left their book"
  int goToReader = 0;  // ActivityManager::goToReader() calls
  int updates = 0;     // requestUpdate() calls
  int updateAndWaits = 0;
  // requestUpdateAndWait() calls made with the render lock held: the real
  // ActivityManager asserts and the process dies (ActivityManager.cpp:326).
  int updateAndWaitsHoldingRenderLock = 0;
};

const Counters& counters();
void resetCounters();

// SdCardFontSystem::ensureLoaded() is the call that frees the resident
// SdCardFont under the render task's feet, so it must only ever run with the
// render lock held (FontSelectionActivity.cpp:200-220).
struct FontSystemCalls {
  int ensureLoaded = 0;
  int ensureLoadedWithoutRenderLock = 0;
};

const FontSystemCalls& fontSystemCalls();

// Drops the activity stack, zeroes the counters, clears button state and
// restores the CrossPointSettings fields these tests write. Activities still on
// the stack are destroyed WITHOUT onExit() — this is a between-tests reset, not
// a navigation path.
void reset();

}  // namespace host
