# SD Card Fonts

CrossPoint supports loading additional fonts from the SD card, including fonts
with extended Unicode coverage (CJK, Cyrillic, Greek, etc.).

## S tier (fork ruling, 2026-08-02; reduced to four 2026-08-07; LibrisADF added 2026-08-12; InknutJunicode added 2026-08-13; TeXGyreHeros added 2026-08-23; Almendra added 2026-08-24)

The installed set on this fork is exactly eight families — **Edgar, Coelacanth,
InknutJunicode, TeXGyreSchola, LibreFranklin, LibrisADF, TeXGyreHeros,
Almendra** — on every surface:
device SD cards, the simulator's `fs_/fonts/`, and the iOS app's bundled seed
set (`crosspoint-simulator/ios/seedfonts/`). The authoritative list is
`installed_families:` in `lib/EpdFont/scripts/sd-fonts.yaml` — when this prose
and that list disagree, the yaml wins. The other curated families remain fully
buildable recipes in `sd-fonts.yaml` (picker labels stay in
`src/FontDisplayNames.h`), but they are not installed anywhere. When adding a
surface or reprovisioning a card, install these eight and nothing else.

**InknutJunicode joined 2026-08-13** (owner ruling: "add inknutjunicode fully
to builds", `6c5fefe0a`), after fourteen bench rounds settled its borrowed
italic — Junicode Expanded SemiBold at ENLA 14, x1.25, word space −0.09 em; the
first installed family whose roman and italic come from different typefaces.
Details in the `installed_families:` comment block in `sd-fonts.yaml`.

**Physical device SD cards are re-verified per provisioning, not per ruling.**
BUNNYFIELDS was reprovisioned and hash-verified with the then-six on
2026-08-15 and has NOT been reprovisioned since TeX Gyre Heros joined;
OWEN_BNF still carries the pre-08-07 set (last verified 2026-08-06) until
reprovisioned. Cards provisioned before a ruling carry the older set —
compare against `fs_/fonts` by hash, and reprovision by hand before relying on
a newer family being present.

**Three of the eight are sans**, and each holds a different cell: **Libre
Franklin**, the 19th-century text grotesque, from
`lib/EpdFont/scripts/grotesque-candidates.yaml`; **Libris** (`LibrisADF`), a
calligraphic humanist sans reclassified out of the Serif section it was
originally promoted into (2026-08-12 owner ruling, see below); and **TeX Gyre
Heros** (2026-08-23 owner ruling, see below), a neo-grotesque — GUST's
Helvetica through URW's Nimbus Sans. The first two stay installed
permanently, not provisionally: **there is no head-to-head between them,
because a head-to-head only runs WITHIN one classification cell** — the
grotesque bench compared grotesques, the humanist/accessibility bench compared
humanist/accessibility faces, and neither ever pitted a grotesque against a
humanist sans. Libre Franklin is a 19th-century-grotesque revival; Libris is a
calligraphic humanist sans in Lydian's mold — different cells. The humanist
cell was empty since Quattrocento Sans went to A tier; Libris fills it, on the
same footing Libre Franklin holds in the grotesque cell. Owner ruling
2026-08-12; no bench is pending or expected.

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

### TeXGyreHeros is S tier (owner ruling, 2026-08-23)

"add TeX Gyre Heros fully to app as s tier candidate. include meta info."
Installed on all three surfaces the same day, as a third sans in a third
classification cell.

**Why it qualifies.** It is a *neo-grotesque* — Helvetica's structure, reached
through URW's Nimbus Sans — which is neither Libre Franklin's 19th-century
grotesque nor Libris' calligraphic humanist sans. The cell rule that has
governed every sans decision on this fork since 2026-08-04 applies unchanged:
a head-to-head only runs WITHIN one cell, so Heros displaces nothing and owes
no bench. The Archivo / Host Grotesk / Lexica Ultralegible cuts were about
three candidates crowding ONE cell; this is the opposite case.

It also clears the two bars the tier has actually enforced in practice:

- **Rebuildable anywhere.** Four CTAN URLs under the GUST Font License, four
  real cuts (v2.004), nothing in gitignored `local_fonts/` — the same standing
  TeX Gyre Schola and Libris have, and the reason Freight Sans lost the
  humanist cell to Quattrocento Sans in the first place.
- **The uniform slots, measured off the built files.** x-height 12/14/16/18 px
  exact on all four slots; `advanceY` 35/39/45/51 against the tier's shipped
  Edgar/TeXGyreSchola cluster of 34/39/45/51 — three slots exact, slot 0 one
  pixel over. That is the same landing Archivo gets from the same four point
  sizes, and no span reaches 34 at 11 pt without dragging slots 1-3 down with
  it.

**`force_autohint: true`, and it is load-bearing.** Under the font's native
hinting the x-height ramp *skips 16 px* — 14 pt renders 15, 15 pt renders 17 —
so slot 2 could only be built a pixel small or a pixel large, and no metrics
override can fix that (a span sets leading, not glyph size). The auto-hinter
puts 14 pt exactly on 16 px. Unlike Archivo, which needed the auto-hinter to
reopen apertures that had sealed shut, this one costs nothing: measured
enclosed-pixel counts for `s`/`e`/`a` at all four sizes are equal or better
under the auto-hinter. It is here for the ramp alone. Archivo's is no longer
the set's only `force_autohint`.

**Metrics span 1542 (ascent 1026, descent −516), read back from built
`.cpfont` files rather than computed.** The `sd-fonts.yaml` header's `ceil()`
formula puts the 35/39/45/51 band at 1520-1527 and is wrong by 20 units,
because FreeType rounds `face.size.height` to whole pixels before `norm_ceil`
sees it. Sweeping spans and measuring what the converter writes puts the band
at 1540-1545. Same trap LibrisADF's recipe records.

**Coverage: Latin, and the description says so.** Measured on the shipped OTF:
Latin-1 223/224, Latin Extended-A 124/128, Latin Extended-B 51/208, Greek
54/144 (symbol-adjacent only), Cyrillic 0/256 — 1087 codepoints. The absent
Cyrillic is upstream policy: `qhv-hist.txt` records that the base-35 Cyrillic
was Valek Filippov's GPL addition, that GUST could not obtain permission to
relicense it, and "thus there are no Cyrillic glyphs in any of the TeX Gyre
fonts". That is measurably true of TeX Gyre Schola as well — identical 1087
codepoints — so the Greek and Cyrillic in *its* description have always come
from the fallback chain too. Left as-is; correcting Schola's description is a
separate change.

Lineage, citations and the picker entry: `docs/font-dates.md` and
`src/FontDisplayNames.h`. The one thing worth repeating here is that the
digitization year is **2009, not the 2006 in the font's copyright string** —
`qhv-hist.txt` calls v2.003 of 16.09.2009 "the first official release of the
TeX Gyre Heros fonts", which it had to be, since URW only released the base-35
originals under the LPPL on 2009-06-22.

### Almendra is S tier (owner ruling, 2026-08-24) — reversing the 2026-08-11 cut below

> keep almendra but add in accurate dates and authors

The eighth installed family, and the only one promoted out of a bundle-only
TRIAL rather than off a bench. The trial is the simulator repo's
`docs/trial-fonts.md`: five faces put on a TestFlight build on 2026-08-23
"without committing it to s tier", bundled through
`CROSSPOINT_IOS_TRIAL_FAMILIES` so that no surface but the phone carried them.
Arvo, Merriweather, IBM Plex Sans and Fira Sans Book were declined the same day
("lose arvo and the other candidate fonts"); Almendra was kept, so the trial
list is empty again and the gate is back to what it was.

**This reverses the C-tier ruling of 2026-08-11 below**, which is recorded
rather than quietly overwritten — that section says in as many words that
putting Almendra into `installed_families:` "is a regression, not a
restoration; it needs a fresh ruling". This is that ruling. Both sections stay:
the earlier one is why the face had a recipe to bundle at all.

**Its measured slots, read back from the built `.cpfont` files on 2026-08-24**
(not computed from the span — that column has been wrong before, see the
Merriweather caveat below): x-height **12/14/16/18, exact at all four**, which
only Edgar, TeX Gyre Schola, Libris and TeX Gyre Heros also manage; advanceY
**32/39/45/55** against the tier's 34/39/45/51, so slots 1 and 2 are exact,
slot 0 is 2 px tight and slot 3 is 4 px loose.

No `metrics:` span fixes that last one, and it was re-checked at promotion
because a trial's fit is allowed to be rough and an installed family's is not.
`metrics:` and `line_height_scale` are both FAMILY-WIDE multipliers: pulling 55
down to 51 is a 0.927 factor, which takes the two exact slots to 36 and 42 and
slot 0 to 30. The cause is the face's nonlinear x-height curve — 18 px needs
17 pt while 16 px needs only 14 pt — and no single span spans that jump. Three
slots right and one 4 px loose at the largest size is the better page.

Note the 2026-08-11 section below quotes advY 32/39/45/54 from the blind
bench's COMPUTED column. The built files measure 55, not 54; the direction of
that section's finding stands, its last digit does not.

**One thing is flagged and deliberately NOT changed**, because it costs bundle
megabytes and the ruling asked for a promotion rather than for those.
Almendra is the only one of the eight still on `intervals: latin-ext`; the
other seven are all on `reading`. `fallback_chain_for()` in
`build-sd-fonts.py` gives a Latin-only family its single committed fallback
face and no symbol/math/dingbat tail, so Almendra's `.cpfont` files carry no
U+2190–2193 arrows and no U+2212 MINUS — and it is now the only INSTALLED
family failing `test/sd_font_arrows` (`EveryInstalledFamilyDrawsRealArrowGlyphs`).
Coelacanth was raised to `reading` on 2026-08-20 over exactly this, when
U+2212 in a Claude reply drew tofu. Measured 2026-08-24, 1x raw bytes, same
four sizes and sources:

| `intervals:` | 1x raw | what it buys |
|---|---|---|
| `latin-ext` (today) | 965,412 | — |
| `latin-ext,symbols` | 2,910,561 | arrows, math operators, dingbats — 3.0x |
| `reading` (the tier baseline) | 4,111,496 | the whole tier layer — 4.3x |

At 4.3x the 1x+2x tree goes ~4.1 MB –> ~17.7 MB raw, which is mid-pack against
Edgar's 22.4 and Libris' 9.5 rather than bottom. The rebuild is ~6 s per tier
with sources and fallbacks cached; it is the WEIGHT that needs a ruling, not
the work.

### Almendra is C tier (owner ruling, 2026-08-11)

**SUPERSEDED 2026-08-24 — see the S-tier section above.** Left in place because
the reasoning is still the record of why the face was benched, and because the
paragraph below is the one the new ruling had to overturn by name.

Called out of `tools/blind-bench/` — a real-device-render blind read, same rig
as the sans and grotesque benches (`render_harness reading`, uniform 12/14/16/18
px x-height slots). Was never in `installed_families:`; the recipe stays in
`sd-fonts.yaml` and the picker label stays in `src/FontDisplayNames.h`, same
treatment as the other C-tier cuts, so nothing needs deleting from a surface it
was never on. Putting it into `installed_families:` is a regression, not a
restoration; it needs a fresh ruling.

For the record, not as the stated reason: of the bench's fourteen, Almendra
needed the widest metrics span (1520) and is the one slot-3 miss, advY
32/39/45/54 against target 34/40/46/51 — 3 px over at the largest size, where
every other candidate lands within 1 px.

### Merriweather, Accanthis and Spectral are C tier (owner ruling, 2026-08-12)

Cut from `tools/blind-bench/blind-candidates.yaml`'s keepers list the same day
LibrisADF (below) was promoted out of it to S tier — the bench's fourteen is
now eight. None of the three were ever in `installed_families:`, so nothing
needed deleting from a surface. Merriweather already had a recipe in
`sd-fonts.yaml` (predating the blind bench); Accanthis and Spectral got one
added alongside the cut, same treatment as every other C-tier family — the
recipe stays so the bench that ruled against them can be re-run, and the
picker label stays unassigned since none of the three ever shipped and never
had one.

For the record, not as the stated reason: against the uniform target
(xh 12/14/16/18 px, advY 34/40/46/51 px) the three each miss by 1 px somewhere
in the advY ramp — Merriweather 34/41/45/51, Accanthis 34/39/45/52, Spectral
33/41/47/50 — all within the tier's usual rounding tolerance, none a
Almendra-style 3 px miss.

Treat those three advY rows as indicative, not settled. They come from
`tools/blind-bench/blind_slots.json`, whose advY column is **computed from the
span rather than read back from a built `.cpfont`** — and the one row since
checked against real files was wrong: it predicted 34/40/46/51 for LibrisADF at
the bench's span, where the built files measure 34/39/45/51 (LibrisADF hits the
target exactly only at the different span it ended up shipping with, which the
bench never tried). Whether the same gap affects these three is unknown; nobody
has re-measured them either way.

