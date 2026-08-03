# Font dates — source data for `src/FontDisplayNames.h`

Editable source of truth for the Text Settings picker's designer/years subtitles
and the picker's ordering. Edit this table, then update `kEntries` in
`src/FontDisplayNames.h` to match.

## Rules

- **Years format**: `creation; digital…` — the historical/design year before the
  semicolon, digital releases after it, ascending. A born-digital face lists its
  years plainly.
- **Adjacent-year collapse**: a design year and its store release one year apart
  (2011 design, 2012 Google Fonts) is release lag, not history — keep only the
  design year.
- **No unreliable dates**: a year that can't be pinned to a source stays out.
- **Places**: city + country where the creation-era design was made, and where
  the digital version was made. Country only when no city is pinnable.
- **Place column format**: `creation city, state, country; digital city, state,
  country` — mirrors the Years `creation; digital` split. A born-digital face
  has one place, no semicolon. State only where the country has them.
- **Picker order** = this table's order: reverse chronological by EARLIEST
  (creation) year — newest lineage first, undated families last, ties
  alphabetical by display name.

## Tiers

Every family carries an explicit tier. The column used to be blank for all but
two rows, which made "demote everything below S" ambiguous — blank was doing
double duty as both "unranked" and "the curated baseline". It no longer is.

- **S** — the reading faces. Coelacanth and Edgar for serif work; Lexica
  Ultralegible, Archivo, Libre Franklin and Host Grotesk for sans. New work,
  previews and screenshots use Edgar or Coelacanth unless a sans is called for.
- **A** — previously S, kept installed and fully supported.
- **B** — installed and buildable, not the default choice for new work.

Tier is editorial and lives only here. It does not reach the device: the picker
sorts reverse-chronologically by earliest year (see below), and
`FontDisplayNames.h` stores no tier field. Changing a tier changes nothing a
reader sees.

## Families

