#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
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
// `origin` is the EARLIEST ORIGIN — one "YEAR PLACE" naming the original type
// the family descends from, which the reader picker draws as the first line of
// its colophon. Usually it is `lineage`'s first stage repeated; three rows say
// otherwise, and that is why it is stored rather than parsed. See the field.
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
  // EARLIEST ORIGIN: one "YEAR PLACE" naming the original type this family
  // descends from, drawn at the TOP of the reader picker's colophon (owner
  // ruling 2026-09-06, "the year and place of its earliest origin — the
  // original type, not the digitisation").
  //
  // A FIELD RATHER THAN A DERIVATION, and Coelacanth is the proof. Its lineage
  // begins "1914 New York" — Bruce Rogers' Centaur — but the type Centaur
  // revives is Jenson's roman, Venice, c. 1470, so reading stage 1 of `lineage`
  // would print the revival's date as the origin. Doves Type is the same shape
  // for the same reason (docs/font-dates.md: "the type was cut after Jenson"),
  // and so is Venetian 301, which is the same Centaur model. Three of the 38
  // rows disagree with their own first stage, which is three too many for a
  // parse.
  //
  // EVERY row carries one, including the ~35 where it IS the first stage
  // repeated. Spelled out rather than defaulted (the way `groupBreakAfter`
  // below is) because this is the line the reader sees FIRST: a row that omits
  // it would be a row whose top line nobody wrote. The cost is ~35 short
  // literals in flash and no DRAM.
  //
  // WHAT ACTUALLY ENFORCES THAT, because it is not the warning. Having no
  // default initialiser does make -Wmissing-field-initializers fire, but the
  // firmware build never sees it: platformio.ini carries only
  // `build_src_flags = -Werror=switch` and no -Wall/-Wextra. The real gate is a
  // TYPE ERROR, and it is a hard one -- a row that skips `origin` puts its
  // `earliestYear` integer into a `const char*`, which no compiler accepts. The
  // field order is load-bearing for that: `origin` sits immediately before
  // `earliestYear` so an omission cannot degrade to a silent nullptr. Backing it
  // up, test/settings_display_order asserts every row's origin is non-null,
  // carries a year, and does not post-date its own first lineage stage.
  //
  // A comma'd stage contributes only its FIRST year: Source Serif 4's
  // "2014, 2021 Santa Clara, California" origins as "2014 Santa Clara,
  // California", because 2021 is a revision of the thing, not the thing.
  //
  // NOT the sort key. `earliestYear` still follows `lineage`'s first stage —
  // see the comment on it below.
  const char* origin;
  // First year of `lineage`'s FIRST STAGE, for the picker's reverse-chron sort.
  //
  // DELIBERATELY NOT `origin`'s year, decided 2026-09-06 when `origin` was
  // added. Moving it would silently reorder the picker AND the in-book font
  // cycle, which walks the same comparator (readingfonts::sortsBefore,
  // src/ReadingFontList.cpp): Coelacanth, Doves Type and Venetian 301 would
  // jump from 1914/1900/1914 to c. 1470 and land beside Inknut at the old end
  // of the list. That is a reordering nobody asked for, and it would make the
  // order a claim about art history rather than about the faces — nearly every
  // revival here traces to Jenson or to Caslon if you follow it far enough.
  // Sorting on origin is one integer per row if it is ever wanted.
  uint16_t earliestYear;
  // Stages after which a BLANK LINE separates one typeface's attribution from
  // the next's. 0 — the value every row that omits it gets — means no break,
  // which is every family whose stages are all one typeface's story.
  //
  // It exists for the compound families, today only Inknut + Junicode,
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
    // OWNER RULING 2026-08-24 — SUPERSEDED 2026-08-27 ("replace 1350 london
    // with something more accurate"). Kept in full because the reasoning on
    // both sides is the useful part, and because the research pass below is
    // what the replacement is built on. The row now carries one stage; see the
    // comment immediately above it.
    //
    // The 2026-08-24 ruling was: "use 1350 London for origin of Alemendra."
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
    // TWO STAGES, and stage 1 names a STYLE rather than a face. Owner
    // 2026-08-27: "add a style for author and a rough year and place for when
    // this style started or peaked or earliest established."
    //
    // So the author slot carries the HAND and the stage carries when and where
    // that hand was established — not a typeface Sanfelippo copied, because
    // there is none. That distinction is the whole reason this row can have a
    // first stage at all now: the earlier attempt failed because it tried to
    // name a MODEL, and an original design has no model.
    //
    // WHY BLACKLETTER, AND WHY c. 1450 MAINZ. Owner 2026-08-27: "use c. 1450
    // mainz or something more accurate for blackletter type used in print."
    //
    // Gutenberg's workshop in Mainz cut the earliest European printing types
    // around 1450 — the so-called DK (Donatus-Kalender) type, followed by the
    // 42-line Bible type of about 1454. Both are BLACKLETTER: textura, modelled
    // on the Mainz liturgical manuscript hand. So "c. 1450 Mainz" is the point
    // the gothic hand became TYPE rather than writing, which is the milestone
    // that matters for a typeface's lineage.
    //
    // The "c." is doing real work and should not be sharpened. The earliest
    // types are not firmly dated — the DK type is placed anywhere in 1449-1452
    // on typographic evidence — so 1450 is an honest round number and 1454
    // would be a false precision about a different, later type.
    //
    // Blackletter also fits the FACE better than the chancery hand this
    // replaces. Almendra is upright, dark and pen-formed; cancelleresca is a
    // cursive italic. Her description names both classes, and this is the half
    // the letterforms actually favour.
    //
    // What it is NOT: a claim that Sanfelippo worked from Gutenberg. She names
    // a class of hands, not a printer. The stage dates the STYLE, which is
    // exactly what the owner asked the first stage to carry.
    //
    // Anchors considered and not used, so they are not re-proposed:
    //   * textura as MANUSCRIPT hand, peak c. 1300 northern France — earlier,
    //     but it predates type and this table's stage 1 is a lineage of faces;
    //   * the chancery half, Arrighi's `La Operina`, Rome 1522 — a crisp date
    //     for the OTHER hand she names, but a cursive italic, which Almendra
    //     is not;
    //   * the humanist cursive that grew into it, c. 1420 Florence — vaguer,
    //     and again the wrong half.
    //
    // SORT: 1450 seats the face just before Inknut (1469), at the old end of
    // the picker — reached by a defensible date rather than by a placement
    // decision, which is what separates this from "1350 London".
    //
    // --- The history of this row, kept because it was contested twice --------
    //
    // ONE STAGE, 2026-08-27, replacing the 1350 London ruling at the owner's
    // word: "replace 1350 london with something more accurate." Superseded
    // hours later by the two-stage form above, which satisfies the same
    // accuracy requirement AND restores a first stage.
    //
    // There is nothing accurate to put in a first stage, and that is the
    // finding rather than a gap. Every other row's stage 1 is a dated
    // historical MODEL with a place -- Inknut's 1469 Venice is Jenson, and it
    // can be pointed at. Almendra is an ORIGINAL design and points at no
    // exemplar: its own description names "the chancery and gothic hands",
    // which are CLASSES of scribal writing worked by anonymous hands across
    // centuries and two regions. Cancelleresca is 16th-century Italian and
    // textura is 13th-15th-century northern European, so no single date and no
    // single place covers both, and any pair invented to fill the slot would be
    // the same uncited inference "1350 London" was.
    //
    // 1350 London was wrong twice over, which is what retired it. English
    // chancery hand is c. 1400+, fifty years after that date; the hand actually
    // written in 1350s London was Anglicana, with textura for formal books. So
    // the stage named a script that was not yet in use in the place it named.
    //
    // SORT CONSEQUENCE, stated because it is visible: the key moves 1350 ->
    // 2011, so Almendra leaves the old end of the picker beside Inknut and
    // sorts to the FRONT. Under the list's own rule -- reverse chronological by
    // original date -- that is where a 2011 original belongs, and it is the
    // placement the 2026-08-24 ruling had deliberately overridden. If the old
    // position is wanted back it is this integer and nothing else; do not
    // reintroduce a fictional stage to get it.
    // ORIGIN = this row's own first stage, because Almendra is an ORIGINAL
    // design and has no earlier model to promote — the finding the whole block
    // above records. Note for anyone reconciling the two files:
    // docs/font-dates.md's Almendra row still reads "1522 Rome; 2011 Buenos
    // Aires" and its Excluded-dates section still says the row "carries one
    // stage, 2011 Buenos Aires, and sorts on 2011". Both predate the later
    // 2026-08-27 ruling quoted above ("use c. 1450 mainz or something more
    // accurate for blackletter type used in print") that this row implements,
    // so the doc is BEHIND the header here, not in conflict with it. Flagged
    // 2026-09-06; the doc row wants the same edit.
    {"Almendra", "Almendra", "Blackletter; Ana Sanfelippo", "c. 1450 Mainz; 2011 Buenos Aires", "c. 1450 Mainz", 1450},
    // Three revisions in one stage: Carter & Cone recut Dwiggins' Caledonia in
    // Cambridge across 1988, 1994 and 2026, so those years share a place and
    // take commas; the 1938 Linotype original is a separate stage. Dwiggins drew
    // that original, so he is stage 1, and stage 2 is left empty on two grounds:
    // the face's own name already says Carter & Cone, and the stage spans three
    // hands anyway (1988 David Berlow at Adobe/Linotype, 1994 and 2026 Carter &
    // Cone), so no single individual belongs on the line.
    {"CaledoniaCC", "Caledonia CC", "W.A. Dwiggins;", "1938 Hingham, Mass.; 1988, 1994, 2026 Cambridge, Mass.",
     "1938 Hingham, Mass.", 1938},
    {"Edgar", "Edgar", "William Caslon & Alexander Phemister; Tobias Frere-Jones & Nina St\xC3\xB6ssinger",
     "1722 London; 2025 Brooklyn", "1722 London", 1722},
    // ORIGIN c. 1470 Venice, and it is the row that made `origin` a field.
    // Stage 1 is Rogers' Centaur, 1914 — but Centaur is a revival of Nicolas
    // Jenson's roman, so the ORIGINAL TYPE is Jenson's and the lineage's first
    // stage is not the origin. OWNER RULING 2026-09-06, not a citation:
    // docs/font-dates.md carries no Jenson attribution for Centaur anywhere
    // (its Basis cell stops at "Centaur 1914"), so do not quote this row as
    // evidence for anything else. The "c." is doing real work — the doc dates
    // Jenson's roman 1470 in the Rosarivo row and that row itself flags the
    // dating as an inference, which is exactly the precision "c." claims.
    // earliestYear stays 1914; see the field comment for why the sort did not
    // move with it.
    {"Coelacanth", "Coelacanth", "Bruce Rogers; Ben Whitmore", "1914 New York; 2014 Waiheke Island, New Zealand",
     "c. 1470 Venice", 1914},
    // Built 2026-09-06 and CUT the same day (owner: "drop dtlfleischmann, keep
    // romulus"). The row stays, the way Freight Sans' and Lexica Ultralegible's
    // do: the recipe in sd-fonts.yaml is still buildable, and a build with no
    // row here shows the raw directory name in the picker. The face is
    // commercial (Dutch Type Library) and reached no device card.
    // Stage 1 is the MODEL and its date is the year Fleischmann began cutting
    // exclusively for the Enschedes, per DTL's own page on the face
    // (dutchtypelibrary.nl/DTLFleischmann.html), which also places him in
    // Amsterdam from his own 1735 foundry to his death in 1768. The Enschede
    // foundry those punches went to was in HAARLEM; the two cities disagree
    // and DTL's page is the one followed, because it is the primary source for
    // this revival and it is Fleischmann's own city that the column names.
    // Stage 2's year is the one the SHIPPED FILE declares -- its name table
    // reads "Version 3.0E/ Generated on 19-02-2000/ 2000 by Dutch Type
    // Library" -- and DTL's own page agrees that a German specimen appeared in
    // 2000. Three other years are in circulation and none is taken: DTL says
    // the commission was 1992, Fonts In Use says the release was 1994, and the
    // German Museum of Books and Writing dates the family to "between 1993 and
    // 2006" alongside Prokyon and Antaris. It is the least certain field in
    // this row. Leipzig is firm: the same museum page has Kaiser at Leipzig
    // University through the 1990s and designing for DTL "since 1992".
    // See docs/font-dates.md.
    {"DTLFleischmann", "DTL Fleischmann", "Johann Michael Fleischmann; Erhard Kaiser", "1743 Amsterdam; 2000 Leipzig",
     "1743 Amsterdam", 1743},
    // Added 2026-09-06 alongside DTL Fleischmann and PROMOTED the same day
    // (owner: "add") -- installed_families:, so every surface. It is the ninth
    // installed family and the first commercial one: the outlines are licensed
    // and gitignored, so a clean clone cannot rebuild this family. The digitiser is Frank E.
    // Blokland, NOT Kaiser: DTL's own page heads the face "Jan van Krimpen |
    // Frank E. Blokland", and the DTL Van Krimpen project it belongs to began
    // when Blokland met Huib van Krimpen at ATypI Basel in autumn 1986. Stage
    // 2's year is again the shipped file's own: "Copyright Dutch Type Library,
    // 2003". Stage 1 is Wikipedia's dating of the metal original, "Romulus
    // (1931, Enschede, also 1936 Monotype)", cut in Haarlem where van Krimpen
    // worked for Koninklijke Joh. Enschede his whole career. The 1936 Monotype
    // issue is deliberately NOT a third stage: it is the same design licensed
    // out, not a redrawing. Note for anyone judging the specimen -- Romulus's
    // sloped form is an OBLIQUE, not a true italic, by van Krimpen's design.
    {"DTLRomulus", "DTL Romulus", "Jan van Krimpen; Frank E. Blokland", "1931 Haarlem; 2003 's-Hertogenbosch",
     "1931 Haarlem", 1931},
    // Added 2026-09-06 (owner: "make the best possible version of Dante").
    // Stage 1 is the type as cut: Giovanni Mardersteig drew it and Charles
    // Malin cut the punches for the Officina Bodoni in Verona; Tipoteca and
    // Wikipedia both give 1954, with the first book set in it (Boccaccio's
    // Trattatello in laude di Dante, hence the name) in 1955. Mardersteig is
    // the designer credited, Malin the punchcutter -- one stage, one hand on
    // the drawing, the same convention as Coelacanth's Rogers-without-Malin.
    // Stage 2 is the digital redrawing: Monotype's Ron Carpenter, "free from
    // any restrictions imposed by hot metal", issued 1993 in three weights.
    // The 1957 Monotype hot-metal adaptation is deliberately NOT a third
    // stage: same design licensed for the machine, not a redrawing. Salfords
    // is Monotype's works near Redhill, Surrey -- the company's address, not
    // a page naming Carpenter's desk; docs/font-dates.md says so.
    // ORIGIN IS GRIFFO, 1501 VENICE (owner ruling 2026-09-07, "research griffo
    // 1501 as origin date and place for Dante"), and it is a RULING rather than
    // a citation, exactly as Coelacanth's c. 1470 is.
    //
    // What the sources say: Wikipedia's Dante page has the face "influenced by
    // (but not directly indebted to) the types cut by Francesco Griffo", and
    // Mardersteig had already cut a face called Griffo before starting this
    // one -- so the debt is acknowledged and indirect, not a model copied.
    // Bringhurst: Dante "has more of Griffo's spirit than any other face now
    // commercially available".
    //
    // 1495 VENICE IS GRIFFO'S ROMAN, the De Aetna type cut for Aldus Manutius
    // and the face Bembo descends from. It was 1501 for a few hours on the same
    // day -- Griffo's italic, for the April 1501 octavo Virgil, the first book
    // printed entirely in italic -- and the owner moved it to the roman when
    // the two were put to him: Dante is a roman-led book face whose italic is
    // secondary, so its ancestor should be the roman Griffo cut, not the
    // italic. The picker order is unchanged either way; nothing else in the
    // table sits between 1495 and 1501. Do not quote this row as evidence for
    // a Griffo attribution anywhere else.
    {"DanteMT", "Dante", "Giovanni Mardersteig; Ron Carpenter", "1954 Verona; 1993 Salfords, Surrey", "1495 Venice",
     1954},
    // Added 2026-09-06 with Dante. Stage 1 is van Krimpen's Lutetia, cut for
    // Joh. Enschede in Haarlem and first shown at the 1925 Paris exposition
    // (hence the name, Lutetia being Roman Paris). Stage 2 is Ralph M. Unger's
    // "complete fresh design" of 2014 under his RMU Typedesign label, with
    // Georg Schiller credited as co-designer by Fonts.com; Unger works in
    // Schwabisch Gmund (Identifont). Two stages, one redrawing -- the same
    // shape as Romulus's row.
    {"LutetiaNova", "Lutetia Nova", "Jan van Krimpen; Ralph M. Unger",
     // Split literals: "\xA4b" would read as one escape (the greedy-\x
     // hazard docs/font-dates.md warns about), the same reason "Grie\xC3\x9F"
     // "hammer" above is two pieces.
     "1925 Haarlem; 2014 Schw\xC3\xA4"
     "bisch Gm\xC3\xBCnd",
     "1925 Haarlem", 1925},
    // Added 2026-09-06 with Dante. Stage 1 is Eric Gill's type for Robert
    // Gibbings's Golden Cockerel Press at Waltham St Lawrence, Berkshire --
    // 1929 per MyFonts and ITC's own launch volume. Stage 2 is ITC's digital
    // cut of 1996, credited by MyFonts to Richard Dawson and Dave Farey, whose
    // HouseStyle studio was in London. The Titling and the Initials &
    // Ornaments cuts exist in the kit and have no slot in the reader.
    {"GoldenCockerel", "Golden Cockerel", "Eric Gill; Richard Dawson & Dave Farey",
     "1929 Waltham St Lawrence; 1996 London", "1929 Waltham St Lawrence", 1929},
    // Added 2026-09-06. Stage 1 is the Doves Press type: Emery Walker and
    // T. J. Cobden-Sanderson had it cut after Jenson for their Hammersmith
    // press, first used 1900. Stage 2 is Robert Green's recovery -- he redrew
    // it from printed sheets over 2013-15 and then, famously, had the original
    // matrices dredged from the Thames at Hammersmith, where Cobden-Sanderson
    // had thrown them in 1916-17 so no one else could use the face. The
    // digital revision this build ships is 2021.
    //
    // THE ROMAN IS ALL THERE EVER WAS. The Doves Press set everything roman
    // and cut no italic, so this family's italic is BORROWED (Junicode,
    // instanced to match) -- the only family in the picker whose italic is not
    // its own, which is why the designer column names Green alone.
    // ORIGIN c. 1470 Venice, and this one IS sourced: docs/font-dates.md's
    // Doves Type row says in its own Creation-place cell that "the type was cut
    // after Jenson", and the Rosarivo row in the same table dates Jenson's roman
    // to 1470 Venice. Two statements from the one authority, not an inference
    // of ours. Same "c." as Coelacanth's, for the same reason.
    {"DovesType", "Doves Type", "Emery Walker & T. J. Cobden-Sanderson; Robert Green", "1900 Hammersmith; 2021 London",
     "c. 1470 Venice", 1900},
    // Added 2026-09-07. Stage 1 is William Martin's types, cut around 1790 for
    // William Bulmer's Shakspeare Press and used for the Boydell Shakespeare;
    // Martin was a Birmingham type-founder out of the Baskerville circle, and
    // the design sits between Baskerville and Bodoni. The face was named
    // "Bulmer" only retrospectively, after the printer rather than the
    // punchcutter -- Martin is credited here because he cut it.
    //
    // LONDON IS THE PRESS'S CITY, not a source placing Martin's bench.
    // Wikipedia puts the Shakspeare Press at 3 Russell Court, off Cleveland
    // Row, St James's, from the spring of 1790, and Martin was engaged for
    // that project; no source found says where he cut the punches. Flagged in
    // docs/font-dates.md rather than dressed up, on the Golden Cockerel and
    // Dante precedent.
    //
    // Stage 2 is DJR's own interpretation, not a revival of a digitisation:
    // Warbler Text went out to his Font of the Month Club in February 2022.
    // "Western Massachusetts" is a REGION because DJR publishes no city --
    // his own About page says only "the hills of Western Massachusetts" --
    // and the rules allow the coarser place where no city is pinnable.
    //
    // ORIGIN is its own first stage, so no ancestor was invented. This face
    // does NOT get Jenson: a Modern after Martin descends from Baskerville's
    // line, and following any revival back to Venice is exactly the claim the
    // earliestYear comment warns against.
    {"WarblerText", "Warbler Text", "William Martin; David Jonathan Ross",
     "1790 London; 2022 Western Massachusetts", "1790 London", 1790},
    {"GoudyBookletter1911", "Goudy Bookletter", "Frederic W. Goudy; Barry Schwartz", "1911 New York; 2009 St. Paul",
     "1911 New York", 1911},
    // Born digital and revised where it was drawn, so one stage, comma'd years.
    {"SourceSerif4", "Source Serif 4",
     "Frank Grie\xC3\x9F"
     "hammer",
     "2014, 2021 Santa Clara, California", "2014 Santa Clara, California", 2014},
    // The one family that repeats a name across both stages, and it is correct:
    // there is no historical model here. Moser drew the 2011 Bern one-off, then
    // spent eight years expanding it into Grilli Type's 2020 release ("GT Alpina
    // by Reto Moser" on the foundry's own specimen). The semicolon separates two
    // cities of one designer's work, not an original from a digitiser.
    {"GTAlpinaCond", "GT Alpina", "Reto Moser; Reto Moser", "2011 Bern; 2020 Lucerne, Switzerland", "2011 Bern", 2011},
    // Stage 1 empty: docs/font-dates.md declines to promote de Spira, calling the
    // 1469 model the table's inference rather than S\xC3\xB8rensen's claim. Same
    // placement the InknutJunicode row below already uses.
    {"InknutAntiqua62", "Inknut Antiqua", "; Claus Eggers S\xC3\xB8rensen", "1469 Venice; 2014 Amsterdam",
     "1469 Venice", 1469},
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
    // Displayed as "Inknut + Junicode", not "Inknut Antiqua + Junicode" (owner
    // 2026-08-28). The KEY stays `InknutJunicode` -- it is the directory name
    // on the card and the string SdCardFontRegistry matches, so moving it would
    // orphan every installed copy. Only the label changed.
    {"InknutJunicode", "Inknut + Junicode", "; Claus Eggers S\xC3\xB8rensen; ; Peter S. Baker",
     "1469 Venice; 2014 Amsterdam; 1703 Oxford; 1998, 2023 Charlottesville, Virginia", "1469 Venice", 1469, 2},
    {"LibreCaslonText", "Libre Caslon Text", "William Caslon; Pablo Impallari & Rodrigo Fuenzalida",
     "1722 London; 2012 Rosario, Argentina", "1722 London", 1722},
    {"Lora", "Lora", "Olga Karpushina", "2011, 2019 Moscow", "2011 Moscow", 2011},
    // Stage 1 empty: docs/font-dates.md adds no original author here and flags the
    // 1757 Baskerville model itself as uncited (owner ruling 2026-08-12, ship as
    // written). Levée joins Gentile on the 2020 stage per the same ruling —
    // Production Type credits both. The literal is split before the 'e' because a
    // hex escape swallows any hex digit that follows it.
    {"Newsreader", "Newsreader",
     "; Hugues Gentile & Jean-Baptiste Lev\xC3\xA9"
     "e",
     "1757 Birmingham; 2020 Paris", "1757 Birmingham", 1757},
    // Stage 1 empty: Jenson is NOT promoted -- docs/font-dates.md flags the 1470
    // Venice model as uncited (the face is named for Raúl Rosarivo, a Gutenberg
    // scholar, which points at Mainz). Ugerman is the 2011 Buenos Aires designer.
    {"Rosarivo", "Rosarivo", "; Pablo Ugerman", "1470 Venice; 2011 Buenos Aires", "1470 Venice", 1470},
    {"TeXGyreSchola", "TeX Gyre Schola",
     "Morris Fuller Benton; Bogus\xC5\x82"
     "aw Jackowski & Janusz M. Nowacki",
     "1918 Jersey City; 2007 Gda\xC5\x84sk", "1918 Jersey City", 1918},
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
     "1931 Warsaw; 2003, 2009 Gda\xC5\x84sk", "1931 Warsaw", 1931},
    // The directory keeps Arkandis' foundry suffix because it is frozen; the
    // display name drops it, which is what the face calls itself. Model-dated
    // at Warren Chappell's Lydian, the 1938 typeface the font's own name table
    // says it mimics — New York, where Chappell ran his own studio, not ATF's
    // Jersey City plant, since he drew it independently rather than as staff.
    // Digitisation place is bare "France": ADF publishes no city.
    {"LibrisADF", "Libris", "Warren Chappell; Hirwen Harendal", "1938 New York; 2011 France", "1938 New York", 1938},
    // Stage 1 empty: the model is Oxford University Press' Pica Roman, an
    // institution, and the punchcutter is unrecorded (Baker's own design history
    // names nobody; do not "fix" it with Peter de Walpergen, who cut the Fell
    // Pica that Baker explicitly rules out). Baker is the digitiser.
    {"Junicode", "Junicode SemiCond", "; Peter S. Baker", "1703 Oxford; 1998, 2023 Charlottesville, Virginia",
     "1703 Oxford", 1703},
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
     "1832 London; 2019 New York; 2024 El Paso, Texas", "1832 London", 1832},
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
     "2023", "2023", 2023},  // year alone: Element Type publishes no location
    {"Archivo", "Archivo",
     "H\xC3\xA9"
     "ctor Gatti",
     "2012, 2020 Buenos Aires", "2012 Buenos Aires", 2012},
    {"LibreFranklin", "Libre Franklin", "Morris Fuller Benton; Pablo Impallari, Rodrigo Fuenzalida & Nhung Nguyen",
     "1902 Jersey City; 2016 Rosario, Argentina", "1902 Jersey City", 1902},
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
     "1957 M\xC3\xBCnchenstein, Switzerland; 2009 Gda\xC5\x84sk", "1957 M\xC3\xBCnchenstein, Switzerland", 1957},
    // Cut back to a recipe on 2026-08-04 and deleted from every surface; the
    // label stays because a card provisioned before that ruling still carries
    // the family. Born digital, one stage. 2004 is the design year from the
    // font's own copyright and head.created; the June 2005 GarageFonts release
    // is a year of lag and collapses under the table's adjacent-year rule.
    {"FreightSans", "Freight Sans", "Joshua Darden", "2004 Brooklyn", "2004 Brooklyn", 2004},
    // Born digital, 2011: the name points at the Italian 1400s but neither the
    // FONTLOG nor the specimen claims a model for the SANS (it is drawn as a
    // companion to Quattrocento, the serif), and this table does not invent
    // lineage years. One stage, two cities — Impallari in Rosario and Marini in
    // Osimo worked it together, so they are joined rather than semicolon'd,
    // which would read as two dates. Brenda Gallo joined for the 2012 bold and
    // italics and is in docs/font-dates.md, trimmed here to fit the row.
    {"QuattrocentoSans", "Quattrocento Sans", "Pablo Impallari & Igino Marini",
     "2011 Rosario, Argentina & Osimo, Italy", "2011 Rosario, Argentina & Osimo, Italy", 2011},
    // Rogers is the ORIGINAL author here, not a digitiser — the reverse of most
    // two-stage rows — so he takes stage 1. Stage 2 is empty: the 1990 revival was
    // Bitstream staff work with no individual named in the font or the licence
    // key, and a company does not go in the designer slot.
    // ORIGIN c. 1470 Venice, on Coelacanth's ruling rather than a second one:
    // docs/font-dates.md's own Excluded-dates section says "the 1914 design is
    // the model year for both Coelacanth and Venetian 301", so this is the SAME
    // Centaur, and two rows carrying one model may not disagree about where it
    // came from. Applied 2026-09-06 with Coelacanth's; flagged there rather than
    // treated as independently sourced.
    {"Venetian301", "Venetian 301", "Bruce Rogers;", "1914 New York; 1990 Cambridge, Mass.", "c. 1470 Venice", 1914},
    // --- The editor (writing) group ---------------------------------------
    // Colophons for the Editor Font picker, which presents and sorts its list
    // identically to Reader Font (owner ruling 2026-08-09). The keys are the
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
    {"SpaceMono", "Space Mono", "Benjamin Critton, Colophon", "2016 London", "2016 London", 2016},
    // PEOPLE, never foundries (owner ruling 2026-08-15). "Bold Monday" is a
    // company; it belongs in the manufacturer field of the font, which is
    // exactly where the shipped file puts it. IBMPlexMono-Regular.ttf name ID 9
    // reads "Mike Abbink, Paul van der Laan, Pieter van Rosmalen" and name ID 8
    // reads "Bold Monday", so the three names below ARE the credit the typeface
    // states about itself. Places: Bold Monday's own IBM page credits "Mike
    // Abbink, New York/Austin", and boldmonday.com/support/about/ puts van der
    // Laan near The Hague and van Rosmalen near Eindhoven.
    {"IBMPlexMono", "IBM Plex Mono", "Mike Abbink, Paul van der Laan & Pieter van Rosmalen",
     "2017 New York, Austin, The Hague & Eindhoven", "2017 New York, Austin, The Hague & Eindhoven", 2017},
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
     "2017 New York, Austin, The Hague & Eindhoven; 2018 Zurich", "2017 New York, Austin, The Hague & Eindhoven", 2017},
    {"iAWriterDuo", "iA Writer Duo", "Mike Abbink, Paul van der Laan & Pieter van Rosmalen; Oliver Reichenstein",
     "2017 New York, Austin, The Hague & Eindhoven; 2017 Zurich", "2017 New York, Austin, The Hague & Eindhoven", 2017},
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
     "2017 New York, Austin, The Hague & Eindhoven; 2018 Zurich", "2017 New York, Austin, The Hague & Eindhoven", 2017},
    // The one COMMERCIAL editor face. Released 2010 by Fabrizio Schiavi Design,
    // the studio Schiavi set up in Piacenza (en.wikipedia.org/wiki/Fabrizio_Schiavi,
    // en.wikipedia.org/wiki/PragmataPro). One stage: fsd.it still sells it as a
    // continuously revised single work (© 2009-2015 on the family page, version
    // 0.821 at time of writing), and docs/font-dates.md's adjacent-year rule
    // makes the 2009 copyright start release lag rather than a second stage.
    // No row in docs/font-dates.md yet — add one there if that table is
    // regenerated.
    {"PragmataPro", "PragmataPro", "Fabrizio Schiavi", "2010 Piacenza", "2010 Piacenza", 2010},
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
    {"NittiTypewriter", "Nitti Typewriter", "Pieter van Rosmalen", "2007", "2007", 2007},
};

