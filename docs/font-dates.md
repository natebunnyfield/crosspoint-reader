# Font dates — source data for `src/FontDisplayNames.h`

Editable source of truth for the Text Settings picker's designer/lineage subtitles
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

- **S** — the reading faces: Coelacanth, Edgar and TeX Gyre Schola for serif
  work; Libre Franklin (grotesque) and Libris (humanist, added 2026-08-12) for
  sans — two sans, permanently, because they fill different classification
  cells and a head-to-head only runs within one cell; owner ruling 2026-08-12,
  see docs/sd-card-fonts.md. Rosarivo and Quattrocento Sans were A-tier'd on
  2026-08-07 and are no longer shipped; Rosarivo is additionally withheld from
  the reader's list (src/ReadingFontList.cpp) because cards provisioned before
  that ruling still carry it. New work, previews and screenshots use Edgar or
  Coelacanth unless a sans is called for. These five are exactly the five in
  `installed_families` as of 2026-08-12 — that is a consequence of the
  rulings, not a rule: the two lists are still maintained separately, and A
  and C families are deliberately curated-but-not-shipped.
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
| B | GTAlpinaCond | GT Alpina | Reto Moser | 2011 Bern; 2020 Lucerne, Switzerland | Bern, Switzerland (Moser lives/works in Bern) | Lucerne, Switzerland (Grilli Type foundry) | Design start 2011 ([Fonts In Use](https://fontsinuse.com/typefaces/102321/gt-alpina)); release 2020 ([It's Nice That](https://www.itsnicethat.com/articles/grilli-type-gt-alpina-graphic-design-030220)); places ([Grilli Type](https://www.grillitype.com/about)). **No original-author gap to fill, checked 2026-08-12**: both stages are Reto Moser. The 2011 stage is not a historical model but a one-off custom face Moser built for a book marking the 75th anniversary of the Swiss Foundation for Alpine Research, which he then spent eight years expanding into the 2020 Grilli Type release — "GT Alpina by Reto Moser / © 2020 Grilli Type" on the foundry's own specimen ([specimen PDF](https://www.grillitype.com/api/storage/app/uploads/public/5e1/e4a/bac/5e1e4abac7b46609129786.pdf)), same designer both times per the It's Nice That piece. The semicolon here separates two cities of one designer's work, not an original from a digitiser, so the device's second subtitle line has no separate name to carry. Do not invent a historical model for this row |
| B | Lora | Lora | Olga Karpushina | 2011, 2019 Moscow | — (born digital) | Moscow, Russia (Karpushina; Cyreal foundry, Russia) | First release 2011 ([GitHub](https://github.com/cyrealtype/Lora-Cyrillic)); variable 2019 ([Cyreal](https://www.cyreal.org/fonts/lora/)); [Behance](https://www.behance.net/OlgaKarpushina) |
| A | CaledoniaCC | Caledonia CC | W.A. Dwiggins | 1938 Hingham, Mass.; 1988, 1994, 2026 Cambridge, Mass. | Hingham, Massachusetts, USA (Dwiggins' studio; for Mergenthaler Linotype, Brooklyn NY) | Cambridge, Massachusetts, USA (Carter & Cone: 1994 Time Caledonia, 2026 CC). 1988 New Caledonia: David Berlow, Adobe (USA) / Linotype AG (Germany) | 1938 design ([Wikipedia](https://en.wikipedia.org/wiki/Caledonia_(typeface))); 1988 = New Caledonia PostScript ([Wikipedia](https://en.wikipedia.org/wiki/Caledonia_(typeface))); 1994 = Time Caledonia by Matthew Carter; CC release April 2026 ([Carter & Cone](https://carterandcone.com/font/caledonia/), [Creative Boom](https://www.creativeboom.com/resources/the-best-new-typefaces-for-april-2026/)); Dwiggins in Hingham 1906–56 ([Britannica](https://www.britannica.com/biography/W-A-Dwiggins)) |
| S | LibrisADF | Libris | Warren Chappell; Hirwen Harendal | 1938 New York; 2011 France | New York City, USA (model: Lydian, Warren Chappell, cut and cast by American Type Founders) | France (Arkandis Digital Foundry; no city published) | Two-stage credit: Warren Chappell drew the model, Hirwen Harendal the digitisation ([Chappell](https://en.wikipedia.org/wiki/Warren_Chappell) is cited again under Creation place below). Model named by the shipped font itself: name table ID 10 reads "Libris is a sans serif font intented to mimic Lydian typeface" [sic], and upstream's NOTICE.txt calls it "The Libris collection". Lydian 1938 ([Wikipedia](https://en.wikipedia.org/wiki/Lydian_(typeface)), [Typewolf](https://www.typewolf.com/lydian)); [Fonts In Use](https://fontsinuse.com/typefaces/6/lydian) dates the release "1938-40", collapsed to the design year by the adjacent-year rule. Creation place is New York rather than ATF's Jersey City plant because Chappell designed independently — Koch's pupil at Offenbach 1931–32, then his own studio in New York City until just before the war ([Wikipedia](https://en.wikipedia.org/wiki/Warren_Chappell)) — unlike the Benton-for-ATF models behind Franklin Gothic and Century Schoolbook, which do take Jersey City. Digitisation 2011: upstream's release directory is `Libris-Std-20110117`, 2011-01-17 ([salsa.debian.org](https://salsa.debian.org/fonts-team/fonts-adf/-/tree/master/Libris-Std-20110117)), and the shipped v1.007 OTF's `head.created` is 2011-10-24, the same year. Designer and foundry from the name table (ID 8 "Arkandis Digital Foundry", ID 9 "HARENDAL Hirwen"), corroborated by [Luc Devroye](https://luc.devroye.org/fonts-47797.html); ADF is French but neither [its own site](https://arkandis.tuxfamily.org/) nor Debian's `debian/copyright` gives a city, so the digital place is bare "France", the same treatment Host Grotesk gets. Licence GPL v2+ with font exception (upstream NOTICE.txt; Debian `debian/copyright` "GPL-2+ with Font exception", © 1998–2019 Hirwen Harendal, Arkandis Digital Foundry), so like TeX Gyre Schola it needs no gitignored local files and rebuilds from URLs anywhere. All four styles are real cuts, v1.007 |
| S | TeXGyreSchola | TeX Gyre Schola | Morris Fuller Benton; Bogusław Jackowski & Janusz M. Nowacki | 1918 Jersey City; 2007 Gdańsk | Jersey City, New Jersey, USA (model: Century Schoolbook, Morris Fuller Benton for American Type Founders, roman 1918) | Gdańsk / Grudziądz, Poland (GUST e-foundry, seated in Toruń; via URW Century Schoolbook L) | First stable v0.996 2007-01-14, final v2.005 2009 ([CTAN README](http://ftp.math.utah.edu/pub/tex/historic/fonts/tex-gyre/schola/0.996/README-TeX-Gyre-Schola.txt), [GUST](https://www.gust.org.pl/projects/e-foundry/tex-gyre/schola)); Benton/ATF ([Wikipedia](https://en.wikipedia.org/wiki/Century_type_family), [ATF](https://en.wikipedia.org/wiki/American_Type_Founders)); original author promoted into the Designer column — Century Schoolbook is Benton's, drawn for ATF ([Morris Fuller Benton](https://en.wikipedia.org/wiki/Morris_Fuller_Benton)); digitisers ([Typoteka: Jackowski](https://typoteka.pl/en/designer/boguslaw-jackowski), [Nowacki](https://typoteka.pl/en/designer/janusz-marian-nowacki)) |
| C | Antpolt | Antykwa Półtawskiego | Bogusław Jackowski & Janusz M. Nowacki | 1931 Warsaw; 2003, 2009 Gdańsk | Warsaw, Poland (Adam Półtawski, born Warsaw 1881; first cast at Jan Idzkowski's foundry, Warsaw, 1931 — used through the 1970s as the text face for, among other work, Paderewski's 27-volume complete Chopin edition) | Gdańsk, Poland (GUST e-foundry — same Jackowski/Nowacki team and city as TeX Gyre Schola above) | Design and first casting: CTAN [poltawski README](https://mirrors.ctan.org/fonts/poltawski/README) ("designed in the twenties... by... Adam Półtawski... first cast in Jan Idzkowski's foundry (Warsaw, Poland) in 1931"; Półtawski's birth/death and dates from the same README). Digitization years 2003 and 2009 are the shipped font's own name-table copyright string ("Copyright 2003, 2009 B. Jackowski and J. M. Nowacki... on behalf of the TeX Users Groups"); CTAN's v1.101 (2010-10-09) is a point-release one year later and collapses under the adjacent-year rule. Designers/place per the same Typoteka bios cited for TeX Gyre Schola. GUST Font License (free/OFL-equivalent), so the recipe rebuilds from CTAN on any machine — no `local_fonts/` needed |
| S | Coelacanth | Coelacanth | Bruce Rogers; Ben Whitmore | 1914 New York; 2014 Waiheke Island, New Zealand | New York, USA (Centaur: Bruce Rogers for the Metropolitan Museum of Art; matrices cut in Chicago) | Waiheke Island (Auckland), New Zealand | Centaur 1914 ([Wikipedia](https://en.wikipedia.org/wiki/Centaur_(typeface))); release 2014-08-29 ([Font Library](https://fontlibrary.org/en/font/coelacanth)); "Ben Whitmore from Waiheke, New Zealand" ([ben-whitmore.com](http://ben-whitmore.com/coelacanth-type-family/)). Original author promoted into the Designer column on the designer's own framing — that page is titled "Coelacanth: a type family inspired by Bruce Rogers' Centaur" — and Rogers drew Centaur for the Metropolitan Museum of Art ([Bruce Rogers](https://en.wikipedia.org/wiki/Bruce_Rogers_(typographer))) |
| B | Venetian301 | Venetian 301 | Bruce Rogers | 1914 New York; 1990 Cambridge, Mass. | New York, USA (Centaur: Rogers for the Metropolitan Museum of Art) | Cambridge, Massachusetts, USA (Bitstream's city at the time; later Marlborough MA) | Centaur 1914 ([Wikipedia](https://en.wikipedia.org/wiki/Centaur_(typeface))); Bitstream © 1990 ([Bitstream key](http://www.sanskritweb.net/forgers/bitstream2.pdf)); Cambridge-era Bitstream ([Wikipedia](https://en.wikipedia.org/wiki/Bitstream_Inc.)). Checked 2026-08-12 and left as-is: Rogers is the ORIGINAL author here, not a digitiser ([Bruce Rogers](https://en.wikipedia.org/wiki/Bruce_Rogers_(typographer))), and the 1990 stage was Bitstream's staff work with no individual named in the font or the licence key — so this row has one person and one company, and a two-line device subtitle should render the second line's designer slot empty rather than printing "Bitstream" as a person |
| B | GoudyBookletter1911 | Goudy Bookletter | Frederic W. Goudy; Barry Schwartz | 1911 New York; 2009 St. Paul | New York City, USA (Goudy's Village Press era; Kennerley Old Style for publisher Mitchell Kennerley) | St. Paul, Minnesota, USA (Schwartz) | Kennerley 1911 ([Wikipedia](https://en.wikipedia.org/wiki/Kennerley_Old_Style)); League of Moveable Type ~2009 ([League](https://www.theleagueofmoveabletype.com/goudy-bookletter-1911)), whose page states the basis outright — "Based on Frederic Goudy's Kennerley Oldstyle" — so the full name is promoted into the Designer column ([Frederic W. Goudy](https://en.wikipedia.org/wiki/Frederic_Goudy)) in place of the Creation place column's shorthand "Goudy's Village Press era"; Schwartz in St. Paul ([Luc Devroye](https://luc.devroye.org/fonts-46940.html), single source) |
| S | LibreFranklin | Libre Franklin | Morris Fuller Benton; Pablo Impallari, Rodrigo Fuenzalida & Nhung Nguyen | 1902 Jersey City; 2016 Rosario, Argentina | Jersey City, New Jersey, USA (model: Franklin Gothic, Morris Fuller Benton for American Type Founders, 1902 — note ATF's Jersey City plant opened 1903, so the design marginally predates the move) | Rosario, Argentina (Impallari Type) | Franklin Gothic 1902 ([Wikipedia](https://en.wikipedia.org/wiki/Franklin_Gothic)); Libre Franklin added to Google Fonts 2016-06-20 ([METADATA.pb](https://raw.githubusercontent.com/google/fonts/main/ofl/librefranklin/METADATA.pb)) and preinstalled on Axis-Praxis 2016-11 ([Axis-Praxis](https://www.axis-praxis.org/blog/2016-11-11/3/new-font-libre-franklin-by-pablo-impallari-now-preinstalled)); three digitisers from the font's name table (ID 9). Original author promoted into the Designer column: the family's own Google Fonts blurb calls itself "an interpretation and expansion of the 1912 Morris Fuller Benton classic" ([DESCRIPTION.en_us.html](https://raw.githubusercontent.com/google/fonts/main/ofl/librefranklin/DESCRIPTION.en_us.html), [Benton](https://en.wikipedia.org/wiki/Morris_Fuller_Benton)) — note that blurb says **1912** where Wikipedia dates the design 1902; the 1902 in the Lineage column is kept, as the cited design year, and the 1912 is not a second stage |
| S | QuattrocentoSans | Quattrocento Sans | Pablo Impallari, Igino Marini & Brenda Gallo | 2011 Rosario, Argentina & Osimo, Italy | — (born digital) | Rosario, Argentina (Impallari Type); Osimo, Italy (Igino Marini, iKern) | v1.002 initial release 5 Apr 2011, Impallari & Marini; v2 "family released" 8 Feb 2012 adds the bold (31 Jan 2012), the italics (3 Feb 2012) and the iKerning, with Brenda Gallo credited from v1.003 — all from the [FONTLOG](https://raw.githubusercontent.com/google/fonts/main/ofl/quattrocentosans/FONTLOG.txt). Copyright 2011 names all three ([METADATA.pb](https://raw.githubusercontent.com/google/fonts/main/ofl/quattrocentosans/METADATA.pb)). Google Fonts `date_added` 2012-02-15 is release lag off that Feb 2012 build; 2012 itself is 10 months after the roman and collapses under the adjacent-year rule, which names this exact 2011/2012 case. **No model year**: the name means the Italian 1400s and the face is the companion to Quattrocento (the serif), but neither the FONTLOG nor the [specimen](https://raw.githubusercontent.com/google/fonts/main/ofl/quattrocentosans/DESCRIPTION.en_us.html) names a historical model for the sans, so it stays born-digital rather than getting an invented lineage — reversible by owner ruling, as Lexica Ultralegible's 1832 was. Marini in Osimo ([Luc Devroye](https://luc.devroye.org/fonts-43047.html)); Impallari Type in Rosario, as the other Impallari rows. Gallo's city is not pinnable from any source and is left out |
| C | FreightSans | Freight Sans | Joshua Darden | 2004 Brooklyn | — (born digital) | Brooklyn, New York, USA (Darden Studio); published by GarageFonts / Phil's Fonts, Rockville, Maryland | Design year 2004 from the shipped font's own copyright string ("Copyright © 2004, Joshua Darden & Phil's Fonts, Inc.") and `head.created` (2004-12-08 on the italics); the Freight superfamily's public release was 2005-06-01 through GarageFonts ([Fonts In Use](https://fontsinuse.com/typefaces/2773/freight-sans), [CreativePro](https://creativepro.com/new-type-design-freight-released/)) — one year of lag, collapsed by the adjacent-year rule. Darden Studio founded in Brooklyn 2004–2005, Freight published soon after ([Wikipedia](https://en.wikipedia.org/wiki/Joshua_Darden)); designer from the font's name table (ID 9), publisher from ID 11 (garagefonts.com) and the ID 0 phone number's Maryland area code |
| A | LexicaUltralegible | Lexica Ultralegible | Applied Design Works & Jacob Perez | 1832 London; 2019 New York; 2024 El Paso, Texas | London, England (model: the style Figgins printed as "sans serif" in his 1832 specimen — not "the grotesque"; see Basis) | New York, USA (Applied Design Works, NY/LA; for Braille Institute of America, Los Angeles); El Paso, Texas, USA (Jacob Perez) | Lineage starts 1832 London, owner ruling 2026-08-03: Figgins' 1832 specimen is where "sans serif" is first printed as the name of the style this face belongs to, and the tier's other entries are dated from their model rather than their release ([Wikipedia: sans-serif](https://en.wikipedia.org/wiki/Sans-serif), [Figgins](https://en.wikipedia.org/wiki/Vincent_Figgins)). Atkinson Hyperlegible released 2019, Applied Design Works with Elliott Scott as lead designer and Craig Dobie as creative director ([Wikipedia](https://en.wikipedia.org/wiki/Atkinson_Hyperlegible), [Fast Company](https://www.fastcompany.com/90395836/this-typeface-hides-a-secret-in-plain-sight-and-thats-the-point)); studio in New York and Los Angeles, Braille Institute in Los Angeles ([Applied Design Works](https://helloapplied.com/braille-institute-of-america/), [It's Nice That](https://www.itsnicethat.com/news/applied-design-braille-institute-atkinson-hyperlegible-graphic-design-050821)); Lexica v1.0.0 released 2024-10-06 ([GitHub release](https://github.com/jacobxperez/lexica-ultralegible/releases)); Perez in El Paso ([GitHub profile](https://github.com/jacobxperez)). **No original author added, researched 2026-08-12 — and Figgins would be the wrong name anyway.** Figgins was a typefounder who cast and sold type, not a punchcutter: he "often commissioned work from two punchcutters about whom little is known named Perry and Edmonston", and no source names who cut the sans-serif punches ([Wikipedia: Vincent Figgins](https://en.wikipedia.org/wiki/Vincent_Figgins)). His claim on the 1832 stage is that he published it and coined the word — the article credits him with introducing "the term 'sans-serif', not previously attested" — which is naming, not authorship. So the 1832 stage has **no recorded individual designer**; the device's original-author line stays empty rather than print Figgins as the designer of a face he did not cut. **Fixed 2026-08-12 (owner ruling: keep the 1832 London stage, correct the wording)**: the Creation place parenthetical said "the grotesque" — that word is William Thorowgood's, first recorded c. 1834, a different founder entirely — where it meant Figgins' own claimed word, "sans-serif". Corrected above; the 2026-08-03 ruling to anchor the lineage on this stage stands unchanged |
| B | Newsreader | Newsreader | Hugues Gentile & Jean-Baptiste Levée | 1757 Birmingham; 2020 Paris | Birmingham, England (model: John Baskerville's types, 1757 — attribution uncited, owner ruling: ship flagged, see Basis) | Paris, France (Production Type) | Google Fonts 2020-07-01 ([METADATA.pb](https://raw.githubusercontent.com/google/fonts/main/ofl/newsreader/METADATA.pb)); [Production Type](https://productiontype.com/font/newsreader). **⚠ No original author added, and the Baskerville model itself is uncited — owner ruling 2026-08-12: ship as written, flagged, revisit later.** Searched 2026-08-12: neither Production Type's own family page nor the Google Fonts blurb names Baskerville or any historical model — the blurb is one sentence, "NewsReader is an original typeface designed by Production Type, primarily intended for continuous on-screen reading in content-rich environments" ([DESCRIPTION.en_us.html](https://raw.githubusercontent.com/google/fonts/main/ofl/newsreader/DESCRIPTION.en_us.html)), and the foundry page calls it "designed for on-screen, longer-form reading" with no lineage at all. Third-party writeups place it in the transitional-serif category generally ([Pimp my Type](https://pimpmytype.com/font/newsreader/)) but none name Baskerville. So the 1757 Birmingham stage rests on a category resemblance, not a sourced claim; kept as-is per owner ruling rather than removed under the "No unreliable dates" rule. **Jean-Baptiste Levée added to the Designer column 2026-08-12 (owner ruling)**: Production Type credits him alongside Gentile on the family page |
| S | Edgar | Edgar | William Caslon & Alexander Phemister; Tobias Frere-Jones & Nina Stössinger | 1722 London; 2025 Brooklyn | London, England (model: William Caslon's types, 1722, with Alexander Phemister's old-styles behind the italics — see Basis) | Brooklyn, New York, USA (Frere-Jones Type, 126 13th St) | Release 2025-10-01 ([Typecache](https://typecache.com/news/6518/)); studio ([frerejones.com](https://frerejones.com/)). Original author promoted into the Designer column: Edgar draws on "the 18th-century engraver William Caslon and the Scottish typographer and punchcutter, Alexander Phemister" ([PRINT Magazine](https://www.printmag.com/type-tuesday/frere-jones-type-edgar-typeface/), [Caslon](https://en.wikipedia.org/wiki/William_Caslon)). **Phemister added to the Designer column 2026-08-12 (owner ruling), sharing Caslon's stage rather than opening a third one**: Phemister supplied "the spark for Edgar's italics" per the same piece, but his old-styles are 1850s–60s Boston/Edinburgh work with no single pinned year of their own, so he rides in the 1722 stage alongside Caslon (`&`, same convention as Quattrocento Sans' and Libre Franklin's multi-person single-stage credits) rather than getting a separate Lineage entry |
| B | LibreCaslonText | Libre Caslon Text | William Caslon; Pablo Impallari & Rodrigo Fuenzalida | 1722 London; 2012 Rosario, Argentina | London, England (William Caslon's foundry) | Rosario, Argentina (Impallari); Fuenzalida: Venezuela, city uncertain (Caracas-trained) | Initial release 2012-11-08 ([FONTLOG](https://github.com/impallari/Libre-Caslon-Text/blob/master/FONTLOG.txt)); [Caslon](https://en.wikipedia.org/wiki/William_Caslon); [Impallari Behance](https://www.behance.net/impallari). Original author promoted into the Designer column, **with a caveat worth an owner ruling**: the family's own Google Fonts blurb says "pretty much all other digital Caslons revivals are based on 18th Century specimens by William Caslon I and William Caslon II. Libre Caslon, instead, is based on hand lettering artist Caslon interpretations typical of 1950s advertising" ([DESCRIPTION.en_us.html](https://raw.githubusercontent.com/google/fonts/main/ofl/librecaslontext/DESCRIPTION.en_us.html)). So Caslon is the origin of the style, not the direct model — the direct model is anonymous 1950s lettering, one remove further out. The 1722 stage is kept because it is where the style begins and because Edgar's row treats Caslon the same way; flagged rather than changed |
| B | Junicode | Junicode SemiCond | Peter S. Baker | 1703 Oxford; 1998, 2023 Charlottesville, Virginia | Oxford, England (Oxford University Press — the "Pica Roman" purchased 1692, printed in Hickes' Thesaurus 1703–05) | Charlottesville, Virginia, USA (Baker, University of Virginia — both 1998 and 2023 eras) | [Junicode design history](https://junicode.sourceforge.io/design.html); Junicode 2 2023-08-18 ([Wikipedia](https://en.wikipedia.org/wiki/Junicode)); [Baker at UVA](https://english.as.virginia.edu/people/peter-baker). Note: Baker says the model "looks more like" the 1692 Pica Roman than a Fell/Walpergen type. **Punchcutter unknown/unrecorded, researched 2026-08-12 — no name to promote.** Baker's own design history names nobody for it: it says only that "the type used for Hickes's *Thesaurus* may be one of those assembled by John Fell (1625–86)" before ruling that out in favour of the 1692 purchase, and it names an individual only for the Old English side (the Pica Saxon "commissioned by the early Anglo-Saxonist Franciscus Junius (1591–1677)"). Wikipedia's Junicode article names none either. **Do not "fix" this with Peter de Walpergen**, which several font sites attach to a Pica: that is the Fell bequest's Pica — IM Fell **DW** Pica is literally *de Walpergen* Pica ([1001 Fonts](https://www.1001fonts.com/im-fell-dw-pica-font.html)) — and Baker's page explicitly distinguishes his model FROM the Fell types. The 1692 stage therefore carries an institution (OUP) and no person, so the device's original-author line stays empty here |
| A | Rosarivo | Rosarivo | Pablo Ugerman | 1470 Venice; 2011 Buenos Aires | Venice, Italy (model: Nicolas Jenson's roman, 1470) | Buenos Aires, Argentina (Ugerman, UBA postgrad program) | [Font Squirrel](https://www.fontsquirrel.com/fonts/rosarivo). **⚠ Jenson NOT promoted — the model attribution itself is uncited, flagged for the owner rather than changed.** Searched 2026-08-12: no source ties this face to Jenson. Its own Google Fonts blurb names no model at all — "a typeface designed for use in letterpress printing … calligraphic and humanistic forms … originates from work presented to the post-graduate course in Typeface Design at the University of Buenos Aires in 2011" ([DESCRIPTION.en_us.html](https://raw.githubusercontent.com/google/fonts/main/ofl/rosarivo/DESCRIPTION.en_us.html)) — and neither Font Squirrel nor [Fonts In Use](https://fontsinuse.com/typefaces/44592/rosarivo) adds one. The name points somewhere else entirely: it honours **Raúl Mario Rosarivo** (1903–66), the Argentine typographer known for analysing the *Gutenberg* Bible's proportions ([Wikipedia](https://en.wikipedia.org/wiki/Ra%C3%BAl_Rosarivo)) — Mainz, not Venice. So the 1470 Venice stage looks like an inference, and promoting Jenson would harden a guess into a credit. **Owner ruling 2026-08-12: ship as written, flagged, revisit later** |
| B | InknutAntiqua62 | Inknut Antiqua | Claus Eggers Sørensen | 1469 Venice; 2014 Amsterdam | Venice, Italy (model: Johannes de Spira's roman, 1469) | Amsterdam, Netherlands (Sørensen, Danish, based in Amsterdam) | [GitHub](https://github.com/clauseggers/Inknut-Antiqua); [FontsArena](https://fontsarena.com/inknut-antiqua-by-claus-eggers-sorensen/). **⚠ De Spira NOT promoted — the model attribution itself is uncited, flagged for the owner rather than changed.** Searched 2026-08-12: Sørensen names a *period*, never a printer. His own README and the Google Fonts blurb both say the face is drawn "to evoke Venetian incunabula and humanist manuscripts, but with the quirks and idiosyncrasies of the kinds of typefaces you find in this artisanal tradition" ([DESCRIPTION.en_us.html](https://raw.githubusercontent.com/google/fonts/main/ofl/inknutantiqua/DESCRIPTION.en_us.html)) — no de Spira, no Jenson, no year. FontsArena and [Fonts In Use](https://fontsinuse.com/typefaces/38696/inknut-antiqua) add nothing. "Johannes de Spira's roman, 1469" is a reasonable reading of "Venetian incunabula" — de Spira printed the first book in Venice in 1469 — but it is the table's inference, not the designer's claim, so no name goes in the Designer column. **Owner ruling 2026-08-12: ship as written, flagged, revisit later** |
| B | InknutJunicode | Inknut Antiqua + Junicode | Claus Eggers Sørensen; Peter S. Baker | 1469 Venice; 2014 Amsterdam; 1703 Oxford; 1998 Charlottesville | Venice, Italy (Inknut's model: Johannes de Spira's roman, 1469) and Oxford, England (Junicode's model: the OUP "Pica Roman" purchased 1692, printed in Hickes' Thesaurus 1703–05) | Amsterdam, Netherlands (Sørensen) and Charlottesville, Virginia, USA (Baker) | TWO TYPEFACES IN ONE FAMILY, so both columns carry both, roman first: Inknut Antiqua supplies regular and bold, Junicode supplies the real cut italics that Inknut's foundry repo has at no weight. Rows above for [InknutAntiqua62](#) and [Junicode](#) are the source of every fact here — this row only orders and compresses them. COMPRESSED DELIBERATELY: Junicode's stage reads "1998, 2023 Charlottesville, Virginia" in its own row, and the full concatenation measures 947 px at 8 pt against a 472 px line, so it needs three lines where the picker gives two (FontSelectionActivity.cpp:29, kColophonLines = 2) and the Junicode half would be the part truncated away. Dropping the 2023 revision rather than the 1998 initial release follows the lineage convention, which treats "," as separating a release from its later revisions within one stage. Do not "restore" the full string without raising kColophonLines |
| B | Almendra | Almendra | Ana Sanfelippo | 1350 London; 2011 Buenos Aires | London, England (model: English chancery hand, ~1350) | Buenos Aires, Argentina (Sanfelippo, CDT UBA postgrad program) | [Font Squirrel](https://www.fontsquirrel.com/fonts/almendra); [METADATA.pb](https://raw.githubusercontent.com/google/fonts/main/ofl/almendra/METADATA.pb). **No named scribe found for the ~1350 chancery-hand model — anonymous tradition, researched 2026-08-12.** Sanfelippo's own description names a class of hands and nothing narrower: "Almendra is a typeface design based on calligraphy. Its style is related to the chancery and gothic hands" ([DESCRIPTION.en_us.html](https://raw.githubusercontent.com/google/fonts/main/ofl/almendra/DESCRIPTION.en_us.html)). No source names a scribe, a manuscript, a house, or a date — chancery and gothic hands are scribal traditions worked by many anonymous hands across centuries, not one person's design, so there is genuinely no original author to credit and the device's second subtitle line should stay empty. **⚠ Related, flagged not changed**: the "~1350" and "London" in the model parenthetical are likewise uncited — no source connects this face to a specific century or country — so under the "No unreliable dates" and "Places" rules the 1350 London stage is a candidate for removal, an owner call |

## Designer column: two names, semicolon-separated

Added 2026-08-12, for the two-line device subtitle ("Original author · YEAR
PLACE" over "Digitizer · YEAR PLACE"). The Designer column now uses the **same
stage separator as the Lineage column**: a semicolon splits the original
author from whoever digitised or revived the design, in the same order as the
lineage stages, so line N of the subtitle pairs with stage N. `Warren Chappell;
Hirwen Harendal` reads "Chappell drew it, Harendal digitised it", the way
`1938 New York; 2011 France` reads "there, then elsewhere". A single-stage
born-digital family keeps one name and gets one line.

**A stage can have no person, and that is a real answer.** Four rows carry an
institution, a company or an anonymous tradition where a name would go —
Junicode (Oxford University Press), Venetian 301 (Bitstream), Almendra
(anonymous scribal hands), Lexica Ultralegible (an unrecorded punchcutter).
The renderer must handle an empty designer slot on either line rather than
being fed a company name as if it were a designer. Only individuals go in this
column; that rule is why Bitstream, ATF, OUP and Applied Design Works are
absent from it.

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

**Out of sync as of 2026-08-12 — the Designer column was extended and
`kEntries` has not been updated.** Seven rows carry an original-author credit
ahead of the digitiser, semicolon-separated: Libris, TeX Gyre Schola,
Coelacanth, Goudy Bookletter, Libre Franklin, Edgar and Libre Caslon Text.
(Venetian 301 already carried its original author alone and is unchanged.) Two
more rows changed under the same-day owner rulings above without adding a
second stage: Edgar's 1722 credit is now `William Caslon & Alexander
Phemister` (was `William Caslon` alone) and Newsreader's single-stage credit is
now `Hugues Gentile & Jean-Baptiste Levée` (was `Hugues Gentile` alone).
Re-sync `kEntries` in `src/FontDisplayNames.h` from this table before the
two-line subtitle ships — that is a separate code step, deliberately not done
in this pass.

Otherwise synced. The header stores one `lineage` string per family, copied verbatim from
the Lineage column here, plus a numeric `earliestYear` for the picker's
reverse-chronological sort. Device subtitle: `Designer · lineage`, wrapped by
the theme over two lines and ellipsized only past that.

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
- **Rosarivo 2012 / Almendra 2011-12** Google Fonts dates — release lag.
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
