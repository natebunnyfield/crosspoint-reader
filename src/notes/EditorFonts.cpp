#include "EditorFonts.h"

namespace editorfonts {

const char* selectedFamily(uint8_t index) {
  if (index >= FAMILY_COUNT) return FAMILIES[0].family;
  return FAMILIES[index].family;
}

}  // namespace editorfonts
