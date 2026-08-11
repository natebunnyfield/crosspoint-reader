# SD Card Fonts

CrossPoint supports loading additional fonts from the SD card, including fonts
with extended Unicode coverage (CJK, Cyrillic, Greek, etc.).

## S tier (fork ruling, 2026-08-02; reduced to four 2026-08-07)

The installed set on this fork is exactly four families — **Edgar, Coelacanth,
TeXGyreSchola, LibreFranklin** — on every surface: both device SD cards, the
simulator's `fs_/fonts/`, and the iOS app's bundled seed set
(`crosspoint-simulator/ios/seedfonts/`). The other curated families remain
fully buildable recipes in `sd-fonts.yaml` (picker labels stay in
`src/FontDisplayNames.h`), but they are not installed anywhere. When adding a
surface or reprovisioning a card, install these four and nothing else.

One of the four is a sans: **Libre Franklin**, the text grotesque, from
`lib/EpdFont/scripts/grotesque-candidates.yaml`. The humanist cell is empty
since Quattrocento Sans went to A tier.

**Archivo and Host Grotesk were cut on 2026-08-04**, promoting Libre Franklin to
the single text grotesque. The bench they came off answered "which grotesque",
and shipping all three of its finalists turned one answer into three: they
occupy the same cell of the taxonomy — 19th-century grotesque for continuous
text — so a picker offering all three spends the reader's attention on a
distinction the page does not make. Archivo carried the set's only
`force_autohint: true` and an 11 px x-height at slot 0 against the tier's 12
(see `tools/grotesque-bench`), so it was the weakest of the three on its own
terms as well. Both remain buildable recipes and keep their picker labels, since
a card provisioned before this ruling still carries them.

**Lexica Ultralegible was cut on 2026-08-04**, the same day as the other two
grotesques, leaving Libre Franklin the only installed sans until Quattrocento
Sans joined it later that day. It had been added on
2026-08-03 after winning a bench of 13 sans-serif candidates rendered through
the real `GfxRenderer` at matched x-height
(`lib/EpdFont/scripts/sans-candidates.yaml`, `render_harness reading`), on the
strength of Atkinson Hyperlegible's letterform-disambiguation design with the
coverage gap closed — 100% of Latin-1 and Latin Extended-A against Atkinson's
own static TTFs at 98% / 73%, which matters because `build-sd-fonts.py` fills
every uncovered codepoint from Noto, so a thin face renders two textures inside
one word. Nothing about that verdict changed; the cut is the same taxonomy
argument applied once more — one installed sans is the answer to "which sans",
and the humanist/accessibility bench and the grotesque bench were both asking it.

The demotion is to **buildable-only**: its recipe stays in `sd-fonts.yaml` and
its picker label stays in `src/FontDisplayNames.h` (cards provisioned before
2026-08-04 still carry the family and must still label it), but its `.cpfont`
files were deleted from all four surfaces on 2026-08-04. Putting
`LexicaUltralegible` back into `installed_families:` — or back onto a surface —
is a regression, not a restoration; it needs a fresh ruling first.

### The humanist sans: Quattrocento Sans in, Freight Sans out (2026-08-04)

Both were built on 2026-08-04, both are humanist sans, and both therefore fill
one cell of the taxonomy — the same "three answers to one question" that cut
Archivo and Host Grotesk that morning. The tier takes one.

**Quattrocento Sans won the humanist-sans cell here, and went to A tier on
2026-08-07 — the section below supersedes this one on where it is installed. The
comparison it won still stands; it is simply no longer on any surface.** Pablo
Impallari
and Igino Marini's humanist companion to the Quattrocento serif, OFL, all four
styles real. It won on everything outside the page:

- **Rebuildable anywhere.** Four Google Fonts URLs, no local files. Freight Sans
  builds only from gitignored commercial sources, which means no CI machine and
  no other contributor can regenerate it.
- **cmap audits clean.** `tools/font-cmap-audit.py` finds only
  comma/quotesinglbase and Eth/Dcroat sharing outlines, both normal practice.
  (`ã õ` look flat-tilde at small ppem but are real tildes drawn as simple
  outlines — `ā`/`ō` are not in the font at all.) Freight Sans needed eight
  `drop_codepoints` to stop `¾` rendering as `ffl`.
- **Kerning and ligatures.** 10,780 kern cells, the richest in the set against
  Edgar's 9,546 — the 2012 v2 was iKerned — plus real `fi`/`fl`. The Freight
  Sans cut ships no `GSUB` at all and 4,284 cells.
- **Provenance.** OFL from Google Fonts, against a fontsgeek.com download of a
  face whose own name table carries the GarageFonts / Phil's Fonts EULA.

