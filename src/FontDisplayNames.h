#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

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
  // Stages after which a BLANK LINE separates one typeface's attribution from
  // the next's. 0 — the value every row that omits it gets — means no break,
  // which is every family whose stages are all one typeface's story.
  //
  // It exists for the compound families, today only Inknut Antiqua + Junicode,
  // whose four stages are two typefaces with a model and a digitisation each.
  // Run together they read as one four-step lineage of one face, which is
  // exactly wrong: nothing in "1469 Venice / 2014 Amsterdam / 1703 Oxford /
  // 1998, 2023 Charlottesville" says the third line starts a second typeface
  // (owner 2026-08-24, "put a line to space out between inknut+junicode
  // metainfo").
  //
  // A stage COUNT rather than a flag on the stage, so the field means the same
  // thing whatever the stages are. A hypothetical three-typeface row would
  // need a second break and therefore a different mechanism; that is a
  // deliberate refusal to build for a case that does not exist.
  //
  // DEFAULTED rather than left to aggregate zero-init: every one of the ~30
  // rows below omits it, and without the initializer each one draws
  // -Wmissing-field-initializers. Thirty warnings that mean nothing is how a
  // warning that means something gets missed.
  uint8_t groupBreakAfter = 0;
};

// Revivals credit whichever name the face itself doesn't already carry — the
// years tell the rest of the story. Caledonia CC says Carter & Cone, so its
// entry credits Dwiggins; Goudy Bookletter says Goudy, so its entry credits
// Barry Schwartz, its digital designer.
inline constexpr Entry kEntries[] = {
    // ONE STAGE, and the row it replaces had two. Almendra is an ORIGINAL
    // design, not a revival, so Sanfelippo is the author rather than a
    // digitiser and there is no empty first slot for a model.
    //
    // The old "1350 London" stage was the table's own inference and never a
    // source's claim -- docs/font-dates.md carried it under a standing â  that
    // called it uncited and a candidate for removal. It was removed on
    // 2026-08-24, when the face was promoted to installed_families and its
    // date started deciding where it sorts. Three primary sources, none of
    // which dates a model:
    //
    //   * the shipped TTFs' own name ID 0 -- "Copyright (c) 2011-2012, Ana
    //     Sanfelippo (anasanfe@gmail.com), with Reserved Font Name 'Almendra'"
    //     -- two adjacent years, so 2011 under the adjacent-year rule;
    //   * google/fonts ofl/almendra METADATA.pb, date_added 2011-12-19;
    //   * the face's own DESCRIPTION.en_us.html, which names "the chancery and
    //     gothic hands" -- classes of hands worked by many anonymous scribes
    //     over centuries, not one dated model, and no country.
    //
    // Buenos Aires is where it was drawn: it was Sanfelippo's graduation
    // typeface at the CDT UBA typography postgraduate program.
    //
    // OWNER RULING 2026-08-24: "use 1350 London for origin of Alemendra."
    // Restored after a research pass had removed it, so the reasoning on both
    // sides is recorded here rather than lost.
    //
    // What the sources say: the shipped TTFs' name table reads 2011-2012 Ana
    // Sanfelippo, Google Fonts' metadata gives 2011-12-19, and the upstream
    // description cites chancery and gothic HANDS -- classes of anonymous
    // scribal writing rather than a dated face. No source names a 1350 model or
    // London, and docs/font-dates.md had flagged that stage as uncited since
    // 2026-08-12. So this stage is an OWNER RULING, not a citation, and
    // font-dates.md says so; do not quote it as evidence for anything else.
    //
    // What it does: 1350 is the sort key, so the face sits at the old end of
    // the picker beside Inknut rather than leading it. A 2011 key put it first,
    // which is where an original 2011 face belongs -- and that placement is
    // what the ruling changes.
    {"Almendra", "Almendra", "; Ana Sanfelippo", "1350 London; 2011 Buenos Aires", 1350},
    // Three revisions in one stage: Carter & Cone recut Dwiggins' Caledonia in
    // Cambridge across 1988, 1994 and 2026, so those years share a place and
    // take commas; the 1938 Linotype original is a separate stage. Dwiggins drew
    // that original, so he is stage 1, and stage 2 is left empty on two grounds:
    // the face's own name already says Carter & Cone, and the stage spans three
    // hands anyway (1988 David Berlow at Adobe/Linotype, 1994 and 2026 Carter &
    // Cone), so no single individual belongs on the line.
    {"CaledoniaCC", "Caledonia CC", "W.A. Dwiggins;", "1938 Hingham, Mass.; 1988, 1994, 2026 Cambridge, Mass.", 1938},
    {"Edgar", "Edgar", "William Caslon & Alexander Phemister; Tobias Frere-Jones & Nina St\xC3\xB6ssinger",
     "1722 London; 2025 Brooklyn", 1722},
    {"Coelacanth", "Coelacanth", "Bruce Rogers; Ben Whitmore", "1914 New York; 2014 Waiheke Island, New Zealand", 1914},
    {"GoudyBookletter1911", "Goudy Bookletter", "Frederic W. Goudy; Barry Schwartz", "1911 New York; 2009 St. Paul",
     1911},
    // Born digital and revised where it was drawn, so one stage, comma'd years.
    {"SourceSerif4", "Source Serif 4",
     "Frank Grie\xC3\x9F"
     "hammer",
     "2014, 2021 Santa Clara, California", 2014},
    // The one family that repeats a name across both stages, and it is correct:
    // there is no historical model here. Moser drew the 2011 Bern one-off, then
    // spent eight years expanding it into Grilli Type's 2020 release ("GT Alpina
    // by Reto Moser" on the foundry's own specimen). The semicolon separates two
    // cities of one designer's work, not an original from a digitiser.
    {"GTAlpinaCond", "GT Alpina", "Reto Moser; Reto Moser", "2011 Bern; 2020 Lucerne, Switzerland", 2011},
    // Stage 1 empty: docs/font-dates.md declines to promote de Spira, calling the
    // 1469 model the table's inference rather than S\xC3\xB8rensen's claim. Same
    // placement the InknutJunicode row below already uses.
    {"InknutAntiqua62", "Inknut Antiqua", "; Claus Eggers S\xC3\xB8rensen", "1469 Venice; 2014 Amsterdam", 1469},
    // TWO TYPEFACES, credited roman-first: Inknut Antiqua sets regular and
    // bold, Junicode supplies the real cut italics Inknut's foundry repo has
    // at no weight.
    // FOUR stages, one line each: two typefaces, each with a model and a
    // digitisation, roman first. Stages 1 and 3 carry no person on purpose —
    // Inknut's Venetian model is credited to no designer (docs/font-dates.md
    // declines to promote de Spira, the table's inference rather than
    // S\xC3\xB8rensen's claim) and Junicode's model is Oxford University Press,
    // an institution. Junicode's revision year is restored here: with a line
    // per stage there is no longer a two-line budget forcing it out.
    // The trailing 2 is `groupBreakAfter`: a blank line after stage 2, which is
    // where Inknut's story ends and Junicode's begins. The ONLY row in the
    // table that sets it.
    {"InknutJunicode", "Inknut Antiqua + Junicode", "; Claus Eggers S\xC3\xB8rensen; ; Peter S. Baker",
     "1469 Venice; 2014 Amsterdam; 1703 Oxford; 1998, 2023 Charlottesville, Virginia", 1469, 2},
    {"LibreCaslonText", "Libre Caslon Text", "William Caslon; Pablo Impallari & Rodrigo Fuenzalida",
     "1722 London; 2012 Rosario, Argentina", 1722},
    {"Lora", "Lora", "Olga Karpushina", "2011, 2019 Moscow", 2011},
    // Stage 1 empty: docs/font-dates.md adds no original author here and flags the
    // 1757 Baskerville model itself as uncited (owner ruling 2026-08-12, ship as
    // written). Levée joins Gentile on the 2020 stage per the same ruling —
    // Production Type credits both. The literal is split before the 'e' because a
    // hex escape swallows any hex digit that follows it.
    {"Newsreader", "Newsreader",
     "; Hugues Gentile & Jean-Baptiste Lev\xC3\xA9"
     "e",
     "1757 Birmingham; 2020 Paris", 1757},
    // Stage 1 empty: Jenson is NOT promoted -- docs/font-dates.md flags the 1470
    // Venice model as uncited (the face is named for Raúl Rosarivo, a Gutenberg
    // scholar, which points at Mainz). Ugerman is the 2011 Buenos Aires designer.
    {"Rosarivo", "Rosarivo", "; Pablo Ugerman", "1470 Venice; 2011 Buenos Aires", 1470},
    {"TeXGyreSchola", "TeX Gyre Schola",
     "Morris Fuller Benton; Bogus\xC5\x82"
     "aw Jackowski & Janusz M. Nowacki",
     "1918 Jersey City; 2007 Gda\xC5\x84sk", 1918},
    // Same GUST e-foundry duo as TeX Gyre Schola above. Original: Adam
    // Półtawski's Antykwa Półtawskiego, first cast at Jan Idzkowski's
    // foundry, Warsaw, 1931. Digitized copyright years (2003, 2009) are the
    // font's own name-table string; the CTAN v1.101 release (Oct 2010) is one
    // year of lag and collapses under the adjacent-year rule.
    // Display name had lost its o-acute: "Po\xC5\x82tawskiego" rendered
    // "Poltawskiego". It is Po\xC3\xB3\xC5\x82tawskiego, spelled that way
    // throughout docs/font-dates.md.
    {"Antpolt",
     "Antykwa P\xC3\xB3\xC5\x82"
     "tawskiego",
     "Adam P\xC3\xB3\xC5\x82"
     "tawski; Bogus\xC5\x82"
     "aw Jackowski & Janusz M. Nowacki",
     "1931 Warsaw; 2003, 2009 Gda\xC5\x84sk", 1931},
    // The directory keeps Arkandis' foundry suffix because it is frozen; the
    // display name drops it, which is what the face calls itself. Model-dated
    // at Warren Chappell's Lydian, the 1938 typeface the font's own name table
    // says it mimics — New York, where Chappell ran his own studio, not ATF's
    // Jersey City plant, since he drew it independently rather than as staff.
    // Digitisation place is bare "France": ADF publishes no city.
    {"LibrisADF", "Libris", "Warren Chappell; Hirwen Harendal", "1938 New York; 2011 France", 1938},
    // Stage 1 empty: the model is Oxford University Press' Pica Roman, an
    // institution, and the punchcutter is unrecorded (Baker's own design history
    // names nobody; do not "fix" it with Peter de Walpergen, who cut the Fell
    // Pica that Baker explicitly rules out). Baker is the digitiser.
    {"Junicode", "Junicode SemiCond", "; Peter S. Baker", "1703 Oxford; 1998, 2023 Charlottesville, Virginia", 1703},
    // Model-dated like the rest of the table, not born-digital: the lineage
    // starts with the sans serif named in Vincent Figgins' 1832 London specimen
    // ("sans serif" is Figgins' own word; "grotesque" is Thorowgood's, c. 1834 —
    // docs/font-dates.md owner ruling 2026-08-12 fixed that wording), then
    // Atkinson Hyperlegible 2019 and the Lexica extension 2024. Credits both
    // modern hands — the face's name carries neither, unlike Caledonia CC or
    // Goudy Bookletter. See docs/font-dates.md, which also records why 1832
    // rather than Caslon IV's 1816.
    // 2019 and 2024 are semicolon'd rather than comma'd: the Lexica extension
    // is a different hand in a different city, which makes it its own stage,
    // not a revision in place the way Caledonia's recuts are.
    // Three stages, three designer slots. Stage 1 is empty: no individual is
    // recorded for the 1832 sans, and Figgins published and named it rather than
    // cutting it. The two modern credits split onto the stages they belong to.
    // FLAGGED: "Applied Design Works" is a studio, and docs/font-dates.md's own
    // rule says only individuals go in this column. Its Basis cell names Elliott
    // Scott (lead designer) and Craig Dobie (creative director) for the 2019
    // Atkinson Hyperlegible, either of which would be a change of attribution
    // rather than a placement, so it awaits an owner ruling and the studio name
    // stays exactly as the table has it.
    // Stage 2 credits the people, not the studio: docs/font-dates.md's own
    // Designer-column rule says only individuals belong there, and the row's
    // Basis names them — Elliott Scott led the 2019 Atkinson Hyperlegible with
    // Craig Dobie as creative director. Same shape as Newsreader's pairing of a
    // designer with a director. 1832 stays empty: no individual is recorded for
    // the Figgins-specimen model.
    {"LexicaUltralegible", "Lexica Ultralegible", "; Elliott Scott & Craig Dobie; Jacob Perez",
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
     "Do\xC4\x9F"
     "ukan Karap\xC4\xB1"
     "nar & \xC4\xB0"
     "brahim Ka\xC3\xA7"
     "t\xC4\xB1"
     "o\xC4\x9F"
     "lu",
     "2023", 2023},  // year alone: Element Type publishes no location
    {"Archivo", "Archivo",
     "H\xC3\xA9"
     "ctor Gatti",
     "2012, 2020 Buenos Aires", 2012},
    {"LibreFranklin", "Libre Franklin", "Morris Fuller Benton; Pablo Impallari, Rodrigo Fuenzalida & Nhung Nguyen",
     "1902 Jersey City; 2016 Rosario, Argentina", 1902},
    // The neo-grotesque, installed 2026-08-23. Same GUST e-foundry duo, same
    // license and same CTAN shelf as TeX Gyre Schola above, pointed at the
    // other end of the base-35 set.
    //
    // WHAT IS BEING NAMED, stage 1: Helvetica, not Nimbus Sans. Schola's row
    // already settled this shape — it is model-dated at Benton's 1918 metal
    // Century Schoolbook and carries "via URW Century Schoolbook L" only in
    // docs/font-dates.md's digital-place column, because URW's base-35 cut is
    // the digitization route, not a design stage by a third hand. Heros takes
    // the same route through URW's Nimbus Sans and so takes the same two
    // stages.
    //
    // Both stage-1 names are the FONT'S OWN claim, not an outside attribution:
    // CTAN's README-TeX-Gyre-Heros.txt says the face "can be used as a
    // replacement for a popular font Helvetica, also known as Swiss (prepared
    // by Max Miedinger with Eduard Hoffmann, 1957, at the Haas Type Foundry)".
    // Hoffmann joins Miedinger for the same reason Newsreader pairs Gentile
    // with Levée and Lexica pairs Scott with Dobie — the source credits both,
    // and the second name is the foundry director who set the brief.
    // Münchenstein, near Basel, is where the Haas'sche Schriftgiesserei sat.
    //
    // 2009, ONE year, and it is deliberately not the font's copyright string.
    // The shipped OTF name table reads "Copyright 2006, 2009", CTAN's README
    // license block reads "Copyright 2007--2009", and the family's own history
    // file (qhv-hist.txt) resolves the disagreement: v0.991/0.995 (Feb-Mar
    // 2007) are labeled prereleases, and v2.003 of 16.09.2009 is "the first
    // official release of the TeX Gyre Heros fonts" — it could not have come
    // earlier, because URW only released the base-35 originals under the LPPL
    // on 2009-06-22. v2.004 (30.10.2009) is the shipped version, same year, so
    // there is no second year to comma in. 2006 appears in no history entry
    // and is a collection-wide copyright year, so it stays out under the
    // table's "no unreliable dates" rule. Antpolt above takes its years from
    // its name table instead; that is not a contradiction — its name-table
    // years agree with its own release history and these do not.
    {"TeXGyreHeros", "TeX Gyre Heros",
     "Max Miedinger & Eduard Hoffmann; Bogus\xC5\x82"
     "aw Jackowski & Janusz M. Nowacki",
     "1957 M\xC3\xBCnchenstein, Switzerland; 2009 Gda\xC5\x84sk", 1957},
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
    // Rogers is the ORIGINAL author here, not a digitiser — the reverse of most
    // two-stage rows — so he takes stage 1. Stage 2 is empty: the 1990 revival was
    // Bitstream staff work with no individual named in the font or the licence
    // key, and a company does not go in the designer slot.
    {"Venetian301", "Venetian 301", "Bruce Rogers;", "1914 New York; 1990 Cambridge, Mass.", 1914},
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
    // PEOPLE, never foundries (owner ruling 2026-08-15). "Bold Monday" is a
    // company; it belongs in the manufacturer field of the font, which is
    // exactly where the shipped file puts it. IBMPlexMono-Regular.ttf name ID 9
    // reads "Mike Abbink, Paul van der Laan, Pieter van Rosmalen" and name ID 8
    // reads "Bold Monday", so the three names below ARE the credit the typeface
    // states about itself. Places: Bold Monday's own IBM page credits "Mike
    // Abbink, New York/Austin", and boldmonday.com/support/about/ puts van der
    // Laan near The Hague and van Rosmalen near Eindhoven.
    {"IBMPlexMono", "IBM Plex Mono", "Mike Abbink, Paul van der Laan & Pieter van Rosmalen",
     "2017 New York, Austin, The Hague & Eindhoven", 2017},
    // Dates from iA's own announcement, "A Typographic Christmas" (ia.net,
    // 14 Dec 2018): Mono is "the classic Nitti, designed by Bold Monday";
    // "last year we added iA Writer Duo ... based on IBM Plex"; "this year we
    // add a third font ... called iA Writer Quattro". So the three are NOT one
    // year -- Nitti long predates the others, which is why Mono carries its
    // two-stage lineage the way the reading families do.
    // All three iA faces carry the SAME four-name designer string in their own
    // name tables (ID 9): "Mike Abbink, Paul van der Laan, Pieter van Rosmalen,
    // Oliver Reichenstein", over copyright "2017 IBM Corp. and iA Inc." That is
    // the Plex team plus iA's, which is what a derivation credits, so each face
    // splits into the Plex stage and iA's adaptation in Zurich.
    {"iAWriterQuattro", "iA Writer Quattro",
     "Mike Abbink, Paul van der Laan & Pieter van Rosmalen; Oliver Reichenstein",
     "2017 New York, Austin, The Hague & Eindhoven; 2018 Zurich", 2017},
    {"iAWriterDuo", "iA Writer Duo", "Mike Abbink, Paul van der Laan & Pieter van Rosmalen; Oliver Reichenstein",
     "2017 New York, Austin, The Hague & Eindhoven; 2017 Zurich", 2017},
    // This row used to credit Nitti — "Pieter van Rosmalen, Bold Monday;
    // Oliver Reichenstein", 2009 The Hague — and that was wrong about WHICH
    // typeface this is. Nitti is what iA Writer LEFT, not what this face is
    // made of. Two pieces of evidence, both primary:
    //
    //   * iAWriterMonoS-Regular.ttf's own name table reads copyright "2017 IBM
    //     Corp. and Information Architects GmbH" and lists Mike Abbink among
    //     the designers. Neither is true of a Bold Monday face from 2009.
    //   * iA's own post: "iA Writer Mono, Duo and Quattro were built upon IBM
    //     Plex", and the 2017 Duospace post explains why — "IBM Plex, like
    //     Nitti, was love at first sight ... Since it's open source, we could
    //     alter it as we wished."
    //
    // The Dec-2018 announcement's "the classic Nitti" is iA describing the face
    // being replaced. Nitti's own credit now lives on the NittiTypewriter row
    // below, where it is finally attached to the typeface it describes.
    {"iAWriterMono", "iA Writer Mono", "Mike Abbink, Paul van der Laan & Pieter van Rosmalen; Oliver Reichenstein",
     "2017 New York, Austin, The Hague & Eindhoven; 2018 Zurich", 2017},
    // The one COMMERCIAL editor face. Released 2010 by Fabrizio Schiavi Design,
    // the studio Schiavi set up in Piacenza (en.wikipedia.org/wiki/Fabrizio_Schiavi,
    // en.wikipedia.org/wiki/PragmataPro). One stage: fsd.it still sells it as a
    // continuously revised single work (© 2009-2015 on the family page, version
    // 0.821 at time of writing), and docs/font-dates.md's adjacent-year rule
    // makes the 2009 copyright start release lag rather than a second stage.
    // No row in docs/font-dates.md yet — add one there if that table is
    // regenerated.
    {"PragmataPro", "PragmataPro", "Fabrizio Schiavi", "2010 Piacenza", 2010},
    // Nitti Typewriter, the face iA Writer used before it moved to IBM Plex
    // ("the classic Nitti", iA, Dec 2018). 2007 is the first year in the shipped
    // font's OWN copyright string (name ID 0, "Copyright © 2007–2016 Bold
    // Monday"), which is primary evidence and outranks inference. It predating
    // Bold Monday's 2008 founding is not a contradiction: van Rosmalen drew it
    // before the foundry he co-founded existed, and the foundry's copyright
    // notice covers the work it later published — the same shape as any face
    // whose designer incorporated after drawing it. Do not "correct" this to a
    // post-2008 year without a source that dates the DESIGN.
    //
    // No place. Bold Monday publishes offices near The Hague and Eindhoven, but
    // those are the foundry's today, not where a 2007 face was drawn, and
    // nothing dates the studio to the design. Bare years, per the rule
    // docs/font-dates.md applies to Host Grotesk.
    {"NittiTypewriter", "Nitti Typewriter", "Pieter van Rosmalen", "2007", 2007},
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

// Above this many lines of information, the colophon gives up the roomy
// stacked form and bullets instead. Blank separator lines do not count toward
// it (owner ruling 2026-08-14).
constexpr size_t kMaxStackedInfoLines = 4;

// The picker row subtitle: TWO lines per lineage stage, the person on one and
// their year and place on the next, with a BLANK LINE between stages (owner
// rulings 2026-08-14 — "Chappel and 1938 need a newline", then "put a newline
// between originator and digitizer"). A family not in the table gets an empty
// subtitle.
//
// The middle dot that used to join designer to year is GONE. It was doing two
// different jobs in one glyph — separating a name from a date, and reading as a
// list bullet — and at picker width the pair regularly ran past the column and
// ellipsized exactly the half the credit exists to show. A line break separates
// without spending width.
//
// FIVE lines is the worst case, and two different rows reach it. A two-stage
// family in the roomy form spends person / year-place / blank / person /
// year-place (Edgar, Coelacanth). Inknut Antiqua + Junicode takes the bulleted
// form instead -- four stages, six lines of info, so one bulleted line each --
// and its `groupBreakAfter` blank brings that back to five as well. Both
// pickers' `kColophonLines` is 5 and neither has slack; a sixth line would be
// silently dropped, so a new row that needs one has to raise both.
//
// Stage N of `designer` pairs with stage N of `lineage`, both split on the
// same ";".
//
// This is a PAIRING, not a wrap. The previous version returned one flat run
// and let the theme word-wrap it, so the break landed wherever the words ran
// out — mid-lineage, splitting a year from its place, which is the very thing
// pairing year with place in the string had been done to prevent.
//
// A stage with no person is a real answer, not missing data: Venetian 301's
// second stage was Bitstream staff work, Junicode's first is Oxford University
// Press, Almendra's model is an anonymous scribal tradition. Those lines carry
// the year and place alone rather than printing an institution as a person.
inline std::string subtitle(const std::string& directory) {
  const Entry* e = find(directory.c_str());
  if (e == nullptr) return "";

  const auto split = [](const char* s) {
    std::vector<std::string> parts;
    if (s == nullptr) return parts;
    const char* start = s;
    for (const char* p = s;; ++p) {
      if (*p == ';' || *p == '\0') {
        std::string piece(start, static_cast<size_t>(p - start));
        const size_t b = piece.find_first_not_of(' ');
        const size_t d = piece.find_last_not_of(' ');
        parts.push_back(b == std::string::npos ? "" : piece.substr(b, d - b + 1));
        if (*p == '\0') break;
        start = p + 1;
      }
    }
    return parts;
  };

  const std::vector<std::string> people = split(e->designer);
  const std::vector<std::string> stages = split(e->lineage);

  // Pair ONLY when the two columns agree stage for stage. A single name against
  // two stages cannot be placed safely: sometimes that name is the digitiser and
  // belongs to the second stage (Inknut Antiqua), sometimes the original author
  // and belongs to the first (Venetian 301), and nothing in the strings
  // distinguishes them. Guessing either way misattributes a historical model to
  // a living designer, so an unsplit family keeps the legacy single line —
  // accurate, just not split — until its Designer column gains the ";" that says
  // who did which stage.
  //
  // Two rows still take this path, both needing a credit the table does not yet
  // carry rather than a re-split of one it does: Antykwa Półtawskiego (its one
  // credit is the GUST digitising duo; the 1931 stage is Adam Półtawski, named
  // only in this file's comment and the doc's Basis cell, never in the Designer
  // column) and iA Writer Mono (its one credit is Nitti's, so the 2018 Zurich
  // stage is the one with no name). Adding either is an attribution change, not a
  // placement, and wants an owner ruling.
  if (people.size() != stages.size()) {
    std::string flat(e->designer);
    if (e->lineage != nullptr && e->lineage[0] != '\0') {
      flat += '\n';
      flat += e->lineage;
    }
    return flat;
  }

  const size_t stageCount = stages.empty() ? people.size() : stages.size();

  // How many lines of INFO the roomy form would need — a person and a
  // year-place for every stage that has both. Blank separators are not counted;
  // they are spacing, not information.
  size_t infoLines = 0;
  for (size_t i = 0; i < stageCount; ++i) {
    const bool who = i < people.size() && !people[i].empty();
    const bool when = i < stages.size() && !stages[i].empty();
    infoLines += static_cast<size_t>(who) + static_cast<size_t>(when);
  }

  // Past four lines of info the stacked form stops being readable and starts
  // being a column of fragments, so it collapses back to one BULLETED line per
  // stage — the middle dot doing the joining a line break did above (owner
  // ruling 2026-08-14: newlines when it is possible, bullets when it is not,
  // and four info lines is where "possible" ends). Only the deepest lineages
  // take this path: Lexica Ultralegible's three stages and Inknut Antiqua +
  // Junicode's four.
  if (infoLines > kMaxStackedInfoLines) {
    std::string out;
    for (size_t i = 0; i < stageCount; ++i) {
      const std::string& who = i < people.size() ? people[i] : std::string();
      const std::string& when = i < stages.size() ? stages[i] : std::string();
      if (who.empty() && when.empty()) continue;
      if (!out.empty()) {
        // The group break, if this row has one and we have just passed it: a
        // blank line where one typeface's attribution ends and the next's
        // begins. Only this bulleted form needs it -- the roomy form below
        // already puts a blank between EVERY stage, so a second one there
        // would be a double gap rather than a separation.
        //
        // Emitted as "\n\n" so the consumer sees an EMPTY SEGMENT between two
        // newlines. Both pickers walk this string splitting on '\n' and push
        // an empty segment through as a blank line, because wrappedText() has
        // no words to lay one out from (FontSelectionActivity.cpp and
        // EditorFontSelectionActivity.cpp, previewColophonLines).
        out += (e->groupBreakAfter != 0 && i == e->groupBreakAfter) ? "\n\n" : "\n";
      }
      if (who.empty()) {
        out += when;
      } else if (when.empty()) {
        out += who;
      } else {
        out += who;
        out += " \xC2\xB7 ";  // middle dot
        out += when;
      }
    }
    return out;
  }

  // Roomy form: the person, then their year and place, then a BLANK LINE before
  // the next stage. The blank is what separates an originator from the person
  // who digitised their work decades later — without it the four lines read as
  // one undifferentiated block of names and dates.
  std::string out;
  for (size_t i = 0; i < stageCount; ++i) {
    const std::string& who = i < people.size() ? people[i] : std::string();
    const std::string& when = i < stages.size() ? stages[i] : std::string();
    if (who.empty() && when.empty()) continue;
    if (!out.empty()) out += "\n\n";  // blank line between stages
    if (who.empty()) {
      out += when;
    } else if (when.empty()) {
      out += who;
    } else {
      out += who;
      out += '\n';
      out += when;
    }
  }
  return out;
}

}  // namespace FontDisplayNames