### Antpolt is C tier (owner ruling, 2026-08-12)

Antykwa Półtawskiego, GUST's revival of the 1931 Polish national serif — same
Jackowski/Nowacki team as the installed TeX Gyre Schola. Cut on the same day
Libris ADF won `tools/blind-bench/` and was promoted to S: this face ran deep in
that bench, further than most of the field, and round 4's page names it among
the three that "didn't make round 3's cut", alongside Spectral and Merriweather
Light. Beaten, not defective — nothing was found wrong with it on the page.

It was never in `installed_families:`, so nothing needs deleting from a surface
it was never on. The recipe stays in `sd-fonts.yaml` so the comparison can be
re-run and the picker label stays in `src/FontDisplayNames.h` for attribution,
the same treatment every other C-tier cut gets. Adding it to
`installed_families:` — or to any surface — is a regression, not a restoration;
it needs a fresh ruling.

### LibrisADF is S tier (owner ruling, 2026-08-12)

Promoted out of `tools/blind-bench/blind-candidates.yaml` straight to S tier —
the only blind-bench candidate to skip A tier entirely, on the strength of the
cleanest ramp of the bench's fourteen: the family as shipped hits the uniform
target (xh 12/14/16/18 px, advY 34/40/46/51 px) exactly on all four slots. That
is measured off the built `.cpfont` files at the span it ships with (ascent
1050, descent -319), not off the bench's sweep — at the span the bench itself
tried, the built files measure advY 34/39/45/51. `sd-fonts.yaml`'s `LibrisADF`
block records how the shipping span was found.

