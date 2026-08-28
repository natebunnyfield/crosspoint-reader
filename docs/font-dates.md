# Font dates — source data for `src/FontDisplayNames.h`

Editable source of truth for the Reader Font picker's designer/lineage subtitles
and the picker's ordering. Edit this table, then update `kEntries` in
`src/FontDisplayNames.h` to match.

## Rules

- **Lineage format**: `YEAR PLACE` stages, ascending. **Semicolons separate
  stages** — the original from its digitisation, or one hand's work from
  another's. **Commas separate the initial release from later revisions within
  one stage**, which is why the years sit in front of the place they share:

      Coelacanth      1914 New York; 2014 Waiheke Island, New Zealand
      Source Serif 4  2014, 2021 Santa Clara, California
      Caledonia CC    1938 Hingham, Mass.; 1988, 1994, 2026 Cambridge, Mass.

  Read the semicolon as "and then, elsewhere" and the comma as "and again,
  there". Until 2026-08-04 the year and the place were two separate columns
  joined in the picker with a middle dot, which put every year in one run and
  every place in another and left the reader to pair them off; this form pairs
  them at the source. The old `Creation place` / `Digital place` columns are
  kept below as working notes — they are NOT what the picker shows.
- **One stage, two cities**: joined with `&`, not a semicolon, which would read
  as a second date (Quattrocento Sans: `2011 Rosario, Argentina & Osimo,
  Italy`).
- **Adjacent-year collapse**: a design year and its store release one year apart
  (2011 design, 2012 Google Fonts) is release lag, not history — keep only the
  design year.
- **No unreliable dates**: a year that can't be pinned to a source stays out.
  Likewise a place: Host Grotesk is bare years because Element Type publishes
  no location.
- **Places**: city + country where the creation-era design was made, and where
  the digital version was made. Country only when no city is pinnable; state
  only where the country has them.
- **Picker order** = this table's order: reverse chronological by EARLIEST
  (creation) year — newest lineage first, undated families last, ties
  alphabetical by display name.

## Tiers

Every family carries an explicit tier. The column used to be blank for all but
two rows, which made "demote everything below S" ambiguous — blank was doing
double duty as both "unranked" and "the curated baseline". It no longer is.

- **S** — the reading faces: Coelacanth, Edgar, TeX Gyre Schola and Almendra
  (bookish calligraphic oldstyle, added 2026-08-24) for serif
  work; Libre Franklin (grotesque), Libris (humanist, added 2026-08-12) and
  TeX Gyre Heros (neo-grotesque, added 2026-08-23) for sans — three sans,
  permanently, because they fill different classification cells and a
  head-to-head only runs within one cell; owner rulings 2026-08-12 and
  2026-08-23, see docs/sd-card-fonts.md. Rosarivo and Quattrocento Sans were
  A-tier'd on 2026-08-07 and are no longer shipped; Rosarivo is additionally withheld from
  the reader's list (src/ReadingFontList.cpp) because cards provisioned before
  that ruling still carry it. New work, previews and screenshots use Edgar or
  Coelacanth unless a sans is called for. Together with Inknut Antiqua + Junicode
  (2026-08-13) and Almendra (2026-08-24, promoted out of the iOS trial and
  reversing its own 2026-08-11 C-tier cut) these are exactly the eight in
  `installed_families` as of
  2026-08-24 — that is a consequence of the rulings, not a rule: the two lists
  are still maintained separately, and A and C families are deliberately
  curated-but-not-shipped.
- **A** — previously S. Still curated, buildable and supported, but **not
  necessarily installed**. Whether a family ships is decided by
  `installed_families` in `lib/EpdFont/scripts/sd-fonts.yaml`, not by this
  column. The two meant the same thing while every S family was installed and
  stopped meaning the same on 2026-08-04, when Lexica Ultralegible came off the
  cards as a recipe only. A rather than B because it was the curated baseline
  until that day, and it still holds the one cell nothing else covers — the
  low-vision / hyperlegible sans. B has never been S.
- **B** — buildable, not the default choice for new work.
- **C** — cut. Was curated once, then beaten at its own job by a face that stays.
  The recipe survives so the bench that ruled against it can be re-run, and for
  no other reason: not installed, not for new work, not a fallback. Archivo and
  Host Grotesk are the first entries — the text-grotesque bench shipped all
  three of its finalists, and all three filled the same cell, so it cut back to
  Libre Franklin on 2026-08-04. **Freight Sans** joined them later the same day:
  it and Quattrocento Sans are both humanist sans filling one cell, and the tier
  took the OFL one that rebuilds anywhere, audits clean and kerns better. Its
  `.cpfont` files were deleted from every surface; the recipe and picker label
  stay, because a card provisioned before the ruling still carries the family.

Tier is editorial and lives only here. It does not reach the device: the picker
sorts reverse-chronologically by earliest year (see below), and
`FontDisplayNames.h` stores no tier field. Changing a tier changes nothing a
reader sees.

## Families