| Tier | Directory | Display name | Designer | Years | Place | Creation place | Digital place | Basis |
|---|---|---|---|---|---|---|---|---|
| S | HostGrotesk | Host Grotesk | Doğukan Karapınar & İbrahim Kaçtıoğlu | 2023 | — | — (born digital) | Not disclosed by the foundry | Copyright 2023 in the shipped font's name table; designers and foundry from the same table and [METADATA.pb](https://raw.githubusercontent.com/google/fonts/main/ofl/hostgrotesk/METADATA.pb); [Element Type](https://elementtype.co/host-grotesk/). Google Fonts listing 2024-10-03 collapsed as release lag. Element Type publishes no location, so the place column stays empty rather than guessing from the designers' names |
| B | SourceSerif4 | Source Serif 4 | Frank Grießhammer | 2014, 2021 | Santa Clara, California | — (born digital) | Santa Clara / San Jose, California, USA (Adobe Type) | Pro 1.0 May 2014 ([Typekit blog](https://blog.typekit.com/2014/05/20/source-serif-pro/)); v4 2021-01-25 ([Wikipedia](https://en.wikipedia.org/wiki/Source_Serif)); Grießhammer in Santa Clara, Adobe Type at San Jose HQ ([Typekit interview](https://blog.typekit.com/2013/12/09/interview-frank-griesshammer/)) |
| S | Archivo | Archivo | Héctor Gatti | 2012, 2020 | Buenos Aires | — (born digital) | Buenos Aires, Argentina (Omnibus-Type) | Design 2012 ([Wikipedia](https://en.wikipedia.org/wiki/Archivo), [Omnibus-Type](https://www.omnibus-type.com/fonts/archivo/)); 2020 = the variable rework, per the shipped font's copyright string and [repo](https://github.com/Omnibus-Type/Archivo); designer from the font's name table (ID 9). Google Fonts listing 2016-12-03 is neither a design nor a release date and is excluded |
| B | GTAlpinaCond | GT Alpina | Reto Moser | 2011, 2020 | Bern; Lucerne, Switzerland | Bern, Switzerland (Moser lives/works in Bern) | Lucerne, Switzerland (Grilli Type foundry) | Design start 2011 ([Fonts In Use](https://fontsinuse.com/typefaces/102321/gt-alpina)); release 2020 ([It's Nice That](https://www.itsnicethat.com/articles/grilli-type-gt-alpina-graphic-design-030220)); places ([Grilli Type](https://www.grillitype.com/about)) |
| B | Lora | Lora | Olga Karpushina | 2011, 2019 | Moscow | — (born digital) | Moscow, Russia (Karpushina; Cyreal foundry, Russia) | First release 2011 ([GitHub](https://github.com/cyrealtype/Lora-Cyrillic)); variable 2019 ([Cyreal](https://www.cyreal.org/fonts/lora/)); [Behance](https://www.behance.net/OlgaKarpushina) |
| A | CaledoniaCC | Caledonia CC | W.A. Dwiggins | 1938; 1988, 1994, 2026 | Hingham, Mass.; Cambridge, Mass. | Hingham, Massachusetts, USA (Dwiggins' studio; for Mergenthaler Linotype, Brooklyn NY) | Cambridge, Massachusetts, USA (Carter & Cone: 1994 Time Caledonia, 2026 CC). 1988 New Caledonia: David Berlow, Adobe (USA) / Linotype AG (Germany) | 1938 design ([Wikipedia](https://en.wikipedia.org/wiki/Caledonia_(typeface))); 1988 = New Caledonia PostScript ([Wikipedia](https://en.wikipedia.org/wiki/Caledonia_(typeface))); 1994 = Time Caledonia by Matthew Carter; CC release April 2026 ([Carter & Cone](https://carterandcone.com/font/caledonia/), [Creative Boom](https://www.creativeboom.com/resources/the-best-new-typefaces-for-april-2026/)); Dwiggins in Hingham 1906–56 ([Britannica](https://www.britannica.com/biography/W-A-Dwiggins)) |
| A | TeXGyreSchola | TeX Gyre Schola | Bogusław Jackowski & Janusz M. Nowacki | 1918; 2007 | Jersey City; Gdańsk | Jersey City, New Jersey, USA (model: Century Schoolbook, Morris Fuller Benton for American Type Founders, roman 1918) | Gdańsk / Grudziądz, Poland (GUST e-foundry, seated in Toruń; via URW Century Schoolbook L) | First stable v0.996 2007-01-14, final v2.005 2009 ([CTAN README](http://ftp.math.utah.edu/pub/tex/historic/fonts/tex-gyre/schola/0.996/README-TeX-Gyre-Schola.txt), [GUST](https://www.gust.org.pl/projects/e-foundry/tex-gyre/schola)); Benton/ATF ([Wikipedia](https://en.wikipedia.org/wiki/Century_type_family), [ATF](https://en.wikipedia.org/wiki/American_Type_Founders)); designers ([Typoteka: Jackowski](https://typoteka.pl/en/designer/boguslaw-jackowski), [Nowacki](https://typoteka.pl/en/designer/janusz-marian-nowacki)) |
| S | Coelacanth | Coelacanth | Ben Whitmore | 1914, 2014 | New York; Waiheke Island, New Zealand | New York, USA (Centaur: Bruce Rogers for the Metropolitan Museum of Art; matrices cut in Chicago) | Waiheke Island (Auckland), New Zealand | Centaur 1914 ([Wikipedia](https://en.wikipedia.org/wiki/Centaur_(typeface))); release 2014-08-29 ([Font Library](https://fontlibrary.org/en/font/coelacanth)); "Ben Whitmore from Waiheke, New Zealand" ([ben-whitmore.com](http://ben-whitmore.com/coelacanth-type-family/)) |
| B | Venetian301 | Venetian 301 | Bruce Rogers | 1914, 1990 | New York; Cambridge, Mass. | New York, USA (Centaur: Rogers for the Metropolitan Museum of Art) | Cambridge, Massachusetts, USA (Bitstream's city at the time; later Marlborough MA) | Centaur 1914 ([Wikipedia](https://en.wikipedia.org/wiki/Centaur_(typeface))); Bitstream © 1990 ([Bitstream key](http://www.sanskritweb.net/forgers/bitstream2.pdf)); Cambridge-era Bitstream ([Wikipedia](https://en.wikipedia.org/wiki/Bitstream_Inc.)) |
| B | GoudyBookletter1911 | Goudy Bookletter | Barry Schwartz | 1911; 2009 | New York; St. Paul | New York City, USA (Goudy's Village Press era; Kennerley Old Style for publisher Mitchell Kennerley) | St. Paul, Minnesota, USA (Schwartz) | Kennerley 1911 ([Wikipedia](https://en.wikipedia.org/wiki/Kennerley_Old_Style)); League of Moveable Type ~2009 ([League](https://www.theleagueofmoveabletype.com/goudy-bookletter-1911)); Schwartz in St. Paul ([Luc Devroye](https://luc.devroye.org/fonts-46940.html), single source) |
| S | LibreFranklin | Libre Franklin | Pablo Impallari, Rodrigo Fuenzalida & Nhung Nguyen | 1902; 2016 | Jersey City; Rosario, Argentina | Jersey City, New Jersey, USA (model: Franklin Gothic, Morris Fuller Benton for American Type Founders, 1902 — note ATF's Jersey City plant opened 1903, so the design marginally predates the move) | Rosario, Argentina (Impallari Type) | Franklin Gothic 1902 ([Wikipedia](https://en.wikipedia.org/wiki/Franklin_Gothic)); Libre Franklin added to Google Fonts 2016-06-20 ([METADATA.pb](https://raw.githubusercontent.com/google/fonts/main/ofl/librefranklin/METADATA.pb)) and preinstalled on Axis-Praxis 2016-11 ([Axis-Praxis](https://www.axis-praxis.org/blog/2016-11-11/3/new-font-libre-franklin-by-pablo-impallari-now-preinstalled)); three designers from the font's name table (ID 9) |
| S | LexicaUltralegible | Lexica Ultralegible | Applied Design Works & Jacob Perez | 1832; 2019, 2024 | London; New York; El Paso, Texas | London, England (model: the grotesque, named in Vincent Figgins' 1832 specimen) | New York, USA (Applied Design Works, NY/LA; for Braille Institute of America, Los Angeles); El Paso, Texas, USA (Jacob Perez) | Lineage starts 1832 London, owner ruling 2026-08-03: Figgins' 1832 specimen is where "sans serif" is first printed as the name of the style this face belongs to, and the tier's other entries are dated from their model rather than their release ([Wikipedia: sans-serif](https://en.wikipedia.org/wiki/Sans-serif), [Figgins](https://en.wikipedia.org/wiki/Vincent_Figgins)). Atkinson Hyperlegible released 2019, Applied Design Works with Elliott Scott as lead designer and Craig Dobie as creative director ([Wikipedia](https://en.wikipedia.org/wiki/Atkinson_Hyperlegible), [Fast Company](https://www.fastcompany.com/90395836/this-typeface-hides-a-secret-in-plain-sight-and-thats-the-point)); studio in New York and Los Angeles, Braille Institute in Los Angeles ([Applied Design Works](https://helloapplied.com/braille-institute-of-america/), [It's Nice That](https://www.itsnicethat.com/news/applied-design-braille-institute-atkinson-hyperlegible-graphic-design-050821)); Lexica v1.0.0 released 2024-10-06 ([GitHub release](https://github.com/jacobxperez/lexica-ultralegible/releases)); Perez in El Paso ([GitHub profile](https://github.com/jacobxperez)) |
| B | Newsreader | Newsreader | Hugues Gentile | 1757; 2020 | Birmingham; Paris | Birmingham, England (model: John Baskerville's types, 1757) | Paris, France (Production Type) | Google Fonts 2020-07-01 ([METADATA.pb](https://raw.githubusercontent.com/google/fonts/main/ofl/newsreader/METADATA.pb)); [Production Type](https://productiontype.com/font/newsreader) |
| S | Edgar | Edgar | Tobias Frere-Jones & Nina Stössinger | 1722; 2025 | London; Brooklyn | London, England (model: William Caslon's types, 1722) | Brooklyn, New York, USA (Frere-Jones Type, 126 13th St) | Release 2025-10-01 ([Typecache](https://typecache.com/news/6518/)); studio ([frerejones.com](https://frerejones.com/)) |
| B | LibreCaslonText | Libre Caslon Text | Pablo Impallari & Rodrigo Fuenzalida | 1722, 2012 | London; Rosario, Argentina | London, England (William Caslon's foundry) | Rosario, Argentina (Impallari); Fuenzalida: Venezuela, city uncertain (Caracas-trained) | Initial release 2012-11-08 ([FONTLOG](https://github.com/impallari/Libre-Caslon-Text/blob/master/FONTLOG.txt)); [Caslon](https://en.wikipedia.org/wiki/William_Caslon); [Impallari Behance](https://www.behance.net/impallari) |
| B | Junicode | Junicode SemiCond | Peter S. Baker | 1703; 1998, 2023 | Oxford; Charlottesville, Virginia | Oxford, England (Oxford University Press — the "Pica Roman" purchased 1692, printed in Hickes' Thesaurus 1703–05) | Charlottesville, Virginia, USA (Baker, University of Virginia — both 1998 and 2023 eras) | [Junicode design history](https://junicode.sourceforge.io/design.html); Junicode 2 2023-08-18 ([Wikipedia](https://en.wikipedia.org/wiki/Junicode)); [Baker at UVA](https://english.as.virginia.edu/people/peter-baker). Note: Baker says the model "looks more like" the 1692 Pica Roman than a Fell/Walpergen type |
| B | Rosarivo | Rosarivo | Pablo Ugerman | 1470; 2011 | Venice; Buenos Aires | Venice, Italy (model: Nicolas Jenson's roman, 1470) | Buenos Aires, Argentina (Ugerman, UBA postgrad program) | [Font Squirrel](https://www.fontsquirrel.com/fonts/rosarivo) |
| B | InknutAntiqua62 | Inknut Antiqua | Claus Eggers Sørensen | 1469; 2014 | Venice; Amsterdam | Venice, Italy (model: Johannes de Spira's roman, 1469) | Amsterdam, Netherlands (Sørensen, Danish, based in Amsterdam) | [GitHub](https://github.com/clauseggers/Inknut-Antiqua); [FontsArena](https://fontsarena.com/inknut-antiqua-by-claus-eggers-sorensen/) |
| B | Almendra | Almendra | Ana Sanfelippo | 1350; 2011 | London; Buenos Aires | London, England (model: English chancery hand, ~1350) | Buenos Aires, Argentina (Sanfelippo, CDT UBA postgrad program) | [Font Squirrel](https://www.fontsquirrel.com/fonts/almendra); [METADATA.pb](https://raw.githubusercontent.com/google/fonts/main/ofl/almendra/METADATA.pb) |

## Sync status vs `FontDisplayNames.h`

Synced. The header stores `years` and `place` as pre-formatted strings copied
verbatim from the Years and Place columns here, plus a numeric `newestYear` for
the picker's reverse-chronological sort. Device subtitle:
`Designer · years · place`, ellipsized by the theme when a row overflows.

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
- **Caslon IV's 1816 sans (the first one cut)** — the earlier date, and the one
  a purist would pick for the style's origin. Passed over for 1832 because the
  other model-dated entries here take the year the model was *published as the
  thing this face descends from*, and 1832 is where Figgins prints the name.
  Owner ruling 2026-08-03; noted so the 16-year gap reads as a decision rather
  than an oversight.