Hirwen Harendal's **Libris**, from Arkandis Digital Foundry: a 2011 French
digitisation of Warren Chappell's 1938 Lydian, which the font's own name table
names as its model. GPL v2+ with font exception, sourced by URL from
salsa.debian.org, so like TeX Gyre Schola it needs nothing in gitignored
`local_fonts/` and rebuilds on any machine. All four styles are real cuts
(v1.007); coverage is printable ASCII + Latin-1 100% but Latin Extended-A only
11/128, the same shape of gap Quattrocento Sans and Freight Sans carry — Western
European text stays in one texture, Polish and Czech fall back to Noto. It
appears in the picker as "Libris", dropping the foundry suffix the frozen
directory name keeps. See `docs/font-dates.md` for the full lineage and
citations, and `src/FontDisplayNames.h` for the picker entry.

**Resolved, owner ruling 2026-08-12: Libris is a sans, and it moved.** It came
off `tools/blind-bench`, which is explicitly a blind *serif* bench, and was
first filed in `sd-fonts.yaml`'s Serif section on that basis — but the face
itself says otherwise: its own name table (ID 10) reads "Libris is a sans
serif font intented to mimic Lydian typeface" [sic], and rendering it beside
TeX Gyre Schola and Coelacanth shows flared calligraphic stem endings and no
serifs. `sd-fonts.yaml`'s `LibrisADF` block now lives in the Sans-serif
section, beside Libre Franklin.

