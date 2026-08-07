#include "EditorFonts.h"

namespace editorfonts {

const char* selectedFamily(uint8_t index) {
  if (index >= FAMILY_COUNT) return FAMILIES[0].family;
  return FAMILIES[index].family;
}

int builtinFontIdFor(uint8_t index) {
  if (index >= FAMILY_COUNT) return FAMILIES[0].builtinFontId;
  return FAMILIES[index].builtinFontId;
}

int fallbackFontId() {
  // First compiled-in entry, in list order, so this follows the table rather
  // than naming a family twice.
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    if (FAMILIES[i].builtinFontId != 0) return FAMILIES[i].builtinFontId;
  }
  return 0;  // no built-in family at all; caller keeps its own UI-face fallback
}

}  // namespace editorfonts
