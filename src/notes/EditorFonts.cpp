#include "EditorFonts.h"

#include <strings.h>

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

bool isEditorFamily(const char* family) {
  if (family == nullptr) return false;
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    if (strcasecmp(family, FAMILIES[i].family) == 0) return true;
  }
  return false;
}

std::vector<uint8_t> displayOrder() {
  std::vector<uint8_t> order;
  order.reserve(FAMILY_COUNT);
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    if (FAMILIES[i].builtinFontId != 0) order.push_back(static_cast<uint8_t>(i));
  }
  for (size_t i = 0; i < FAMILY_COUNT; ++i) {
    if (FAMILIES[i].builtinFontId == 0) order.push_back(static_cast<uint8_t>(i));
  }
  return order;
}

int resolve(uint8_t index, const std::function<bool(int)>& isRegistered,
            const std::function<int(const char*)>& sdLookup, int uiFallbackId) {
  // Built-in first -- but only if this build actually compiled it in. A font id
  // is just an int; holding one implies nothing about the family being present.
  if (const int builtin = builtinFontIdFor(index); builtin != 0 && isRegistered && isRegistered(builtin)) {
    return builtin;
  }

  if (sdLookup) {
    if (const int id = sdLookup(selectedFamily(index)); id != 0) return id;
  }

  // A card-only row whose family is not installed degrades to a built-in
  // MONOSPACE face rather than to UI chrome -- again only if it is really there.
  if (const int mono = fallbackFontId(); mono != 0 && isRegistered && isRegistered(mono)) return mono;

  // OMIT_FONTS builds land here: no editor face exists in the binary at all, so
  // the caller's UI face is the only thing that can draw. Chrome is the wrong
  // texture for a writing surface, but it is text on screen instead of a blank
  // page.
  return uiFallbackId;
}

}  // namespace editorfonts