inline const Entry* find(const char* directory) {
  if (directory == nullptr || directory[0] == '\0') return nullptr;
  const auto it = std::find_if(std::begin(kEntries), std::end(kEntries),
                               [directory](const Entry& e) { return std::strcmp(e.directory, directory) == 0; });
  return it != std::end(kEntries) ? &*it : nullptr;
}

// THE THREE SORT KEYS, derived from the strings rather than stored beside them.
//
// Owner ruling 2026-09-06: *"sort by origin year, secondary sort by next year,
// tertiary by next year."* So a family orders on its ORIGIN first (the year in
// `origin`, which for a revival is the original type — Coelacanth's c. 1470,
// not Rogers' 1914), then on its own first lineage stage, then on its second.
//
// WHY THESE ARE PARSED AND NOT THREE MORE INTEGER FIELDS. `earliestYear` is
// already a numeric duplicate of the first lineage year and its comment says
// why that was tolerated; adding two more would be three duplications of the
// same fact per row across 38 rows, and the first time one of them disagreed
// with its string the list would sort by something no one could see on screen.
// The strings are the only copy. Scanning them costs a few dozen byte
// comparisons inside a sort of at most 38 items, on a screen the reader opens
// by hand — measured against nothing because it is not on any hot path.
//
// A year is the first run of four digits, so "c. 1470 Venice" gives 1470 and
// "1988, 1994, 2026 Cambridge, Mass." gives 1988. A stage is a ';'-separated
// segment of `lineage`; a missing stage answers 0 and therefore sorts last,
// which is what an absent second stage should do.
constexpr uint16_t yearIn(const char* s) {
  if (s == nullptr) return 0;
  for (const char* p = s; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') continue;
    uint16_t y = 0;
    int digits = 0;
    for (const char* q = p; *q >= '0' && *q <= '9'; ++q) {
      y = static_cast<uint16_t>(y * 10 + (*q - '0'));
      ++digits;
    }
    if (digits == 4) return y;
    while (*p >= '0' && *p <= '9') ++p;  // skip a 2- or 3-digit run whole
    if (*p == '\0') break;
  }
  return 0;
}