| Tier | Directory | Display name | Designer | Lineage (picker) | Creation place | Digital place | Basis |
|---|---|---|---|---|---|---|---|
| C | HostGrotesk | Host Grotesk | Doğukan Karapınar & İbrahim Kaçtıoğlu | 2023 | — (born digital) | Not disclosed by the foundry | Copyright 2023 in the shipped font's name table; designers and foundry from the same table and [METADATA.pb](https://raw.githubusercontent.com/google/fonts/main/ofl/hostgrotesk/METADATA.pb); [Element Type](https://elementtype.co/host-grotesk/). Google Fonts listing 2024-10-03 collapsed as release lag. Element Type publishes no location, so the place column stays empty rather than guessing from the designers' names |
| B | SourceSerif4 | Source Serif 4 | Frank Grießhammer | 2014, 2021 Santa Clara, California | — (born digital) | Santa Clara / San Jose, California, USA (Adobe Type) | Pro 1.0 May 2014 ([Typekit blog](https://blog.typekit.com/2014/05/20/source-serif-pro/)); v4 2021-01-25 ([Wikipedia](https://en.wikipedia.org/wiki/Source_Serif)); Grießhammer in Santa Clara, Adobe Type at San Jose HQ ([Typekit interview](https://blog.typekit.com/2013/12/09/interview-frank-griesshammer/)) |
| C | Archivo | Archivo | Héctor Gatti | 2012, 2020 Buenos Aires | — (born digital) | Buenos Aires, Argentina (Omnibus-Type) | Design 2012 ([Wikipedia](https://en.wikipedia.org/wiki/Archivo), [Omnibus-Type](https://www.omnibus-type.com/fonts/archivo/)); 2020 = the variable rework, per the shipped font's copyright string and [repo](https://github.com/Omnibus-Type/Archivo); designer from the font's name table (ID 9). Google Fonts listing 2016-12-03 is neither a design nor a release date and is excluded |
| B | GTAlpinaCond | GT Alpina | Reto Moser; Reto Moser | 2011 Bern; 2020 Lucerne, Switzerland | Bern, Switzerland (Moser lives/works in Bern) | Lucerne, Switzerland (Grilli Type foundry) | Design start 2011 ([Fonts In Use](https://fontsinuse.com/typefaces/102321/gt-alpina)); release 2020 ([It's Nice That](https://www.itsnicethat.com/articles/grilli-type-gt-alpina-graphic-design-030220)); places ([Grilli Type](https://www.grillitype.com/about)). **No original-author gap to fill, checked 2026-08-12**: both stages are Reto Moser. The 2011 stage is not a historical model but a one-off custom face Moser built for a book marking the 75th anniversary of the Swiss Foundation for Alpine Research, which he then spent eight years expanding into the 2020 Grilli Type release — "GT Alpina by Reto Moser / © 2020 Grilli Type" on the foundry's own specimen ([specimen PDF](https://www.grillitype.com/api/storage/app/uploads/public/5e1/e4a/bac/5e1e4abac7b46609129786.pdf)), same designer both times per the It's Nice That piece. The semicolon here separates two cities of one designer's work, not an original from a digitiser, so the device's second subtitle line has no separate name to carry. Do not invent a historical model for this row. **Per-stage split 2026-08-13: Moser on BOTH stages** — the only row in the table that repeats a name, and it is repeated rather than left empty precisely because "both stages are Reto Moser" above; an empty second slot would render "2020 Lucerne, Switzerland" bare and imply the Grilli Type release had no designer, which the foundry's own "GT Alpina by Reto Moser" specimen contradicts |
| B | Lora | Lora | Olga Karpushina | 2011, 2019 Moscow | — (born digital) | Moscow, Russia (Karpushina; Cyreal foundry, Russia) | First release 2011 ([GitHub](https://github.com/cyrealtype/Lora-Cyrillic)); variable 2019 ([Cyreal](https://www.cyreal.org/fonts/lora/)); [Behance](https://www.behance.net/OlgaKarpushina) |
| EDITOR | NittiTypewriter | Nitti Typewriter | Pieter van Rosmalen | 2009 | Not pinnable (near Eindhoven per van Rosmalen's own statement, not independently verifiable; omitted per "no unreliable places" rule, same as Host Grotesk) | same as Creation place | Copyright 2007–2016 in the font's name table; Bold Monday founded 2008 ([boldmonday.com/support/about/](https://boldmonday.com/support/about/)). The 2007 date predates the foundry, which is suspicious — likely an internal design-start year before the studio existed. 2009 used as conservative minimum: iA's Dec 2018 blog post ("A Typographic Christmas", ia.net) calls iA Writer Mono "the classic Nitti, designed by Bold Monday", and iAWriterMono's FontDisplayNames.h colophon carries "2009 The Hague" on the strength of that post. Using 2009 is consistent with that implied claim. Version 5.0, 1220 glyphs, Regular only — Bold/Italic/BoldItalic are synthesised at build time in this firmware. COMMERCIAL: the recipe is committed, the TTF is not. |
| A | CaledoniaCC | Caledonia CC | W.A. Dwiggins; | 1938 Hingham, Mass.; 1988, 1994, 2026 Cambridge, Mass. | Hingham, Massachusetts, USA (Dwiggins' studio; for Mergenthaler Linotype, Brooklyn NY) | Cambridge, Massachusetts, USA (Carter & Cone: 1994 Time Caledonia, 2026 CC). 1988 New Caledonia: David Berlow, Adobe (USA) / Linotype AG (Germany) | 1938 design ([Wikipedia](https://en.wikipedia.org/wiki/Caledonia_(typeface))); 1988 = New Caledonia PostScript ([Wikipedia](https://en.wikipedia.org/wiki/Caledonia_(typeface))); 1994 = Time Caledonia by Matthew Carter; CC release April 2026 ([Carter & Cone](https://carterandcone.com/font/caledonia/), [Creative Boom](https://www.creativeboom.com/resources/the-best-new-typefaces-for-april-2026/)); Dwiggins in Hingham 1906–56 ([Britannica](https://www.britannica.com/biography/W-A-Dwiggins)). **Per-stage split 2026-08-13: Dwiggins on stage 1, second slot empty.** He drew the 1938 original, which is stage 1; the second slot is empty on two grounds — the face's own name already carries Carter & Cone (the standing rule in `FontDisplayNames.h`, "revivals credit whichever name the face itself doesn't already carry"), and that stage spans three separate hands anyway (1988 New Caledonia: David Berlow, Adobe/Linotype; 1994 Time Caledonia: Matthew Carter; 2026 CC: Carter & Cone, per the Digital place cell), so no single individual belongs on the line |
| S | LibrisADF | Libris | Warren Chappell; Hirwen Harendal | 1938 New York; 2011 France | New York City, USA (model: Lydian, Warren Chappell, cut and cast by American Type Founders) | France (Arkandis Digital Foundry; no city published) | Two-stage credit: Warren Chappell drew the model, Hirwen Harendal the digitisation ([Chappell](https://en.wikipedia.org/wiki/Warren_Chappell) is cited again under Creation place below). Model named by the shipped font itself: name table ID 10 reads "Libris is a sans serif font intented to mimic Lydian typeface" [sic], and upstream's NOTICE.txt calls it "The Libris collection". Lydian 1938 ([Wikipedia](https://en.wikipedia.org/wiki/Lydian_(typeface)), [Typewolf](https://www.typewolf.com/lydian)); [Fonts In Use](https://fontsinuse.com/typefaces/6/lydian) dates the release "1938-40", collapsed to the design year by the adjacent-year rule. Creation place is New York rather than ATF's Jersey City plant because Chappell designed independently — Koch's pupil at Offenbach 1931–32, then his own studio in New York City until just before the war ([Wikipedia](https://en.wikipedia.org/wiki/Warren_Chappell)) — unlike the Benton-for-ATF models behind Franklin Gothic and Century Schoolbook, which do take Jersey City. Digitisation 2011: upstream's release directory is `Libris-Std-20110117`, 2011-01-17 ([salsa.debian.org](https://salsa.debian.org/fonts-team/fonts-adf/-/tree/master/Libris-Std-20110117)), and the shipped v1.007 OTF's `head.created` is 2011-10-24, the same year. Designer and foundry from the name table (ID 8 "Arkandis Digital Foundry", ID 9 "HARENDAL Hirwen"), corroborated by [Luc Devroye](https://luc.devroye.org/fonts-47797.html); ADF is French but neither [its own site](https://arkandis.tuxfamily.org/) nor Debian's `debian/copyright` gives a city, so the digital place is bare "France", the same treatment Host Grotesk gets. Licence GPL v2+ with font exception (upstream NOTICE.txt; Debian `debian/copyright` "GPL-2+ with Font exception", © 1998–2019 Hirwen Harendal, Arkandis Digital Foundry), so like TeX Gyre Schola it needs no gitignored local files and rebuilds from URLs anywhere. All four styles are real cuts, v1.007 |
| S | TeXGyreSchola | TeX Gyre Schola | Morris Fuller Benton; Bogusław Jackowski & Janusz M. Nowacki | 1918 Jersey City; 2007 Gdańsk | Jersey City, New Jersey, USA (model: Century Schoolbook, Morris Fuller Benton for American Type Founders, roman 1918) | Gdańsk / Grudziądz, Poland (GUST e-foundry, seated in Toruń; via URW Century Schoolbook L) | First stable v0.996 2007-01-14, final v2.005 2009 ([CTAN README](http://ftp.math.utah.edu/pub/tex/historic/fonts/tex-gyre/schola/0.996/README-TeX-Gyre-Schola.txt), [GUST](https://www.gust.org.pl/projects/e-foundry/tex-gyre/schola)); Benton/ATF ([Wikipedia](https://en.wikipedia.org/wiki/Century_type_family), [ATF](https://en.wikipedia.org/wiki/American_Type_Founders)); original author promoted into the Designer column — Century Schoolbook is Benton's, drawn for ATF ([Morris Fuller Benton](https://en.wikipedia.org/wiki/Morris_Fuller_Benton)); digitisers ([Typoteka: Jackowski](https://typoteka.pl/en/designer/boguslaw-jackowski), [Nowacki](https://typoteka.pl/en/designer/janusz-marian-nowacki)) |
| S | TeXGyreHeros | TeX Gyre Heros | Max Miedinger & Eduard Hoffmann; Bogusław Jackowski & Janusz M. Nowacki | 1957 Münchenstein, Switzerland; 2009 Gdańsk | Münchenstein, Switzerland (model: Helvetica, Max Miedinger with Eduard Hoffmann, Haas'sche Schriftgiesserei, 1957) | Gdańsk, Poland (GUST e-foundry; via URW Nimbus Sans L, the base-35 cut) | Model named by the font's own documentation, the same footing Libris' name table gives Lydian: [README-TeX-Gyre-Heros.txt](https://mirrors.ctan.org/fonts/tex-gyre/doc/README-TeX-Gyre-Heros.txt) says the face "can be used as a replacement for a popular font Helvetica, also known as Swiss (prepared by Max Miedinger with Eduard Hoffmann, 1957, at the Haas Type Foundry)" — so both stage-1 names and the 1957 date are the digitizer's own claim, not an outside attribution. Hoffmann is credited beside Miedinger on the Newsreader/Lexica precedent (the source names both, and the second is the director who set the brief). Haas'sche Schriftgiesserei sat in Münchenstein near Basel ([Wikipedia](https://en.wikipedia.org/wiki/Haas_Type_Foundry)). URW route from the collection's own [README.txt](https://mirrors.ctan.org/fonts/tex-gyre/README.txt), "The original fonts are the base35 release from URW" — recorded here rather than as a lineage stage, exactly as TeX Gyre Schola records URW Century Schoolbook L. **Digitization year 2009, and it is deliberately NOT the name-table copyright**: the shipped OTF reads "Copyright 2006, 2009", CTAN's README license block reads "Copyright 2007--2009", and the family history file [qhv-hist.txt](https://mirrors.ctan.org/fonts/tex-gyre/doc/qhv-hist.txt) settles it — v0.991/0.995 (Feb–Mar 2007) are labeled prereleases and v2.003 of 16.09.2009 is "the first official release of the TeX Gyre Heros fonts", which could not have been earlier because URW only released the base-35 originals under the LPPL on 2009-06-22. The shipped v2.004 is 30.10.2009, the same year, so there is no second year to comma in; 2006 appears in no history entry and stays out under the no-unreliable-dates rule (Antpolt takes its years from its name table because there they agree with its release history — here they do not). License: GUST Font License, so like TeX Gyre Schola and Libris it rebuilds from CTAN on any machine with no gitignored local files. All four styles are real cuts, v2.004. Coverage measured on the shipped OTF: Latin-1 223/224, Latin Extended-A 124/128, Greek 54/144, Cyrillic 0/256 — the same 1087 codepoints TeX Gyre Schola has, because qhv-hist.txt records that GUST could not relicense Valek Filippov's Cyrillic and "thus there are no Cyrillic glyphs in any of the TeX Gyre fonts" |
| C | Antpolt | Antykwa Półtawskiego | Adam Półtawski; Bogusław Jackowski & Janusz M. Nowacki | 1931 Warsaw; 2003, 2009 Gdańsk | Warsaw, Poland (Adam Półtawski, born Warsaw 1881; first cast at Jan Idzkowski's foundry, Warsaw, 1931 — used through the 1970s as the text face for, among other work, Paderewski's 27-volume complete Chopin edition) | Gdańsk, Poland (GUST e-foundry — same Jackowski/Nowacki team and city as TeX Gyre Schola above) | Design and first casting: CTAN [poltawski README](https://mirrors.ctan.org/fonts/poltawski/README) ("designed in the twenties... by... Adam Półtawski... first cast in Jan Idzkowski's foundry (Warsaw, Poland) in 1931"; Półtawski's birth/death and dates from the same README). Digitization years 2003 and 2009 are the shipped font's own name-table copyright string ("Copyright 2003, 2009 B. Jackowski and J. M. Nowacki... on behalf of the TeX Users Groups"); CTAN's v1.101 (2010-10-09) is a point-release one year later and collapses under the adjacent-year rule. Designers/place per the same Typoteka bios cited for TeX Gyre Schola. GUST Font License (free/OFL-equivalent), so the recipe rebuilds from CTAN on any machine — no `local_fonts/` needed |
| S | Coelacanth | Coelacanth | Bruce Rogers; Ben Whitmore | 1914 New York; 2014 Waiheke Island, New Zealand | New York, USA (Centaur: Bruce Rogers for the Metropolitan Museum of Art; matrices cut in Chicago) | Waiheke Island (Auckland), New Zealand | Centaur 1914 ([Wikipedia](https://en.wikipedia.org/wiki/Centaur_(typeface))); release 2014-08-29 ([Font Library](https://fontlibrary.org/en/font/coelacanth)); "Ben Whitmore from Waiheke, New Zealand" ([ben-whitmore.com](http://ben-whitmore.com/coelacanth-type-family/)). Original author promoted into the Designer column on the designer's own framing — that page is titled "Coelacanth: a type family inspired by Bruce Rogers' Centaur" — and Rogers drew Centaur for the Metropolitan Museum of Art ([Bruce Rogers](https://en.wikipedia.org/wiki/Bruce_Rogers_(typographer))) |
| B | Venetian301 | Venetian 301 | Bruce Rogers; | 1914 New York; 1990 Cambridge, Mass. | New York, USA (Centaur: Rogers for the Metropolitan Museum of Art) | Cambridge, Massachusetts, USA (Bitstream's city at the time; later Marlborough MA) | Centaur 1914 ([Wikipedia](https://en.wikipedia.org/wiki/Centaur_(typeface))); Bitstream © 1990 ([Bitstream key](http://www.sanskritweb.net/forgers/bitstream2.pdf)); Cambridge-era Bitstream ([Wikipedia](https://en.wikipedia.org/wiki/Bitstream_Inc.)). Checked 2026-08-12 and left as-is: Rogers is the ORIGINAL author here, not a digitiser ([Bruce Rogers](https://en.wikipedia.org/wiki/Bruce_Rogers_(typographer))), and the 1990 stage was Bitstream's staff work with no individual named in the font or the licence key — so this row has one person and one company, and a two-line device subtitle should render the second line's designer slot empty rather than printing "Bitstream" as a person. **Per-stage split 2026-08-13: Rogers on stage 1, second slot empty**, exactly as that sentence specifies — this is the row where the person is the ORIGINAL author rather than the digitiser, the reverse of Rosarivo/Inknut/Junicode |
| B | GoudyBookletter1911 | Goudy Bookletter | Frederic W. Goudy; Barry Schwartz | 1911 New York; 2009 St. Paul | New York City, USA (Goudy's Village Press era; Kennerley Old Style for publisher Mitchell Kennerley) | St. Paul, Minnesota, USA (Schwartz) | Kennerley 1911 ([Wikipedia](https://en.wikipedia.org/wiki/Kennerley_Old_Style)); League of Moveable Type ~2009 ([League](https://www.theleagueofmoveabletype.com/goudy-bookletter-1911)), whose page states the basis outright — "Based on Frederic Goudy's Kennerley Oldstyle" — so the full name is promoted into the Designer column ([Frederic W. Goudy](https://en.wikipedia.org/wiki/Frederic_Goudy)) in place of the Creation place column's shorthand "Goudy's Village Press era"; Schwartz in St. Paul ([Luc Devroye](https://luc.devroye.org/fonts-46940.html), single source) |
| S | LibreFranklin | Libre Franklin | Morris Fuller Benton; Pablo Impallari, Rodrigo Fuenzalida & Nhung Nguyen | 1902 Jersey City; 2016 Rosario, Argentina | Jersey City, New Jersey, USA (model: Franklin Gothic, Morris Fuller Benton for American Type Founders, 1902 — note ATF's Jersey City plant opened 1903, so the design marginally predates the move) | Rosario, Argentina (Impallari Type) | Franklin Gothic 1902 ([Wikipedia](https://en.wikipedia.org/wiki/Franklin_Gothic)); Libre Franklin added to Google Fonts 2016-06-20 ([METADATA.pb](https://raw.githubusercontent.com/google/fonts/main/ofl/librefranklin/METADATA.pb)) and preinstalled on Axis-Praxis 2016-11 ([Axis-Praxis](https://www.axis-praxis.org/blog/2016-11-11/3/new-font-libre-franklin-by-pablo-impallari-now-preinstalled)); three digitisers from the font's name table (ID 9). Original author promoted into the Designer column: the family's own Google Fonts blurb calls itself "an interpretation and expansion of the 1912 Morris Fuller Benton classic" ([DESCRIPTION.en_us.html](https://raw.githubusercontent.com/google/fonts/main/ofl/librefranklin/DESCRIPTION.en_us.html), [Benton](https://en.wikipedia.org/wiki/Morris_Fuller_Benton)) — note that blurb says **1912** where Wikipedia dates the design 1902; the 1902 in the Lineage column is kept, as the cited design year, and the 1912 is not a second stage |
| S | QuattrocentoSans | Quattrocento Sans | Pablo Impallari, Igino Marini & Brenda Gallo | 2011 Rosario, Argentina & Osimo, Italy | — (born digital) | Rosario, Argentina (Impallari Type); Osimo, Italy (Igino Marini, iKern) | v1.002 initial release 5 Apr 2011, Impallari & Marini; v2 "family released" 8 Feb 2012 adds the bold (31 Jan 2012), the italics (3 Feb 2012) and the iKerning, with Brenda Gallo credited from v1.003 — all from the [FONTLOG](https://raw.githubusercontent.com/google/fonts/main/ofl/quattrocentosans/FONTLOG.txt). Copyright 2011 names all three ([METADATA.pb](https://raw.githubusercontent.com/google/fonts/main/ofl/quattrocentosans/METADATA.pb)). Google Fonts `date_added` 2012-02-15 is release lag off that Feb 2012 build; 2012 itself is 10 months after the roman and collapses under the adjacent-year rule, which names this exact 2011/2012 case. **No model year**: the name means the Italian 1400s and the face is the companion to Quattrocento (the serif), but neither the FONTLOG nor the [specimen](https://raw.githubusercontent.com/google/fonts/main/ofl/quattrocentosans/DESCRIPTION.en_us.html) names a historical model for the sans, so it stays born-digital rather than getting an invented lineage — reversible by owner ruling, as Lexica Ultralegible's 1832 was. Marini in Osimo ([Luc Devroye](https://luc.devroye.org/fonts-43047.html)); Impallari Type in Rosario, as the other Impallari rows. Gallo's city is not pinnable from any source and is left out |
| C | FreightSans | Freight Sans | Joshua Darden | 2004 Brooklyn | — (born digital) | Brooklyn, New York, USA (Darden Studio); published by GarageFonts / Phil's Fonts, Rockville, Maryland | Design year 2004 from the shipped font's own copyright string ("Copyright © 2004, Joshua Darden & Phil's Fonts, Inc.") and `head.created` (2004-12-08 on the italics); the Freight superfamily's public release was 2005-06-01 through GarageFonts ([Fonts In Use](https://fontsinuse.com/typefaces/2773/freight-sans), [CreativePro](https://creativepro.com/new-type-design-freight-released/)) — one year of lag, collapsed by the adjacent-year rule. Darden Studio founded in Brooklyn 2004–2005, Freight published soon after ([Wikipedia](https://en.wikipedia.org/wiki/Joshua_Darden)); designer from the font's name table (ID 9), publisher from ID 11 (garagefonts.com) and the ID 0 phone number's Maryland area code |
| A | LexicaUltralegible | Lexica Ultralegible | ; Elliott Scott & Craig Dobie; Jacob Perez | 1832 London; 2019 New York; 2024 El Paso, Texas | London, England (model: the style Figgins printed as "sans serif" in his 1832 specimen — not "the grotesque"; see Basis) | New York, USA (Applied Design Works, NY/LA; for Braille Institute of America, Los Angeles); El Paso, Texas, USA (Jacob Perez) | Lineage starts 1832 London, owner ruling 2026-08-03: Figgins' 1832 specimen is where "sans serif" is first printed as the name of the style this face belongs to, and the tier's other entries are dated from their model rather than their release ([Wikipedia: sans-serif](https://en.wikipedia.org/wiki/Sans-serif), [Figgins](https://en.wikipedia.org/wiki/Vincent_Figgins)). Atkinson Hyperlegible released 2019, Applied Design Works with Elliott Scott as lead designer and Craig Dobie as creative director ([Wikipedia](https://en.wikipedia.org/wiki/Atkinson_Hyperlegible), [Fast Company](https://www.fastcompany.com/90395836/this-typeface-hides-a-secret-in-plain-sight-and-thats-the-point)); studio in New York and Los Angeles, Braille Institute in Los Angeles ([Applied Design Works](https://helloapplied.com/braille-institute-of-america/), [It's Nice That](https://www.itsnicethat.com/news/applied-design-braille-institute-atkinson-hyperlegible-graphic-design-050821)); Lexica v1.0.0 released 2024-10-06 ([GitHub release](https://github.com/jacobxperez/lexica-ultralegible/releases)); Perez in El Paso ([GitHub profile](https://github.com/jacobxperez)). **No original author added, researched 2026-08-12 — and Figgins would be the wrong name anyway.** Figgins was a typefounder who cast and sold type, not a punchcutter: he "often commissioned work from two punchcutters about whom little is known named Perry and Edmonston", and no source names who cut the sans-serif punches ([Wikipedia: Vincent Figgins](https://en.wikipedia.org/wiki/Vincent_Figgins)). His claim on the 1832 stage is that he published it and coined the word — the article credits him with introducing "the term 'sans-serif', not previously attested" — which is naming, not authorship. So the 1832 stage has **no recorded individual designer**; the device's original-author line stays empty rather than print Figgins as the designer of a face he did not cut. **Fixed 2026-08-12 (owner ruling: keep the 1832 London stage, correct the wording)**: the Creation place parenthetical said "the grotesque" — that word is William Thorowgood's, first recorded c. 1834, a different founder entirely — where it meant Figgins' own claimed word, "sans-serif". Corrected above; the 2026-08-03 ruling to anchor the lineage on this stage stands unchanged. **Per-stage split 2026-08-13: stage 1 empty, Applied Design Works on stage 2 (2019 New York), Jacob Perez on stage 3 (2024 El Paso).** Stage 1 is empty on this row's own finding — "the 1832 stage has no recorded individual designer; the device's original-author line stays empty rather than print Figgins". The two modern credits were already an `&` pair for the two modern stages and only needed distributing. **⚠ Flagged, not changed: "Applied Design Works" is a studio**, and the "Designer column" section below says only individuals go here, naming Applied Design Works as one of the four that should be absent — yet this row has carried it since it was written. This row's own Basis names **Elliott Scott (lead designer)** and **Craig Dobie (creative director)** for the 2019 Atkinson Hyperlegible, so a fix has candidates; substituting either is a change of attribution rather than a placement, so it needs an owner ruling and the studio name stays as written |
| B | Newsreader | Newsreader | ; Hugues Gentile & Jean-Baptiste Levée | 1757 Birmingham; 2020 Paris | Birmingham, England (model: John Baskerville's types, 1757 — attribution uncited, owner ruling: ship flagged, see Basis) | Paris, France (Production Type) | Google Fonts 2020-07-01 ([METADATA.pb](https://raw.githubusercontent.com/google/fonts/main/ofl/newsreader/METADATA.pb)); [Production Type](https://productiontype.com/font/newsreader). **⚠ No original author added, and the Baskerville model itself is uncited — owner ruling 2026-08-12: ship as written, flagged, revisit later.** Searched 2026-08-12: neither Production Type's own family page nor the Google Fonts blurb names Baskerville or any historical model — the blurb is one sentence, "NewsReader is an original typeface designed by Production Type, primarily intended for continuous on-screen reading in content-rich environments" ([DESCRIPTION.en_us.html](https://raw.githubusercontent.com/google/fonts/main/ofl/newsreader/DESCRIPTION.en_us.html)), and the foundry page calls it "designed for on-screen, longer-form reading" with no lineage at all. Third-party writeups place it in the transitional-serif category generally ([Pimp my Type](https://pimpmytype.com/font/newsreader/)) but none name Baskerville. So the 1757 Birmingham stage rests on a category resemblance, not a sourced claim; kept as-is per owner ruling rather than removed under the "No unreliable dates" rule. **Jean-Baptiste Levée added to the Designer column 2026-08-12 (owner ruling)**: Production Type credits him alongside Gentile on the family page. **Per-stage split 2026-08-13: stage 1 empty, Gentile & Levée on stage 2 (2020 Paris).** Stage 1 is empty on this row's own finding — "No original author added" — and Baskerville is deliberately NOT promoted into the slot, since the 1757 model attribution is itself flagged as uncited. Both credited names are Production Type's, so both sit on the 2020 stage, joined with `&` per the multi-person single-stage convention |
| S | Edgar | Edgar | William Caslon & Alexander Phemister; Tobias Frere-Jones & Nina Stössinger | 1722 London; 2025 Brooklyn | London, England (model: William Caslon's types, 1722, with Alexander Phemister's old-styles behind the italics — see Basis) | Brooklyn, New York, USA (Frere-Jones Type, 126 13th St) | Release 2025-10-01 ([Typecache](https://typecache.com/news/6518/)); studio ([frerejones.com](https://frerejones.com/)). Original author promoted into the Designer column: Edgar draws on "the 18th-century engraver William Caslon and the Scottish typographer and punchcutter, Alexander Phemister" ([PRINT Magazine](https://www.printmag.com/type-tuesday/frere-jones-type-edgar-typeface/), [Caslon](https://en.wikipedia.org/wiki/William_Caslon)). **Phemister added to the Designer column 2026-08-12 (owner ruling), sharing Caslon's stage rather than opening a third one**: Phemister supplied "the spark for Edgar's italics" per the same piece, but his old-styles are 1850s–60s Boston/Edinburgh work with no single pinned year of their own, so he rides in the 1722 stage alongside Caslon (`&`, same convention as Quattrocento Sans' and Libre Franklin's multi-person single-stage credits) rather than getting a separate Lineage entry |
| B | LibreCaslonText | Libre Caslon Text | William Caslon; Pablo Impallari & Rodrigo Fuenzalida | 1722 London; 2012 Rosario, Argentina | London, England (William Caslon's foundry) | Rosario, Argentina (Impallari); Fuenzalida: Venezuela, city uncertain (Caracas-trained) | Initial release 2012-11-08 ([FONTLOG](https://github.com/impallari/Libre-Caslon-Text/blob/master/FONTLOG.txt)); [Caslon](https://en.wikipedia.org/wiki/William_Caslon); [Impallari Behance](https://www.behance.net/impallari). Original author promoted into the Designer column, **with a caveat worth an owner ruling**: the family's own Google Fonts blurb says "pretty much all other digital Caslons revivals are based on 18th Century specimens by William Caslon I and William Caslon II. Libre Caslon, instead, is based on hand lettering artist Caslon interpretations typical of 1950s advertising" ([DESCRIPTION.en_us.html](https://raw.githubusercontent.com/google/fonts/main/ofl/librecaslontext/DESCRIPTION.en_us.html)). So Caslon is the origin of the style, not the direct model — the direct model is anonymous 1950s lettering, one remove further out. The 1722 stage is kept because it is where the style begins and because Edgar's row treats Caslon the same way; flagged rather than changed |
| B | Junicode | Junicode SemiCond | ; Peter S. Baker | 1703 Oxford; 1998, 2023 Charlottesville, Virginia | Oxford, England (Oxford University Press — the "Pica Roman" purchased 1692, printed in Hickes' Thesaurus 1703–05) | Charlottesville, Virginia, USA (Baker, University of Virginia — both 1998 and 2023 eras) | [Junicode design history](https://junicode.sourceforge.io/design.html); Junicode 2 2023-08-18 ([Wikipedia](https://en.wikipedia.org/wiki/Junicode)); [Baker at UVA](https://english.as.virginia.edu/people/peter-baker). Note: Baker says the model "looks more like" the 1692 Pica Roman than a Fell/Walpergen type. **Punchcutter unknown/unrecorded, researched 2026-08-12 — no name to promote.** Baker's own design history names nobody for it: it says only that "the type used for Hickes's *Thesaurus* may be one of those assembled by John Fell (1625–86)" before ruling that out in favour of the 1692 purchase, and it names an individual only for the Old English side (the Pica Saxon "commissioned by the early Anglo-Saxonist Franciscus Junius (1591–1677)"). Wikipedia's Junicode article names none either. **Do not "fix" this with Peter de Walpergen**, which several font sites attach to a Pica: that is the Fell bequest's Pica — IM Fell **DW** Pica is literally *de Walpergen* Pica ([1001 Fonts](https://www.1001fonts.com/im-fell-dw-pica-font.html)) — and Baker's page explicitly distinguishes his model FROM the Fell types. The 1692 stage therefore carries an institution (OUP) and no person, so the device's original-author line stays empty here. **Per-stage split 2026-08-13: stage 1 empty, Baker on stage 2** — exactly as that sentence specifies, and the same placement the InknutJunicode row already uses for Junicode's half of its four stages |
| A | Rosarivo | Rosarivo | ; Pablo Ugerman | 1470 Venice; 2011 Buenos Aires | Venice, Italy (model: Nicolas Jenson's roman, 1470) | Buenos Aires, Argentina (Ugerman, UBA postgrad program) | [Font Squirrel](https://www.fontsquirrel.com/fonts/rosarivo). **⚠ Jenson NOT promoted — the model attribution itself is uncited, flagged for the owner rather than changed.** Searched 2026-08-12: no source ties this face to Jenson. Its own Google Fonts blurb names no model at all — "a typeface designed for use in letterpress printing … calligraphic and humanistic forms … originates from work presented to the post-graduate course in Typeface Design at the University of Buenos Aires in 2011" ([DESCRIPTION.en_us.html](https://raw.githubusercontent.com/google/fonts/main/ofl/rosarivo/DESCRIPTION.en_us.html)) — and neither Font Squirrel nor [Fonts In Use](https://fontsinuse.com/typefaces/44592/rosarivo) adds one. The name points somewhere else entirely: it honours **Raúl Mario Rosarivo** (1903–66), the Argentine typographer known for analysing the *Gutenberg* Bible's proportions ([Wikipedia](https://en.wikipedia.org/wiki/Ra%C3%BAl_Rosarivo)) — Mainz, not Venice. So the 1470 Venice stage looks like an inference, and promoting Jenson would harden a guess into a credit. **Owner ruling 2026-08-12: ship as written, flagged, revisit later**. **Per-stage split 2026-08-13: stage 1 empty, Ugerman on stage 2.** Ugerman is unambiguously the 2011 Buenos Aires designer (the face came out of the UBA postgrad course that year), and the empty first slot is the direct consequence of the 2026-08-12 ruling not to promote Jenson — the device now renders the 1470 stage as a bare year and place rather than attaching a name to it |
| B | InknutAntiqua62 | Inknut Antiqua | ; Claus Eggers Sørensen | 1469 Venice; 2014 Amsterdam | Venice, Italy (model: Johannes de Spira's roman, 1469) | Amsterdam, Netherlands (Sørensen, Danish, based in Amsterdam) | [GitHub](https://github.com/clauseggers/Inknut-Antiqua); [FontsArena](https://fontsarena.com/inknut-antiqua-by-claus-eggers-sorensen/). **⚠ De Spira NOT promoted — the model attribution itself is uncited, flagged for the owner rather than changed.** Searched 2026-08-12: Sørensen names a *period*, never a printer. His own README and the Google Fonts blurb both say the face is drawn "to evoke Venetian incunabula and humanist manuscripts, but with the quirks and idiosyncrasies of the kinds of typefaces you find in this artisanal tradition" ([DESCRIPTION.en_us.html](https://raw.githubusercontent.com/google/fonts/main/ofl/inknutantiqua/DESCRIPTION.en_us.html)) — no de Spira, no Jenson, no year. FontsArena and [Fonts In Use](https://fontsinuse.com/typefaces/38696/inknut-antiqua) add nothing. "Johannes de Spira's roman, 1469" is a reasonable reading of "Venetian incunabula" — de Spira printed the first book in Venice in 1469 — but it is the table's inference, not the designer's claim, so no name goes in the Designer column. **Owner ruling 2026-08-12: ship as written, flagged, revisit later**. **Per-stage split 2026-08-13: stage 1 empty, Sørensen on stage 2 (2014 Amsterdam)** — the same placement the InknutJunicode row has already used for this typeface's half of its four stages since that row was written |
| B | InknutJunicode | Inknut Antiqua + Junicode | Claus Eggers Sørensen; Peter S. Baker | 1469 Venice; 2014 Amsterdam; 1703 Oxford; 1998 Charlottesville | Venice, Italy (Inknut's model: Johannes de Spira's roman, 1469) and Oxford, England (Junicode's model: the OUP "Pica Roman" purchased 1692, printed in Hickes' Thesaurus 1703–05) | Amsterdam, Netherlands (Sørensen) and Charlottesville, Virginia, USA (Baker) | TWO TYPEFACES IN ONE FAMILY, so both columns carry both, roman first: Inknut Antiqua supplies regular and bold, Junicode supplies the real cut italics that Inknut's foundry repo has at no weight. Rows above for [InknutAntiqua62](#) and [Junicode](#) are the source of every fact here — this row only orders and compresses them. COMPRESSED DELIBERATELY: Junicode's stage reads "1998, 2023 Charlottesville, Virginia" in its own row, and the full concatenation measures 947 px at 8 pt against a 472 px line, so it needs three lines where the picker gives two (FontSelectionActivity.cpp:29, kColophonLines = 2) and the Junicode half would be the part truncated away. Dropping the 2023 revision rather than the 1998 initial release follows the lineage convention, which treats "," as separating a release from its later revisions within one stage. Do not "restore" the full string without raising kColophonLines |
| S | Almendra | Almendra | Chancery hand; Ana Sanfelippo | 1522 Rome; 2011 Buenos Aires | **Stage 1 names a STYLE, not a face** (owner 2026-08-27: *"add a style for author and a rough year and place for when this style started or peaked or earliest established"*). 1522 Rome is Arrighi's *La Operina*, the first printed manual of the cancelleresca and the point the chancery hand became a taught, codified style rather than a papal-chancery house practice. It is NOT a claim that Sanfelippo worked from Arrighi, and it does not cover the GOTHIC half of her description — textura peaks c. 1300 in northern France, two centuries and a country away, and no single date-and-place holds both. Earlier anchor if ever wanted: the humanist cursive it grew from, c. 1420 Florence (Niccoli, Poggio). Superseding the one-stage form: **RESOLVED 2026-08-27 in favour of the evidence** — owner: *"replace 1350 london with something more accurate."* The row now carries ONE stage and sorts on 2011. The 2026-08-24 ruling that restored `1350 London` is superseded and the disagreement below is settled | Buenos Aires, Argentina (Sanfelippo; drawn as her graduation typeface at CDT UBA) | **The evidence and the ruling disagree, and both are recorded on purpose.** Three primary sources date the DESIGN to 2011 and none of them dates a model: (1) the shipped TTFs' own name ID 0, `Copyright (c) 2011-2012, Ana Sanfelippo`, two adjacent years so 2011 under the adjacent-year rule; (2) [METADATA.pb](https://raw.githubusercontent.com/google/fonts/main/ofl/almendra/METADATA.pb), `date_added: 2011-12-19`; (3) [DESCRIPTION.en_us.html](https://raw.githubusercontent.com/google/fonts/main/ofl/almendra/DESCRIPTION.en_us.html), “related to the chancery and gothic hands” — classes of hands worked by many anonymous scribes, not one dated model, and naming no country. Devroye agrees and adds the school. A 2026-08-24 pass removed `1350 London` on that basis; the owner ruled it back the same day. It therefore stands as a **placement decision** — 1350 is the sort key, which seats the face at the old end of the picker beside Inknut rather than leading it — and NOT as a dating claim. **Do not cite this row as evidence for a 1350 anything.** |

## Designer column: two names, semicolon-separated

Added 2026-08-12, for the two-line device subtitle ("Original author · YEAR
PLACE" over "Digitizer · YEAR PLACE"). The Designer column now uses the **same
stage separator as the Lineage column**: a semicolon splits the original
author from whoever digitised or revived the design, in the same order as the
lineage stages, so line N of the subtitle pairs with stage N. `Warren Chappell;
Hirwen Harendal` reads "Chappell drew it, Harendal digitised it", the way
`1938 New York; 2011 France` reads "there, then elsewhere". A single-stage
born-digital family keeps one name and gets one line.

**Studios and institutions are not people, and the rule now bites.** Lexica's
2019 stage carried "Applied Design Works" — a studio — in a column this section
reserves for individuals, which the row itself flagged. Resolved 2026-08-13 to
**Elliott Scott & Craig Dobie**, the lead designer and creative director the
row's own Basis cell already named, the same designer-plus-director pairing
Newsreader uses. Antpolt gained its stage-1 credit in the same pass: **Adam
Półtawski** drew it, first cast at Jan Idzkowski's foundry in Warsaw in 1931,
and the GUST duo digitised it — the Basis cell had said so since the row was
written while the Designer column carried only the digitisers.

**One family still has no row here at all**: `iAWriterMono` is credited in
src/FontDisplayNames.h (van Rosmalen/Bold Monday for the 2009 Nitti, Oliver
Reichenstein for iA's 2018 Zurich adaptation) on the strength of that header's
own comment and its sibling Duo/Quattro entries, NOT on a cited row here. That
is a gap in this table, not a settled fact.

**A stage can have no person, and that is a real answer.** Several rows carry an
institution, a company or an anonymous tradition where a name would go —
Junicode (Oxford University Press), Venetian 301 (Bitstream),
Lexica Ultralegible (an unrecorded punchcutter),
Inknut Antiqua and Rosarivo (model attributions the table declines to harden
into a credit), Caledonia CC (three revising hands under one name the face
already carries), Newsreader (no original author added). The renderer handles an
empty designer slot on either line rather than being fed a company name as if it
were a designer. Only individuals go in this column; that rule is why Bitstream,
ATF, OUP and Applied Design Works are absent from it — **except that Lexica
Ultralegible's row does still carry Applied Design Works, an unresolved
contradiction flagged in its Basis cell, not a precedent.**

**How an empty slot is written.** The slot is empty, not absent: the semicolons
still have to count out to the same number of stages as the Lineage column, or
the device gives up on pairing and falls back to one flat line. So a row whose
model has no author leads with the separator (`; Ana Sanfelippo`), a row whose
revival has no named individual trails with it (`Bruce Rogers;`), and an interior
gap doubles it (`; Claus Eggers Sørensen; ; Peter S. Baker`). A single-stage
born-digital family keeps one bare name and gets one line.

**Repeating a name is allowed where the evidence says so.** GT Alpina reads
`Reto Moser; Reto Moser` because its semicolon separates two cities of one
designer's work rather than an original from a digitiser. Leaving the second slot
empty there would say the 2020 release had no designer, which is false. This is
the only such row.

## Owner rulings — 2026-08-12

Four factual issues turned up while sourcing the original-author credits, each
a claim in a column that research pass wasn't authorised to edit on its own.
All four were put to the owner and ruled on the same day; this section now
records the rulings, not open questions.

1. **Lexica Ultralegible — fixed.** "The grotesque, named in Vincent Figgins'
   1832 specimen" attributed the wrong term to the wrong founder: Figgins
   coined **sans-serif**, not *grotesque* — that word is **William
   Thorowgood's**, first recorded on his Seven-line Grotesque of c. 1834
   ([Commercial
   Type](https://commercialtype.com/about/collections/thorowgood_grotesque)).
   Two founders, two words, two years. **Ruling: keep the 1832 London stage,
   fix the wording** — the Creation place parenthetical now says "sans serif"
   rather than "the grotesque"; the 2026-08-03 ruling that anchors the lineage
   on this stage is untouched.
2. **Rosarivo → Nicolas Jenson, Inknut Antiqua → Johannes de Spira, Newsreader
   → John Baskerville — none of these three model attributions has a citation
   behind it.** In all three the font's own documentation names no historical
   model (Rosarivo names none at all; Inknut names only "Venetian incunabula";
   Newsreader calls itself "an original typeface"), and no third-party source
   supplies one — details and links in each row's Basis. Rosarivo is the
   sharpest case: the family is named for **Raúl Rosarivo**, a Gutenberg
   scholar, pointing at Mainz rather than Venice. **Ruling: ship all three as
   written, flagged in the Basis cell, revisit later** — no lineage or
   attribution change for now.
3. **Libre Caslon Text's direct model is 1950s lettering, not Caslon's
   types.** The family's own blurb says it is "based on hand lettering artist
   Caslon interpretations typical of 1950s advertising", explicitly *unlike*
   other digital Caslons — the 1722 stage is one remove further from the face
   than the row implies. **Ruling: keep 1722 London and the Caslon credit, note
   the discrepancy** — done, in the row's Basis cell; no value changed.
4. **Two credit omissions — added.** Newsreader is credited to **Jean-Baptiste
   Levée** alongside Hugues Gentile on Production Type's own page, and Edgar
   draws on **Alexander Phemister** as well as Caslon (his old-styles gave
   Edgar its italics). **Ruling: add both** — Levée now shares Newsreader's
   digitiser credit, Phemister now shares Caslon's 1722 stage on Edgar.

## Sync status vs `FontDisplayNames.h`

**In sync as of 2026-08-13.** The Designer column extension of 2026-08-12 landed
in `kEntries` in two passes: `041c4949` copied across the seven rows that had
gained an original-author credit (Libris, TeX Gyre Schola, Coelacanth, Goudy
Bookletter, Libre Franklin, Edgar, Libre Caslon Text) and taught `subtitle()` to
pair stage N of `designer` with stage N of `lineage`; the 2026-08-13 pass gave a
per-stage string to the nine rows that still had one name against two or more
stages — Almendra, Caledonia CC, GT Alpina, Inknut Antiqua, Newsreader, Rosarivo,
Junicode, Lexica Ultralegible, Venetian 301 — and picked up Newsreader's
outstanding `& Jean-Baptiste Levée` at the same time. Almendra has since left
that group: its 2026-08-24 rewrite dropped the invented first stage, so it is
one name against one stage and needs no split at all.

**Two rows are still deliberately unpaired**, and both need a credit this table
does not carry rather than a re-split of one it does:

- **Antykwa Półtawskiego** — one credit (the GUST digitising duo) against two
  stages. The 1931 stage is Adam Półtawski, named in this row's Basis cell and in
  `FontDisplayNames.h`'s comment but never in the Designer column.
- **iA Writer Mono** — one credit (Nitti's, `Pieter van Rosmalen, Bold Monday`)
  against two stages, so it is the 2018 Zurich stage that has no name.

Both fall back to the flat one-line subtitle, which is accurate, just unsplit.
Adding either name is a change of attribution, not a placement, so it wants an
owner ruling.

The header stores one `lineage` string per family, copied verbatim from
the Lineage column here, plus a numeric `earliestYear` for the picker's
reverse-chronological sort. Device subtitle: one line per stage,
`Designer · YEAR PLACE`, or the year and place alone where the stage has no
person.

The separate `years` and `place` fields were merged on 2026-08-04 — see the
Lineage format rule above. The `Creation place` / `Digital place` columns are
sourced working notes and do NOT reach the device.

## Excluded dates (and why)

- **Libre Caslon Text VF** (~2019, Stephen Nixon for Google Fonts) — inferred
  from repo activity, no dated announcement.
- **Coelacanth v0.005** (2022, CTAN) — minor package bump, not a revision.
- **Source Serif's Fournier model** — cited as loose inspiration, never with a
  year (Fournier's types span the 1740s–60s).
- **Junicode 1.000** (2017) — intermediate milestone between 1998 and the 2023
  rewrite. (Wikipedia also lists 2001 as first release; Baker's copyright string
  says 1998.)
- **Centaur 1929** (Monotype commercial release) — the 1914 design is the model
  year for both Coelacanth and Venetian 301.
- **Newsreader 2021** (Production Type official launch) — collapsed as release
  lag behind the 2020 Google Fonts release.
- **Rosarivo 2012** Google Fonts date — release lag.
- **Almendra's `1350 London` first stage** — removed 2026-08-24 on the evidence
  above, RESTORED the same day by owner ruling, and **removed again on
  2026-08-27** at his word ("replace 1350 london with something more accurate").
  Settled: the row carries one stage, `2011 Buenos Aires`, and sorts on 2011.

  Two things learned on the way out, worth keeping so the stage is not invented
  a third time. **There is nothing accurate to put in a first stage** — every
  other row's is a dated historical MODEL that can be pointed at (Inknut's 1469
  Venice is Jenson), while Almendra is an original whose own description says
  only "related to the chancery and gothic hands": classes of anonymous scribal
  writing spanning centuries and two regions, cancelleresca being 16th-century
  Italian and textura 13th-15th-century northern European. No single date and no
  single place covers both.

  And **1350 London was wrong twice over**, not merely uncited. English chancery
  hand is c. 1400+, fifty years after that date, and the hand actually written in
  1350s London was Anglicana, with textura for formal books. The stage named a
  script that was not yet in use in the place it named.

  Sort consequence, stated because it is visible: Almendra leaves the old end of
  the picker beside Inknut and sorts to the front, which is where the list's own
  reverse-chronological rule puts a 2011 original. If the old position is ever
  wanted back it is the sort integer and nothing else — do not reintroduce a
  fictional stage to get it.
- **Almendra 2012** — the second year of the font's own `2011-2012` copyright,
  and Google Fonts' `date_added: 2011-12-19` falls inside it; adjacent-year
  rule, so 2011 stands alone.
- **Lexica Ultralegible's 2023 repo creation and 2025 pushes** — the repo
  predates the release and is still maintained; v1.0.0 (2024-10-06) is the
  first dated release. Its OFL string carries `2020 Braille Institute of
  America`, the upstream Atkinson copyright, not a Lexica date.
- **Atkinson Hyperlegible Next 2025** — a separate family from the one Lexica
  forked, and not installed here.
- **Libris' FontForge `FFTM` source-created stamp (2010-01-11)** — tooling
  metadata from when the file was started, not a release. The 2011 stage is
  dated from upstream's own `Libris-Std-20110117` release directory.
- **Caslon IV's 1816 sans (the first one cut)** — the earlier date, and the one
  a purist would pick for the style's origin. Passed over for 1832 because the
  other model-dated entries here take the year the model was *published as the
  thing this face descends from*, and 1832 is where Figgins prints the name.
  Owner ruling 2026-08-03; noted so the 16-year gap reads as a decision rather
  than an oversight.