Freight Sans wins on slot fit, and that is the one thing it wins: it hits
x-height 12/14/16/18 px AND advanceY 34/39/45/51 exactly, the only family in
`sd-fonts.yaml` needing no per-slot caveat. Quattrocento Sans is x-height-exact
and a pixel off on leading — **12/14/17/19 pt → x-height 12/14/16/18 px,
advanceY 33/38/46/51**. That is the floor, not a first attempt: `advanceY`
depends only on the span, and sweeping every span from the 1162 ink floor upward
bottoms out at three slots ±1 for this size set. The cause is the ramp — the
hinted x-height plateaus at 15 px across both 15 and 16 pt, so 16 px costs
17 pt, and no single span serves 17 and 19 pt at the tier's spacing at once.
x-height exactness wins, the same call Almendra's recipe records. A pixel of
leading did not outweigh the four rows above.

`force_autohint` was tried on Quattrocento Sans and rejected: it moves slot 2 to
16 pt and yields 33/39/44/52 — also three slots ±1, the misses merely shuffled,
with equal-or-smaller counters at matched slots. Archivo's remains the set's
only `force_autohint`, and it is there for a defect this family does not have.

Both carry the same shape of coverage gap, and it is a property of the available
cuts rather than either design: Latin-1 is 96% (Freight) / 99–100%
(Quattrocento), but Latin Extended-A is 8% / 9%, so Œœ Šš Žž Ÿ are in the faces
and the Central European set comes from the Noto fallback. English and Western
European text stays in one texture; Polish or Czech will not.

**Freight Sans is C tier.** Its `.cpfont` files were deleted from every surface
on 2026-08-04. The recipe stays in `sd-fonts.yaml` so the comparison can be
re-run, and the picker label stays in `src/FontDisplayNames.h` because a card
provisioned before the ruling still carries the family — the same treatment
Archivo, Host Grotesk and Lexica Ultralegible got. Its commercial sources stay
in gitignored `lib/EpdFont/local_fonts/`, never committed or distributed.
Putting it back into `installed_families:` — or back onto a surface — is a
regression, not a restoration; it needs a fresh ruling.

The 2x hi-res companions (`<Family>/2x/<same 1x filename>`, built at doubled
point sizes for `CROSSPOINT_RENDER_SCALE=2`) are part of the set: every
installed family carries one, and `install-sim-fonts.py` cannot regenerate them
— it only prunes orphans. Build them by hand with the doubled `sizes:` and
rename the output back to the 1x filenames, which is what
`SdCardFontManager::hiResCompanionPath` looks up.

**A companion is matched per POINT SIZE, so a set can be complete for one size
slot and missing for the next.** The lookup is by exact filename and the
filename carries the size, so changing the reader's size in Text Settings can
move it onto a slot with no 2x file — the page then renders 1x-replicated at
half the resolution the build asked for. That used to happen in silence; since
2026-08-04 it logs

    INF SDMGR: No hi-res companion /fonts/<Family>/2x/<Family>_<pt>.cpfont - <pt> pt renders 1x-replicated

once per load. To audit a card without running anything, compare each family's
1x filenames against its `2x/` directory — any name present in one and not the
other is a size slot that will render 1x.

This applies to the READER font only. UI chrome takes a different route: the
chrome ids (`SMALL_FONT_ID`, `UI_10_FONT_ID`, `UI_12_FONT_ID`) are built-ins,
and since the selectable System font landed they get their hi-res halves from
`registerHiResBuiltinFont` in `applySystemFont` rather than from the card, so
no `2x/` directory is involved and nothing here can degrade them. Two details
of that path are worth knowing because both are load-bearing: built-ins are
registered in `hiResFontMap_` but deliberately NOT in `hiResSdFonts_` (which
`FontCacheManager` dereferences to prewarm glyphs off the card, and a built-in
has nothing to prewarm), and unloading SD fonts calls `clearSdCardHiResFonts`,
which erases only the ids it finds in `hiResSdFonts_`. That is what keeps a
reader-font reload — every font-size change is one — from taking the UI's
hi-res registrations down with it.

The ruling is enforced, not just written down. `installed_families:` at the top
of `lib/EpdFont/scripts/sd-fonts.yaml` is the single source of truth, and
`scripts/install-sim-fonts.py` defaults to it. That default used to be "every
curated family the recipes can build"; once all 15 became buildable
(2026-08-01) a routine re-run silently reinstalled the eleven this ruling
excludes, which is exactly the drift the list now prevents. Use
`--all-curated` to opt back into the old behavior; it prints a parity warning.

The iOS app needs no equivalent list. `CrossPointFsPrep.cpp::seedOneFontDirectory`
seeds whatever `crosspoint-simulator/ios/seedfonts/` contains and prunes files
the bundle no longer carries, so that directory is its own source of truth —
keep it holding exactly these six.

