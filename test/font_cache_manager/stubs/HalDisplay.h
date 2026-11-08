// FontCacheManager.h includes HalDisplay.h only for CROSSPOINT_RENDER_SCALE
// (the simulator's HalDisplay.h defines it above 1). This suite is device
// scale, so the macro is all it needs.
#pragma once

#ifndef CROSSPOINT_RENDER_SCALE
#define CROSSPOINT_RENDER_SCALE 1
#endif
