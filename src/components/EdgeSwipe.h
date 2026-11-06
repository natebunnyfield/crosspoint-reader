// The edge-swipe Back gesture's geometry, pure so it is host-tested
// (test/keyboard_touch) and shared by every board's MappedInputManager.
//
// Back is a left-to-right swipe that starts near the left edge and runs more
// across than down. Edge-anchored so that mid-screen horizontal swipes stay
// available to activities that consume SwipeDir::Left/Right (percent
// selection, the image viewer). Points are in the renderer's LOGICAL portrait
// space, the one MappedInputManager::decodeSwipe maps both endpoints into.
//
// On the BOOX Picco this is the only swipe the firmware takes, on every screen
// (owner 2026-11-06: "Left-edge swipe = Back"); HalGPIO::wasBackSwipeCandidate
// says how that is gated.
//
// Header-only on purpose: a new translation unit breaks the simulator's iOS
// source list (CLAUDE.md).
#pragma once

#include <cstdlib>

namespace edgeswipe {

// The start must lie in the left quarter of the screen.
constexpr float LEFT_EDGE_BACK_FRAC_X = 0.25f;

inline bool isLeftEdgeBack(const int sx, const int sy, const int ex, const int ey, const int screenW) {
  return sx <= screenW * LEFT_EDGE_BACK_FRAC_X && ex > sx && std::abs(ex - sx) > std::abs(ey - sy);
}

}  // namespace edgeswipe
