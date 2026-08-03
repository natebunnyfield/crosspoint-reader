#pragma once

#include <cstdint>
#include <cstring>
#include <string>

// Display names, designer credits, dates, and places for SD font families.
//
// An installed family is identified by its DIRECTORY name on the card
// (`/.fonts/<Family>/<Family>_<size>.cpfont`), which is also what
// SETTINGS.sdFontFamilyName persists. Those names are filesystem-safe and often
// squashed or suffixed — `GTAlpinaCond`, `SourceSerif4`, `InknutAntiqua62`,
// `LibreCaslonText` — so they read poorly in a picker. This maps them to a
// properly spaced typeface name plus the designer, years, and places, for
// attribution.
//
// Keyed on the on-disk name deliberately: renaming a family directory would drop
// the font for anyone who has it selected (the setting stores the string), so the
// directory names are effectively frozen and this table is where the presentation
// lives instead.
//
// A family with no entry falls back to its directory name, so an unlisted or
// user-installed font still appears — it just gets no credit. Nothing is hidden.
//
// `years` and `place` are pre-formatted strings in `creation; digital…` form —
// the historical/design side before the semicolon, the digital side after it
// (a born-digital face has one group, no semicolon). They are authored in
// docs/font-dates.md, the source of truth with citations, and copied here
// verbatim. `earliestYear` duplicates the first (creation) year numerically:
// the picker sorts reverse chronologically by it, newest lineage first.
//
// `constexpr` array of pointers to string literals: lives in flash, costs no DRAM
// (Resource Protocol 3/6). Names carry UTF-8 (ø, ß) — the UI faces cover Latin-1,
// verified by rendering the picker.
namespace FontDisplayNames {

struct Entry {
  const char* directory;  // family name as it appears on the SD card
  const char* name;       // typeface name, properly spaced
  const char* designer;   // credited designer
  const char* years;      // "creation; digital…", ascending — from docs/font-dates.md
  const char* place;      // "creation place; digital place" — from docs/font-dates.md
  uint16_t earliestYear;  // first year in `years`, for reverse-chron picker sort
};

// Revivals credit whichever name the face itself doesn't already carry — the
// years tell the rest of the story. Caledonia CC says Carter & Cone, so its
// entry credits Dwiggins; Goudy Bookletter says Goudy, so its entry credits
// Barry Schwartz, its digital designer.
inline constexpr Entry kEntries[] = {
    {"Almendra", "Almendra", "Ana Sanfelippo", "1350; 2011", "London; Buenos Aires", 1350},
    {"CaledoniaCC", "Caledonia CC", "W.A. Dwiggins", "1938; 1988, 1994, 2026",
     "Hingham, Mass.; Cambridge, Mass.", 1938},
    {"Edgar", "Edgar", "Tobias Frere-Jones & Nina St\xC3\xB6ssinger", "1722; 2025", "London; Brooklyn", 1722},
    {"Coelacanth", "Coelacanth", "Ben Whitmore", "1914, 2014", "New York; Waiheke Island, New Zealand", 1914},
    {"GoudyBookletter1911", "Goudy Bookletter", "Barry Schwartz", "1911; 2009", "New York; St. Paul", 1911},
    {"SourceSerif4", "Source Serif 4", "Frank Grie\xC3\x9F"
                                       "hammer",
     "2014, 2021", "Santa Clara, California", 2014},
    {"GTAlpinaCond", "GT Alpina", "Reto Moser", "2011, 2020", "Bern; Lucerne, Switzerland", 2011},
    {"InknutAntiqua62", "Inknut Antiqua", "Claus Eggers S\xC3\xB8rensen", "1469; 2014", "Venice; Amsterdam", 1469},
    {"LibreCaslonText", "Libre Caslon Text", "Pablo Impallari & Rodrigo Fuenzalida", "1722, 2012",
     "London; Rosario, Argentina", 1722},
    {"Lora", "Lora", "Olga Karpushina", "2011, 2019", "Moscow", 2011},
    {"Newsreader", "Newsreader", "Hugues Gentile", "1757; 2020", "Birmingham; Paris", 1757},
    {"Rosarivo", "Rosarivo", "Pablo Ugerman", "1470; 2011", "Venice; Buenos Aires", 1470},
    {"TeXGyreSchola", "TeX Gyre Schola", "Bogus\xC5\x82"
                                         "aw Jackowski & Janusz M. Nowacki",
     "1918; 2007", "Jersey City; Gda\xC5\x84sk", 1918},
    {"Junicode", "Junicode SemiCond", "Peter S. Baker", "1703; 1998, 2023",
     "Oxford; Charlottesville, Virginia", 1703},
    // Born digital, so both years are its own: Atkinson Hyperlegible 2019, the
    // Lexica extension 2024. Credits both hands — the face's name carries
    // neither, unlike Caledonia CC or Goudy Bookletter.
    {"LexicaUltralegible", "Lexica Ultralegible", "Applied Design Works & Jacob Perez", "2019, 2024",
     "New York; El Paso, Texas", 2019},
    // The three installed text grotesques. Every UTF-8 escape below is closed
    // off with a string break because C++ hex escapes are greedy: "\xC3\xA9"
    // followed by the 'c' of "ctor" would otherwise parse as \xA9C. The Turkish
    // ğ/İ/ı are Latin Extended-A, outside Latin-1 — checked against the builtin
    // interval preset, which covers the block, and confirmed in the picker.
    {"HostGrotesk", "Host Grotesk",
     "Do\xC4\x9F" "ukan Karap\xC4\xB1" "nar & \xC4\xB0" "brahim Ka\xC3\xA7" "t\xC4\xB1" "o\xC4\x9F" "lu",
     "2023", "", 2023},
    {"Archivo", "Archivo", "H\xC3\xA9" "ctor Gatti", "2012, 2020", "Buenos Aires", 2012},
    {"LibreFranklin", "Libre Franklin", "Pablo Impallari, Rodrigo Fuenzalida & Nhung Nguyen",
     "1902; 2016", "Jersey City; Rosario, Argentina", 1902},
    {"Venetian301", "Venetian 301", "Bruce Rogers", "1914, 1990", "New York; Cambridge, Mass.", 1914},
};

inline const Entry* find(const char* directory) {
  if (directory == nullptr || directory[0] == '\0') return nullptr;
  for (const auto& e : kEntries) {
    if (std::strcmp(e.directory, directory) == 0) return &e;
  }
  return nullptr;
}

// Earliest (creation) year for a family, for ordering the picker reverse
// chronologically by lineage. 0 for a family not in the table — sorts last.
inline uint16_t earliestYear(const char* directory) {
  const Entry* e = find(directory);
  return e != nullptr ? e->earliestYear : 0;
}

// Typeface name only — for the preview pane and the picker row title.
inline std::string displayName(const std::string& directory) {
  const Entry* e = find(directory.c_str());
  return e != nullptr ? std::string(e->name) : directory;
}

// "Designer · years · place" for the picker row subtitle, each segment in the
// md table's `creation; digital` form. Empty segments are skipped; a family
// not in the table gets an empty subtitle. The theme ellipsizes overflow.
inline std::string subtitle(const std::string& directory) {
  const Entry* e = find(directory.c_str());
  if (e == nullptr) return "";
  std::string out(e->designer);
  for (const char* part : {e->years, e->place}) {
    if (part != nullptr && part[0] != '\0') {
      out += " \xC2\xB7 ";  // middle dot
      out += part;
    }
  }
  return out;
}

}  // namespace FontDisplayNames
