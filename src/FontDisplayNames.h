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
// `lineage` is one pre-formatted string of `YEAR PLACE` stages, authored in
// docs/font-dates.md (the source of truth, with citations) and copied here
// verbatim. Punctuation carries the meaning:
//
//   ; separates a distinct stage — the original from its digitisation, or one
//     hand's work from another's
//   , separates the initial release from later revisions WITHIN one stage,
//     which is why the years sit in front of the place they share
//
// So Coelacanth reads "1914 New York; 2014 Waiheke Island, New Zealand" (cut
// in New York, revived in New Zealand) while Source Serif 4 reads
// "2014, 2021 Santa Clara, California" (one place, revised in situ). The year
// and place used to be separate fields joined with a middle dot, which put
// every year in one run and every place in another and left the reader to pair
// them off; this form pairs them at the source. A born-digital face with one
// stage is just "YEAR PLACE", and a face whose place is unknown is bare years.
//
// `earliestYear` duplicates the first year numerically: the picker sorts
// reverse chronologically by it, newest lineage first.
//
// `constexpr` array of pointers to string literals: lives in flash, costs no DRAM
// (Resource Protocol 3/6). Names carry UTF-8 (ø, ß) — the UI faces cover Latin-1,
// verified by rendering the picker.
namespace FontDisplayNames {

struct Entry {
  const char* directory;  // family name as it appears on the SD card
  const char* name;       // typeface name, properly spaced
  const char* designer;   // credited designer
  const char* lineage;    // "YEAR PLACE; YEAR PLACE" stages — from docs/font-dates.md
  uint16_t earliestYear;  // first year in `lineage`, for reverse-chron picker sort
};

// Revivals credit whichever name the face itself doesn't already carry — the
// years tell the rest of the story. Caledonia CC says Carter & Cone, so its
// entry credits Dwiggins; Goudy Bookletter says Goudy, so its entry credits
// Barry Schwartz, its digital designer.
inline constexpr Entry kEntries[] = {
    {"Almendra", "Almendra", "Ana Sanfelippo", "1350 London; 2011 Buenos Aires", 1350},
    // Three revisions in one stage: Carter & Cone recut Dwiggins' Caledonia in
    // Cambridge across 1988, 1994 and 2026, so those years share a place and
    // take commas; the 1938 Linotype original is a separate stage.
    {"CaledoniaCC", "Caledonia CC", "W.A. Dwiggins",
     "1938 Hingham, Mass.; 1988, 1994, 2026 Cambridge, Mass.", 1938},
    {"Edgar", "Edgar", "Tobias Frere-Jones & Nina St\xC3\xB6ssinger", "1722 London; 2025 Brooklyn", 1722},
    {"Coelacanth", "Coelacanth", "Ben Whitmore", "1914 New York; 2014 Waiheke Island, New Zealand", 1914},
    {"GoudyBookletter1911", "Goudy Bookletter", "Barry Schwartz", "1911 New York; 2009 St. Paul", 1911},
    // Born digital and revised where it was drawn, so one stage, comma'd years.
    {"SourceSerif4", "Source Serif 4", "Frank Grie\xC3\x9F"
                                       "hammer",
     "2014, 2021 Santa Clara, California", 2014},
    {"GTAlpinaCond", "GT Alpina", "Reto Moser", "2011 Bern; 2020 Lucerne, Switzerland", 2011},
    {"InknutAntiqua62", "Inknut Antiqua", "Claus Eggers S\xC3\xB8rensen", "1469 Venice; 2014 Amsterdam", 1469},
    {"LibreCaslonText", "Libre Caslon Text", "Pablo Impallari & Rodrigo Fuenzalida",
     "1722 London; 2012 Rosario, Argentina", 1722},
    {"Lora", "Lora", "Olga Karpushina", "2011, 2019 Moscow", 2011},
    {"Newsreader", "Newsreader", "Hugues Gentile", "1757 Birmingham; 2020 Paris", 1757},
    {"Rosarivo", "Rosarivo", "Pablo Ugerman", "1470 Venice; 2011 Buenos Aires", 1470},
    {"TeXGyreSchola", "TeX Gyre Schola", "Bogus\xC5\x82"
                                         "aw Jackowski & Janusz M. Nowacki",
     "1918 Jersey City; 2007 Gda\xC5\x84sk", 1918},
    // Same GUST e-foundry duo as TeX Gyre Schola above. Original: Adam
    // Półtawski's Antykwa Półtawskiego, first cast at Jan Idzkowski's
    // foundry, Warsaw, 1931. Digitized copyright years (2003, 2009) are the
    // font's own name-table string; the CTAN v1.101 release (Oct 2010) is one
    // year of lag and collapses under the adjacent-year rule.
    {"Antpolt", "Antykwa Po\xC5\x82"
                "tawskiego",
     "Bogus\xC5\x82"
     "aw Jackowski & Janusz M. Nowacki",
     "1931 Warsaw; 2003, 2009 Gda\xC5\x84sk", 1931},
    // The directory keeps Arkandis' foundry suffix because it is frozen; the
    // display name drops it, which is what the face calls itself. Model-dated
    // at Warren Chappell's Lydian, the 1938 typeface the font's own name table
    // says it mimics — New York, where Chappell ran his own studio, not ATF's
    // Jersey City plant, since he drew it independently rather than as staff.
    // Digitisation place is bare "France": ADF publishes no city.
    {"LibrisADF", "Libris", "Hirwen Harendal", "1938 New York; 2011 France", 1938},
    {"Junicode", "Junicode SemiCond", "Peter S. Baker",
     "1703 Oxford; 1998, 2023 Charlottesville, Virginia", 1703},
    // Model-dated like the rest of the table, not born-digital: the lineage
    // starts with the grotesque named in Vincent Figgins' 1832 London specimen,
    // then Atkinson Hyperlegible 2019 and the Lexica extension 2024. Credits
    // both modern hands — the face's name carries neither, unlike Caledonia CC
    // or Goudy Bookletter. See docs/font-dates.md, which also records why 1832
    // rather than Caslon IV's 1816.
    // 2019 and 2024 are semicolon'd rather than comma'd: the Lexica extension
    // is a different hand in a different city, which makes it its own stage,
    // not a revision in place the way Caledonia's recuts are.
    {"LexicaUltralegible", "Lexica Ultralegible", "Applied Design Works & Jacob Perez",
     "1832 London; 2019 New York; 2024 El Paso, Texas", 1832},
    // The text grotesques. Libre Franklin is the installed one; Host Grotesk and
    // Archivo are recipe-only since 2026-08-04 but keep their labels, because a
    // card provisioned before that ruling still shows them in the picker.
    //
    // Every UTF-8 escape below is closed off with a string break because C++ hex
    // escapes are greedy: "\xC3\xA9" followed by the 'c' of "ctor" would
    // otherwise parse as \xA9C. The Turkish ğ/İ/ı are Latin Extended-A, outside
    // Latin-1 — checked against the builtin interval preset, which covers the
    // block, and confirmed in the picker.
    {"HostGrotesk", "Host Grotesk",
     "Do\xC4\x9F" "ukan Karap\xC4\xB1" "nar & \xC4\xB0" "brahim Ka\xC3\xA7" "t\xC4\xB1" "o\xC4\x9F" "lu",
     "2023", 2023},  // year alone: Element Type publishes no location
    {"Archivo", "Archivo", "H\xC3\xA9" "ctor Gatti", "2012, 2020 Buenos Aires", 2012},
    {"LibreFranklin", "Libre Franklin", "Pablo Impallari", "1902 Jersey City; 2016 Rosario, Argentina", 1902},
    // Cut back to a recipe on 2026-08-04 and deleted from every surface; the
    // label stays because a card provisioned before that ruling still carries
    // the family. Born digital, one stage. 2004 is the design year from the
    // font's own copyright and head.created; the June 2005 GarageFonts release
    // is a year of lag and collapses under the table's adjacent-year rule.
    {"FreightSans", "Freight Sans", "Joshua Darden", "2004 Brooklyn", 2004},
    // Born digital, 2011: the name points at the Italian 1400s but neither the
    // FONTLOG nor the specimen claims a model for the SANS (it is drawn as a
    // companion to Quattrocento, the serif), and this table does not invent
    // lineage years. One stage, two cities — Impallari in Rosario and Marini in
    // Osimo worked it together, so they are joined rather than semicolon'd,
    // which would read as two dates. Brenda Gallo joined for the 2012 bold and
    // italics and is in docs/font-dates.md, trimmed here to fit the row.
    {"QuattrocentoSans", "Quattrocento Sans", "Pablo Impallari & Igino Marini",
     "2011 Rosario, Argentina & Osimo, Italy", 2011},
    {"Venetian301", "Venetian 301", "Bruce Rogers", "1914 New York; 1990 Cambridge, Mass.", 1914},
    // --- The editor (writing) group ---------------------------------------
    // Colophons for the Editor Font picker, which presents and sorts its list
    // identically to Text Settings (owner ruling 2026-08-09). The keys are the
    // same on-card directory names editorfonts::Entry::family already carries
    // (EditorFonts.h:21), so this needs no second key space and no parallel
    // table — which is what a parallel table would have cost: two tables keyed
    // on the same frozen directory names, free to drift.
    //
    // These five are WRITING faces, not reading families, and listing them here
    // cannot grow the reading picker by a row. Two independent reasons:
    // kEntries is never enumerated — find() at :149 is its only reader — and
    // FontSelectionActivity builds its list from the SD registry, skipping
    // editor families by name before any lookup reaches here
    // (FontSelectionActivity.cpp:128, editorfonts::isEditorFamily).
    //
    // The three iA faces are drawn from IBM Plex Mono — iA says so, and the
    // proportions show it — but they are dated at their OWN 2018 design year,
    // one stage, not model-dated at Plex's 2017. Model-dating in this table is
    // for historical models (1470 Venice, 1722 London); a contemporary
    // derivation by a different hand is its own start. docs/font-dates.md
    // carries the citations and records this as a decision.
    {"SpaceMono", "Space Mono", "Benjamin Critton, Colophon", "2016 London", 2016},
    {"IBMPlexMono", "IBM Plex Mono", "Mike Abbink & Bold Monday", "2017 New York & The Hague", 2017},
    // Dates from iA's own announcement, "A Typographic Christmas" (ia.net,
    // 14 Dec 2018): Mono is "the classic Nitti, designed by Bold Monday";
    // "last year we added iA Writer Duo ... based on IBM Plex"; "this year we
    // add a third font ... called iA Writer Quattro". So the three are NOT one
    // year -- Nitti long predates the others, which is why Mono carries its
    // two-stage lineage the way the reading families do.
    {"iAWriterQuattro", "iA Writer Quattro", "Oliver Reichenstein & Bold Monday", "2018 Zurich & The Hague", 2018},
    {"iAWriterDuo", "iA Writer Duo", "Oliver Reichenstein & Bold Monday", "2017 Zurich & The Hague", 2017},
    {"iAWriterMono", "iA Writer Mono", "Pieter van Rosmalen, Bold Monday", "2009 The Hague; 2018 Zurich", 2009},
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

// "Designer · YEAR PLACE; YEAR PLACE" for the picker row subtitle. One middle
// dot now, not two: the year and place of a stage belong together, so the only
// division left is designer from lineage. A family not in the table gets an
// empty subtitle. The theme wraps this over two lines and ellipsizes overflow.
inline std::string subtitle(const std::string& directory) {
  const Entry* e = find(directory.c_str());
  if (e == nullptr) return "";
  std::string out(e->designer);
  if (e->lineage != nullptr && e->lineage[0] != '\0') {
    out += " \xC2\xB7 ";  // middle dot
    out += e->lineage;
  }
  return out;
}

}  // namespace FontDisplayNames
