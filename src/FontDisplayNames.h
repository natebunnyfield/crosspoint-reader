#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// Display names, designer credits, and dates for SD font families.
//
// An installed family is identified by its DIRECTORY name on the card
// (`/.fonts/<Family>/<Family>_<size>.cpfont`), which is also what
// SETTINGS.sdFontFamilyName persists. Those names are filesystem-safe and often
// squashed or suffixed — `GTAlpinaCond`, `SourceSerif4`, `InknutAntiqua62`,
// `LibreCaslonText` — so they read poorly in a picker. This maps them to a
// properly spaced typeface name plus the designer and years, for attribution.
//
// Keyed on the on-disk name deliberately: renaming a family directory would drop
// the font for anyone who has it selected (the setting stores the string), so the
// directory names are effectively frozen and this table is where the presentation
// lives instead.
//
// A family with no entry falls back to its directory name, so an unlisted or
// user-installed font still appears — it just gets no credit. Nothing is hidden.
//
// Years: `originalYear` is when the design was first drawn or cut — for a
// revival that is the historical model (Caslon 1722, Centaur 1914), for a
// born-digital face it is the design's debut. `digitalYear` is when THIS
// digital version was released. Equal years collapse to one in the subtitle,
// and 0 means unknown and is omitted.
//
// `constexpr` array of pointers to string literals: lives in flash, costs no DRAM
// (Resource Protocol 3/6). Names carry UTF-8 (ø, ß) — the UI faces cover Latin-1,
// verified by rendering the picker.
namespace FontDisplayNames {

struct Entry {
  const char* directory;  // family name as it appears on the SD card
  const char* name;       // typeface name, properly spaced
  const char* designer;   // credited designer
  uint16_t originalYear;  // year the design was originally drawn/cut (0 = unknown)
  uint16_t digitalYear;   // year this digital version was released (0 = unknown)
};

// Revivals credit whichever name the face itself doesn't already carry — the
// years tell the rest of the story. Caledonia CC says Carter & Cone, so its
// entry credits Dwiggins; Goudy Bookletter 1911 says Goudy, so its entry
// credits Barry Schwartz, its digital designer.
inline constexpr Entry kEntries[] = {
    {"Almendra", "Almendra", "Ana Sanfelippo", 2011, 2011},
    {"CalendoniaCC", "Caledonia CC", "W.A. Dwiggins", 1938, 2026},
    {"Edgar", "Edgar", "Tobias Frere-Jones & Nina St\xC3\xB6ssinger", 2025, 2025},
    {"Coelacanth", "Coelacanth", "Ben Whitmore", 1914, 2014},
    {"GoudyBookletter1911", "Goudy Bookletter 1911", "Barry Schwartz", 1911, 2008},
    {"SourceSerif4", "Source Serif 4", "Frank Grie\xC3\x9F"
                                       "hammer",
     2014, 2021},
    {"GTAlpinaCond", "GT Alpina", "Reto Moser", 2011, 2020},
    {"InknutAntiqua62", "Inknut Antiqua", "Claus Eggers S\xC3\xB8rensen", 2014, 2014},
    {"LibreCaslonText", "Libre Caslon Text", "Pablo Impallari & Rodrigo Fuenzalida", 1722, 2012},
    {"Lora", "Lora", "Olga Karpushina", 2011, 2011},
    {"Newsreader", "Newsreader", "Hugues Gentile", 2020, 2021},
    {"Rosarivo", "Rosarivo", "Pablo Ugerman", 2011, 2011},
    {"Junicode", "Junicode SemiCond", "Peter S. Baker", 1703, 2001},
    {"Venetian301", "Venetian 301", "Bruce Rogers", 1914, 1990},
};

inline const Entry* find(const char* directory) {
  if (directory == nullptr || directory[0] == '\0') return nullptr;
  for (const auto& e : kEntries) {
    if (std::strcmp(e.directory, directory) == 0) return &e;
  }
  return nullptr;
}

// Typeface name only — for the preview pane and the picker row title.
inline std::string displayName(const std::string& directory) {
  const Entry* e = find(directory.c_str());
  return e != nullptr ? std::string(e->name) : directory;
}

// "Designer · original / digital" for the picker row subtitle. A born-digital
// face (equal years) shows a single year; zero years are omitted; a family not
// in the table gets an empty subtitle.
inline std::string subtitle(const std::string& directory) {
  const Entry* e = find(directory.c_str());
  if (e == nullptr) return "";
  std::string out(e->designer);
  char years[16];
  if (e->originalYear != 0 && e->digitalYear != 0 && e->originalYear != e->digitalYear) {
    snprintf(years, sizeof(years), "%u / %u", e->originalYear, e->digitalYear);
  } else if (e->digitalYear != 0 || e->originalYear != 0) {
    snprintf(years, sizeof(years), "%u", e->digitalYear != 0 ? e->digitalYear : e->originalYear);
  } else {
    years[0] = '\0';
  }
  if (years[0] != '\0') {
    out += " \xC2\xB7 ";  // middle dot
    out += years;
  }
  return out;
}

}  // namespace FontDisplayNames