This does NOT settle the "which sans" question the way the grotesque and
humanist/accessibility benches settled theirs — those benches each compared
candidates WITHIN one classification cell, and Libris never competed against
Libre Franklin at anything; it won a serif bench. **There is no head-to-head
between them and none is coming: a head-to-head only runs within a single
classification cell, and Libre Franklin (grotesque) and Libris (humanist) are
different cells** — owner ruling 2026-08-12. Both stay installed permanently
on that basis, the same way Libre Franklin already held the grotesque cell
alone. The S tier ships two sans against the "one installed sans" argument
written above for Archivo/Host Grotesk/Lexica Ultralegible — those cuts were
about redundant candidates in the SAME cell; this isn't that, so the argument
doesn't apply here.

Cap-I and lowercase-l are the same bare stem in Libris, so "Ill" renders as
three identical vertical bars with nothing to tell them apart. Noted and ruled
fine — owner call 2026-08-12: not a blocker, not worth acting on.

The 2x hi-res companions (`<Family>/2x/<same 1x filename>`, built at doubled
point sizes for `CROSSPOINT_RENDER_SCALE=2`) are part of the set: every
installed family carries one, and `install-sim-fonts.py` cannot regenerate them
— it only prunes orphans. Build them by hand with the doubled `sizes:` and
rename the output back to the 1x filenames, which is what
`SdCardFontManager::hiResCompanionPath` looks up.

