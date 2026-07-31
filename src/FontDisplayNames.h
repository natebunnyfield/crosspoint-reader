#pragma once

#include <cstring>
#include <string>

// Display names and designer credits for SD font families.
//
// An installed family is identified by its DIRECTORY name on the card
// (`/.fonts/<Family>/<Family>_<size>.cpfont`), which is also what
// SETTINGS.sdFontFamilyName persists. Those names are filesystem-safe and often
// squashed or suffixed — `GTAlpinaCond`, `SourceSerif4`, `InknutAntiqua62`,
// `LibreCaslonText` — so they read poorly in a picker. This maps them to a
// properly spaced typeface name plus the designer, for attribution.
//
// Keyed on the on-disk name deliberately: renaming a family directory would drop
// the font for anyone who has it selected (the setting stores the string), so the
// directory names are effectively frozen and this table is where the presentation
// lives instead.
//
// A family with no entry falls back to its directory name, so an unlisted or
// user-installed font still appears — it just gets no credit. Nothing is hidden.
//
// `constexpr` array of pointers to string literals: lives in flash, costs no DRAM
// (Resource Protocol 3/6). Names carry UTF-8 (ø, ß) — the UI faces cover Latin-1,
// verified by rendering the picker.
namespace FontDisplayNames {

struct Entry {
  const char* directory;  // family name as it appears on the SD card
  const char* name;       // typeface name, properly spaced
  const char* designer;   // credited designer
};

inline constexpr Entry kEntries[] = {
    {"Almendra", "Almendra", "Ana Sanfelippo"},
    {"Edgar", "Edgar", "Tobias Frere-Jones"},
    {"Coelacanth", "Coelacanth", "Ben Whitmore"},
    {"SourceSerif4", "Source Serif 4", "Frank Grie\xC3\x9F"
                                       "hammer"},
    {"GTAlpinaCond", "GT Alpina", "Reto Moser"},
    {"InknutAntiqua62", "Inknut Antiqua", "Claus Eggers S\xC3\xB8rensen"},
    {"LibreCaslonText", "Libre Caslon Text", "Pablo Impallari"},
    {"Lora", "Lora", "Olga Karpushina"},
    {"Newsreader", "Newsreader", "Hugues Gentile"},
    {"Rosarivo", "Rosarivo", "Pablo Ugerman"},
};

inline const Entry* find(const char* directory) {
  if (directory == nullptr || directory[0] == '\0') return nullptr;
  for (const auto& e : kEntries) {
    if (std::strcmp(e.directory, directory) == 0) return &e;
  }
  return nullptr;
}

// Typeface name only — for the preview pane, which already shows the resolved
// point size and would read oddly with a designer credit inside its quotes.
inline std::string displayName(const std::string& directory) {
  const Entry* e = find(directory.c_str());
  return e != nullptr ? std::string(e->name) : directory;
}

// "Typeface - Designer", for the picker list. Falls back to the bare directory
// name when the family is not in the table.
inline std::string displayLabel(const std::string& directory) {
  const Entry* e = find(directory.c_str());
  if (e == nullptr) return directory;
  return std::string(e->name) + " - " + e->designer;
}

}  // namespace FontDisplayNames