// The year of lineage stage `index` (0-based), 0 if there is no such stage.
constexpr uint16_t stageYear(const char* lineage, int index) {
  if (lineage == nullptr) return 0;
  const char* p = lineage;
  for (int seen = 0; seen < index; ++seen) {
    while (*p != '\0' && *p != ';') ++p;
    if (*p == '\0') return 0;
    ++p;
  }
  // yearIn stops at the first 4-digit run, which is inside this segment
  // because every segment begins with its year.
  return yearIn(p);
}

// Earliest (creation) year for a family, for ordering the picker reverse
// chronologically by lineage. 0 for a family not in the table — sorts last.
inline uint16_t earliestYear(const char* directory) {
  const Entry* e = find(directory);
  return e != nullptr ? e->earliestYear : 0;
}

// The family's earliest origin, "YEAR PLACE" — the original type, not the
// digitisation. Empty for a family not in the table, exactly as subtitle() is:
// an unlisted or user-installed font gets no credit rather than a wrong one.
//
// Returned by value to match displayName()/subtitle(); the underlying literal
// is in flash and nothing is built per frame beyond the string copy the two
// neighbours already cost.
// The origin year alone, for the primary sort key. 0 when the family is not
// in the table -- sorts last, as earliestYear() does.
inline uint16_t originYear(const char* directory) {
  const Entry* e = find(directory);
  return e != nullptr ? yearIn(e->origin) : 0;
}

// The year of lineage stage `index` for a family. 0 when absent.
inline uint16_t lineageStageYear(const char* directory, int index) {
  const Entry* e = find(directory);
  return e != nullptr ? stageYear(e->lineage, index) : 0;
}

inline std::string origin(const std::string& directory) {
  const Entry* e = find(directory.c_str());
  return (e != nullptr && e->origin != nullptr) ? std::string(e->origin) : std::string();
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
// year-place (Edgar, Coelacanth). Inknut + Junicode takes the bulleted
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
