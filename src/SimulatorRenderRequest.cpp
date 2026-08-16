// One call, so the simulator harness can ask for a RE-RENDER without including
// the activity headers.
//
// The harness needs this because SimulatorOverlay::requestPresent() only pushes
// the framebuffer that already exists. When the system appearance flips, the
// harness writes SETTINGS.darkMode -- and everything the firmware DREW from
// that value is still the old pixels, most visibly the System > Dark Mode row,
// which keeps painting "Off" over a dark page until the owner navigates away
// and back.
//
// It cannot include ActivityManager.h itself: that header holds
// unique_ptr<Activity>, so it needs the complete Activity type and drags the
// whole activity header set into an Objective-C++ translation unit for one
// call. A free function is the smaller seam.
//
// Deferred (immediate = false): it sets a flag the manager reads at the end of
// its loop, which is safe from the harness thread and safe before any activity
// exists.
//
// Compiled into every build, including the device. Nothing on an X3 calls it --
// there is no host appearance to follow -- so it costs one unreferenced
// function that --gc-sections drops.
#include "activities/Activity.h"
#include "activities/ActivityManager.h"

extern ActivityManager activityManager;

void crosspointRequestRender() { activityManager.requestUpdate(); }