## A tier (fork ruling, 2026-08-07)

**Rosarivo and Quattrocento Sans.** Removed from every surface — both device SD
cards, `fs_/fonts/`, and the iOS seed bundle — and taken out of
`installed_families:`. Recipes stay buildable and picker labels stay in
`src/FontDisplayNames.h`, the same treatment every cut family gets, because a
card provisioned before this ruling still carries them.

A tier is not C tier. C means a comparison was run and the family lost it —
Freight Sans lost the humanist-sans cell to Quattrocento Sans on evidence, and
re-adding it needs a fresh ruling because the evidence still says no. A means
the family is good and simply is not installed: the set is being kept small, and
these are the first candidates if a slot opens. Nothing was found wrong with
either face.

Consequences worth knowing before reprovisioning:

- **The humanist-sans cell is now empty.** Libre Franklin, a text grotesque, is
  the only installed sans. Quattrocento Sans was promoted into that cell on
  2026-08-04 and is the obvious way to fill it again.
- **A selected family that is no longer installed does not fall back to another
  SD family.** `getReaderFontId()` falls back to the built-in Libre Franklin, which
  is a visible change of face, so repoint `SETTINGS.sdFontFamilyName` when
  removing a family a surface is actually using. Both were in use when this
  ruling landed: the simulator was on Quattrocento Sans and the X4 card on
  Rosarivo. They were repointed to Libre Franklin and Coelacanth respectively —
  nearest surviving family of the same class.

## Installing Fonts

There are two ways to install fonts, and both put the files there from off the
device.

An on-device "Manage Fonts" downloader used to be a third. Its screen was
removed on 2026-08-08 (nothing launched it, so it was unreachable), and the rest
of it — the `fonts.json` manifest, its generator, and the "stable" release tag
devices fetched from — on 2026-08-10, on the owner's ruling that fonts cannot be
installed completely by downloading them on the device. Do not rebuild it
without solving that first. SD firmware updates remain the on-device update path
for firmware itself.

### Option 1: Upload via web browser

1. Start **File Transfer** and connect through **Join Network** or **Create Hotspot**
2. Open the web interface URL shown on the reader
3. Navigate to the **Fonts** tab
4. Upload `.cpfont` files using the upload form

### Option 2: Manual SD card copy

