// Owner ruling 2026-08-05: the EDITOR font group is its own list, chosen by a
// CrossPoint setting under Text Settings. These are writing faces, NOT reading
// faces — they do not join the six-family reading S tier, and a card carrying
// them is not thereby carrying a seventh reading family.
//
// Order is the persisted encoding of SETTINGS.editorFont — APPEND ONLY.
// Recipes live in lib/EpdFont/scripts/sd-fonts.yaml under "Editor group".
#pragma once

#include <stddef.h>

namespace editorfonts {

struct Entry {
  const char* family;  // sd-fonts.yaml family name == on-card directory name
  const char* label;   // picker label
};

// All five are OFL. The iA faces are the "S" (narrow) cuts.
inline constexpr Entry FAMILIES[] = {
    {"iAWriterQuattro", "iA Writer Quattro"}, {"iAWriterDuo", "iA Writer Duo"},
    {"iAWriterMono", "iA Writer Mono"},       {"SpaceMono", "Space Mono"},
    {"IBMPlexMono", "IBM Plex Mono"},
};
inline constexpr size_t FAMILY_COUNT = sizeof(FAMILIES) / sizeof(FAMILIES[0]);

}  // namespace editorfonts