**A companion is matched per POINT SIZE, so a set can be complete for one size
slot and missing for the next.** The lookup is by exact filename and the
filename carries the size, so changing the reader's size in Reader Font can
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

### A built-in whose glyph data is NOT in this repo (PragmataPro, 2026-08-14)

Every other family in `lib/EpdFont/builtinFonts/` ships its generated `.h`
committed — 96 of them. **PragmataPro is the one exception**, and the reason is
worth stating plainly: the CLAUDE.md rule for commercial faces is "the recipe is
committed, never the font files", and for an SD family that is the whole story,
because the `.cpfont` is built onto a card and never enters git. A *built-in*
inverts that. Its glyph tables are a committed C array, so following only the
existing rule — gitignore the TTF, commit the generated header — would put a
12 pt and 24 pt bitmap rasterisation of a commercial typeface into a public
repository. That is a redistributable derivative of the face, not a build
artifact of it.

So both halves stay out of git:

* `lib/EpdFont/local_fonts/PragmataPro-{Regular,Bold,Italic,BoldItalic}.ttf` —
  gitignored, supplied by whoever holds the licence.
* `lib/EpdFont/builtinFonts/pragmatapro_*.h` — gitignored too, which is the new
  part. Rebuild them with `lib/EpdFont/scripts/convert-builtin-fonts.sh`; that
  script skips the family with a clear message when the TTFs are absent.

`src/main.cpp` gates the row on `#if __has_include(<builtinFonts/pragmatapro_12_regular.h>)`,
so a clone without the licence compiles and runs unchanged — the row is simply
not registered, `editorfonts::resolve()` refuses to hand back an id the renderer
has no glyphs for, and the Editor Font picker degrades it to a built-in mono the
same way it degrades a card-only family. The headers are deliberately NOT added
to `builtinFonts/all.h`, which is committed and unconditional.

**Its four cuts are not equally covered, unlike every other editor face.**
Measured against `fontconvert.py`'s interval list (1665 printable codepoints):
Regular 950, Bold 234, Italic 234, BoldItalic 234 — for comparison Space Mono is
539 and IBM Plex Mono 732 across all four styles evenly. All four do cover
printable ASCII and Latin-1 100%, plus smart quotes, en/em dash, ellipsis and
bullet; what the three non-roman cuts lack is Latin Extended-A and beyond. So an
*emphasised* word containing `ż` or `ř` renders `?` where the roman renders the
letter — the same shape of gap Quattrocento Sans and Libris carry, but
style-dependent rather than family-wide. `EpdFont::getGlyph` substitutes U+FFFD
then `'?'`, so the pen still advances and the line does not slide. This is why
the row is writing-only and must not be given `alsoReading`.

That asymmetry is a property of *these* files (v0.8, 1699/264/264/262 glyphs),
not of the product: fsd.it describes current PragmataPro as ~18,000 glyphs in
the Regular and ~17,000 in each of the other three. Re-cutting from a current
licensed release would even the four styles out and is the right fix if the gap
ever matters.

Flash cost on device: **+66,086 bytes** (4,176,519 → 4,242,605, 63.7% → 64.7%),
measured by building `-e default` with the headers present and again with them
moved aside. The `_2x` companions are not in that number — they sit behind
`CROSSPOINT_RENDER_SCALE > 1` and reach only simulator/iOS binaries.

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
keep it holding exactly these eight.

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

[type-coverage.html](type-coverage.html) sorts all 38 curated families by what
the letterforms actually are rather than by the four headings `sd-fonts.yaml`
files them under, which are a filing system and not a classification. The 38
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
(That "five" is the tier as it stood on 2026-08-07; it is eight families now,
and the three additions since — Inknut Antiqua + Junicode, TeX Gyre Heros,
Almendra — widened the spread again rather than doubling a class: Almendra is
a bookish oldstyle drawn from calligraphy rather than cut in metal, which is
its own cell.)
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