1. Download font files from the
   [crosspoint-fonts repository](https://github.com/crosspoint-reader/crosspoint-fonts)
2. Copy font family folders to one of two locations on your SD card:

   - `/.fonts/` — hidden directory (preferred; keeps the SD root tidy
     when mounted on a desktop)
   - `/fonts/` — visible directory (use this if your OS hides dot-files
     and you'd rather see the folder in your file manager)

   Both roots are always scanned at boot and the results are merged: a
   family installed in `/fonts/` shows up even when `/.fonts/` also
   exists, and vice versa. The two roots only collide if the same family
   name appears in both — in that case the copy in `/.fonts/` wins and
   the duplicate in `/fonts/` is ignored.

       SD Card Root/
       ├── .fonts/                     ← Hidden root (preferred)
       │   └── Literata/
       │       ├── Literata_12.cpfont
       │       ├── Literata_14.cpfont
       │       ├── Literata_16.cpfont
       │       └── Literata_18.cpfont
       └── fonts/                      ← Visible root (equally valid)
           └── Merriweather/
               ├── Merriweather_12.cpfont
               └── ...

3. Insert the SD card and power on your CrossPoint reader

## CJK in the User Interface

The built-in UI fonts are Latin-only, so by default the interface (book titles
in the library, file names in the browser, list rows, headers) shows
replacement boxes for Chinese/Japanese/Korean text even when book *content*
renders correctly with a selected SD-card font.

To avoid shipping a large CJK glyph set in flash, CrossPoint instead reuses the
SD-card font you already selected: when a UI string contains a CJK character
the built-in font cannot draw, that whole string is rendered with your selected
SD-card font instead.

The fallback is **size-matched**. The built-in UI fonts render at 8 pt
(small/author lines), 10 pt (list rows) and 12 pt (book-cover titles, headers),
so CrossPoint loads your SD family at those sizes too and maps each UI font to
its same-size SD font. CJK book names therefore appear at the same size as the
Latin text around them. For this to work the family must contain `.cpfont`
files at sizes **8, 10 and 12** (in addition to the reader sizes 12–18); any UI
size missing from the family simply keeps showing boxes for CJK at that size.

Note that **Settings > Reader > Font Size** lists every size the family ships,
so a family built at 8,10,12,14,16,18 offers all six as reading sizes — the UI
sizes are not hidden from the list. Reading at 8 pt is your call; if you would
rather not see the small sizes there, convert two families (one with the UI
sizes for fallback, one with only the reading sizes you want).

When converting your own font, include the UI sizes:

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
      MyCJKFont-Regular.otf \
      --intervals cjk \
      --sizes 8,10,12,14,16,18 \
      --style regular \
      --name MyCJKFont \
      --output-dir ./MyCJKFont/

What this means in practice:

- Select a CJK-capable SD font under **Settings > Reader > Font Family**
  (see [Installing Fonts](#installing-fonts) and the `cjk` / `hangul` presets
  under [Converting Custom Fonts](#converting-custom-fonts)). That single
  selection drives both book content *and* size-matched CJK fallback in the UI.
- Pure-Latin UI strings keep the crisp built-in font; only strings that
  actually contain CJK are routed to the SD font.
- The fallback is per *string*, not per glyph: a mixed title such as
  `三体 Vol.1` renders entirely in the SD font (including the Latin part). If
  that SD font is a `Mono` family, the Latin portion will appear half/full
  width.
- If no SD font is selected (a built-in reading font is active), there is no
  CJK fallback and the UI again shows boxes for CJK — pick a CJK SD font to
  restore it.

## Available Pre-Built Fonts

The current list of pre-built fonts is maintained in the
[crosspoint-fonts repository](https://github.com/crosspoint-reader/crosspoint-fonts).

## What the catalog covers

[type-coverage.html](type-coverage.html) sorts all 37 curated families by what
the letterforms actually are rather than by the four headings `sd-fonts.yaml`
files them under, which are a filing system and not a classification. The 37
land in 13 of 22 structural classes; the nine empty ones are listed with a
judgment on each, because a gap only matters if the class survives a 1-bit
panel at 12-18 pt — a Didone is defined by a hairline, and a hairline here is
one pixel or none.

Two things worth knowing from it. **Alegreya is filed under Sans-serif and is a
serif** (its own `description:` says "calligraphic serif/display"), so any count
of the sans taken from the headings is off by one. And **the S tier is better
spread than the catalog**: its five occupy five different classes, while 19 of
the catalog's 25 serifs sit in two. Cutting Archivo and Host Grotesk widened
that spread rather than narrowing it, since all three grotesques filled one cell.
Cutting Lexica Ultralegible is the one cut that did cost a class — humanist sans,
and the low-vision/hyperlegible cell with it.

## Converting Custom Fonts

To convert your own TrueType/OpenType fonts:

### Prerequisites

    pip install freetype-py fonttools

### Single font (one style)

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
      MyFont-Regular.ttf \
      --intervals latin-ext \
      --sizes 12,14,16,18 \
      --style regular \
      --name MyFont \
      --output-dir ./MyFont/

### Multi-style font

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
      --regular MyFont-Regular.ttf \
      --bold MyFont-Bold.ttf \
      --italic MyFont-Italic.ttf \
      --bolditalic MyFont-BoldItalic.ttf \
      --intervals latin-ext \
      --sizes 12,14,16,18 \
      --name MyFont \
      --output-dir ./MyFont/

### Available Unicode interval presets

| Preset | Coverage |
|--------|----------|
| `ascii` | U+0020–U+007E (Basic Latin) |
| `latin1` | U+0080–U+00FF (Latin-1 Supplement) |
| `latin-ext` | European languages (Latin + Extended-A/B + punctuation + ligatures) |
| `greek` | Greek + Extended Greek |
| `cyrillic` | Cyrillic + Supplement |
| `hebrew` | Hebrew + Alphabetic Presentation Forms |
| `georgian` | Georgian + Georgian Supplement |
| `armenian` | Armenian |
| `ethiopic` | Ethiopic + Extended |
| `vietnamese` | Vietnamese subset (ơ/ư and combining marks) |
| `punctuation` | General punctuation (U+2000–U+206F) |
| `cjk` | CJK Unified Ideographs + Hiragana + Katakana + Fullwidth |
| `hangul` | Korean Hangul syllables + Jamo + Compatibility Jamo |
| `cherokee` | Cherokee (historic + supplement block) |
| `tifinagh` | Tifinagh |
| `symbols` | Math, currency, arrows, box-drawing, misc symbols, dingbats |
| `reading` | Literary fiction coverage: Latin, Greek, Cyrillic, math/symbol blocks, supplemental punctuation, and CJK quote marks |
| `builtin` | Matches the firmware's built-in font conversion intervals |

Combine presets with commas: `--intervals latin-ext,greek,cyrillic`

You can also specify arbitrary Unicode ranges directly:
`--intervals latin-ext,(0x2100-0x214F)`

To list all presets with codepoint counts:

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py --list-presets

### Additional options

`--force-autohint` — force FreeType's auto-hinter instead of the font's native hinting (useful when a font's built-in hints produce poor results at small sizes).

Install custom fonts via the web interface or manual SD card copy.
