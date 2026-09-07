# DTL VandenKeere — sourcing, slot fit and verification

*2026-09-07, evening. The thirteenth installed family, added on the owner's
instruction ("Add the DTL VandenKeere SD family… so it ships in the NEXT
TestFlight build (185)"). Every number below is **read off a built `.cpfont`**
unless it is explicitly labeled a sweep prediction; the method is the joint
sweep of [docs/three-faces-2026-09-06.md](three-faces-2026-09-06.md) and the
synthetic-style corrections of
[docs/doves-italic-tuning-2026-09-07.md](doves-italic-tuning-2026-09-07.md)
§"Round five". The recipe in `sd-fonts.yaml` carries the short form of each
decision; this file carries the sweep, the rejected candidates, and the things
that were checked and found clean.*

---

## 1. What the face is, and what the files are

**Three OTFs**, from the owner's iCloud `dtl` folder, staged under clean names
in `lib/EpdFont/local_fonts/` (gitignored, never committed):

| staged as | source file | name ID 0 |
|---|---|---|
| `VandenKeere-Regular.otf` | `DTL VandenKeere SD Regular.otf` | "version 1.01/created on 01-05-1995/ by Dutch Type Library/URW#V019C13D" |
| `VandenKeere-Italic.otf` | `DTL VandenKeere SD Italic.otf` | "Version 1.0/ created on 27-05-97/ 1997 by Dutch Type Library / File#V019C33D" |
| `VandenKeere-Bold.otf` | `DTL VandenKeere SD Bold.otf` | "Version 1.0 / created on 2-5-95 / 1995 by Dutch Type Library / URW# V019C16D" |

CFF outlines, `unitsPerEm` 1000, `hhea` 963/−321/0 (roman), 959/−306/0
(italic), 956/−317/0 (bold) — sane, no `metrics:` rescue needed for the reason
Inknut's was.

**The attribution, confirmed from the font's own name table plus DTL's pages,
not assumed.** The files say only "Dutch Type Library"; the design credit comes
from [DTL's product page](https://www.dutchtypelibrary.nl/DTLVandenKeere.html),
which heads the face *"DTL VandenKeere | Hendrik van den Keere | Frank E.
Blokland"*, and its
[supplement](https://www.dutchtypelibrary.nl/VandenKeere_left_supp.html):

* **the roman follows the *Parangon Romein*** that Hendrik van den Keere cut in
  **1575**, which Plantin called *"Reale Romaine"* and first used in **1576**;
  Vervliet, quoted there, calls it *"one of the truly outstanding designs
  originating in the Low Countries"*;
* **van den Keere** — born Ghent about **1540**, died **1580**, Plantin's
  exclusive supplier of type from 1570
  ([Wikipedia](https://en.wikipedia.org/wiki/Hendrik_van_den_Keere));
* **the italic follows François Guyot's *Ascendonica Cursief*, c. 1557**, with
  *"shape, weight, and contrast adapted"* to sit with the roman and the
  capitals redrawn from the Parangon's rather than from Guyot's. Guyot
  (c. 1505–1570) was a French punchcutter registered in Antwerp from 1539 who
  supplied Plantin from 1558 until his death;
* the digital family was **drawn at DTL Studio 1991–1995**
  ([Fonts In Use](https://fontsinuse.com/typefaces/30273/dtl-vandenkeere)).

**No date was invented.** The picker's digital year is **1995**, taken from the
files' own name tables, on the same terms as DTL Romulus's 2003 — DTL publishes
no release year for either. The Italic file is dated 1997 and that is the same
family finished two years later, not a second stage.

### 1.1 The decision that has more than one defensible answer

**Origin 1575 Ghent (taken) versus 1557 Antwerp (not taken).** The italic's
model predates the roman's by eighteen years and is a different punchcutter's
work. The roman wins here on the precedent already recorded in
[docs/font-dates.md](font-dates.md) for Dante — *"a roman-led book face whose
italic is secondary"* — and because DTL's own product page names only van den
Keere and Blokland. **Choosing 1557 Antwerp instead would change the Designer
column and the colophon's first line but would NOT move the family in the
picker**: 1557 still falls between Edgar's 1722 and Dante's 1501, so it sorts
eighth either way. Guyot is recorded in `font-dates.md` and in the recipe rather
than dropped. Flagged for the owner rather than settled silently.

### 1.2 "SD", and why it is the cut that ships

DTL sells this face as **four series — D, SD, T and ST** — and **expands none
of the letters** on its product page, its
[shop page](https://www.dutchtypelibrary.nl/Shops/TypeShop/contents/en-uk/d111.html)
or its supplement. That is stated as unsourced rather than guessed at. What the
shop page *does* show decides the question anyway:

* **SD** — six faces: Regular / Medium / Bold × roman and italic
* **D** — twelve faces, including a Caps set
* **T** — a single Regular
* **ST** — a single Regular

**SD is the only series with three real cuts to ship**, and it is what the owner
supplied. Do not "correct" SD to T on the assumption that T means text: there is
no T italic and no T bold to pair with it.

---

## 2. The face, measured

Design-unit measurements from the sources (per 1000 em), against the two
nearest families in the tier:

| | x-height | cap | cap/x-ht | `d` ascender | `p` descender |
|---|---|---|---|---|---|
| **VandenKeere Regular** | **401** | **671** | **1.67** | 712 | −301 |
| VandenKeere Italic | 418 | 669 | 1.60 | 705 | −294 |
| VandenKeere Bold | 419 | 645 | 1.54 | 717 | −280 |
| Doves Text | 398 | 646 | 1.62 | 687 | −278 |
| Warbler Text | 464 | 727 | 1.57 | 765 | −248 |
| Dante Regular | 404 | 596 | 1.48 | 667 | −240 |

**Small on body, tall in the capitals.** x-height 401/1000 is the second
smallest of the installed faces (Doves' 398 is smaller), which is why `scale:`
comes out at the largest upward value in the file. cap/x-height 1.67 is the
largest in the tier, so at a matched x-height its capitals stand 2–3 px above
Doves' and Warbler's at the upper slots. That is the drawing; the anchor is
x-height and it wins.

**Stems**, middle row of `l`:

| ppem | Regular | Italic | Bold | Bold/Regular |
|---|---|---|---|---|
| 50 (this file's usual convention) | 80/1000 em | 60 | 100 | 1.25× |
| 200 | 70 | 65 | 110 | 1.57× |
| 400 | 72 | 68 | 110 | **1.53×** |

**The 50 ppem figure is too coarse to divide** — it is 4 whole pixels against 5
— so the ratio was taken at 400 ppem, where `o` reads 1.50× and `n` 1.47×.
**1.5× is what Blokland drew**, and that, not the 1.6× synthetic-bold target, is
what a synthetic bold italic in a family that ships a real bold has to match.

**Coverage.** 242 codepoints in the roman and bold, **221 in the italic**; 10 of
Latin Extended-A in the roman and bold, 4 in the italic; no Latin Extended
Additional, no Cyrillic, one Greek letter as a symbol. **The thinnest coverage
of any installed family** — Golden Cockerel's 249 was the previous floor — so
`reading` sends more of a page's accented text to the Noto fallback here than
anywhere else in the tier. Stated, not fixed: that chain is what it is for.

---

## 3. The slot fit

### 3.1 The sweep

`k` = 0.85…1.30 at 0.01, then 0.0005 near the winner × **every
strictly-increasing 6-size ramp from 6 to 31 pt** (solved by DP rather than
enumerated) × **every metrics span from the ink floor to 1500**, scored
lexicographically: x-height error first (the anchor), leading drift second.

**The ink floor is 1189** = worst-style plain ink span 1059 + 0.13 em. Plain ink
(ASCII printable) runs `$` 744 to `p` −301 in the roman, `$` 754 to `y` −305 in
the italic, `$` 760 to `y` −286 in the bold.

**The advanceY model was validated before it was trusted**, and the first
version of it was wrong in a way worth recording: FreeType's
`FT_Request_Metrics` derives `y_scale` from the **26.6 scaled height**, not from
the rounded `y_ppem` — `FT_DivFix(scaled_h, upem)`, not
`FT_DivFix(y_ppem << 6, upem)`. The `y_ppem` form disagreed with real FreeType
on 62 of 104 cases. The corrected form is **156/156 exact** on `advanceY`,
`ascender` and `descender` across six metric splits × 26 point sizes.

### 3.2 The result: drift 0

**`scale: 1.19`, ramp `8/10/12/14/16/18`, `metrics: {ascent: 1046, descent: −321}`
(span 1367).** Read off the built `.cpfont` headers and style TOCs:

| slot | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| point size | 8 | 10 | 12 | 14 | 16 | 18 |
| x-height (target 8/10/12/14/16/18) | **8** | **10** | **12** | **14** | **16** | **18** |
| advanceY (target 23/28/34/40/46/51) | **23** | **28** | **34** | **40** | **46** | **51** |
| cap height | 14 | 17 | 21 | 24 | 27 | 31 |

**DRIFT 0 on both axes** — the second family in the file to manage it, after
WarblerText, and the point sizes land on the slot numbers as well. The render
harness confirms the leading independently: `slot 0 line 23px … slot 5 line
51px`.

**k = 1.19 is the CENTRE of the drift-0 window, not its edge.** Every k from
**1.1625 to 1.2215** scores 0 on this ramp (boundaries located at 0.0005), and
the midpoint is 1.192, so a rounding change at either end cannot walk the fit
off. Span 1365…1368 all score 0 at k = 1.19; 1367 is the middle of that window.

### 3.3 Not taken

| k | ramp | x-drift | leading drift | why not |
|---|---|---|---|---|
| 1.05–1.07 | 9/11/13/16/18/20 | 0 | **1** (slot 2 lands 33) | close, but a ramp that stops stepping evenly and one pixel of drift for it |
| 1.08 | 9/11/13/15/18/20 | 0 | 2 | strictly worse |
| 1.23–1.27 | 7/9/11/13/15/17 | 0 | 2 | needs span 1475, 108 units above the one that scores 0, for no gain |
| ≥ 1.2225 | any | — | ≥ 2 | past the window's upper edge |
| ≤ 1.16 | any | — | ≥ 1 | below the window's lower edge |

**No trade-off was needed**, which is unusual and is why the recipe carries no
"descenders win" note. Span 1367 sits 178 units above the ink floor and inside
the 1365…1368 window that holds the exact leading, which leaves room for both
edges at once:

* **descent −321** is the deepest ink in the whole `reading` charset in any
  style — the pilcrow at −321 (roman) and −317 (bold). The deepest *letter* is
  the italic's `y` at −305 and the roman's `p` at −301;
* **ascent 1046** clears the tallest accented capital by 83 units — `Scaron` at
  963 (roman), `Acircumflex` at 959 (italic) and 956 (bold).

**Nothing pokes above the declared ascender and nothing falls below the declared
descender, in any of the four styles.** WarblerText is the only other installed
family of which that is true.

### 3.4 Per-style, read off the built files

| style | x-height | cap height |
|---|---|---|
| regular | 8 / 10 / 12 / 14 / 16 / 18 | 14 / 17 / 21 / 24 / 27 / 31 |
| italic | 8 / 10 / 12 / 14 / 16 / 18 | 14 / 17 / 20 / 23 / 27 / 30 |
| bold | 9 / 11 / 13 / 15 / 17 / 19 | 13 / 17 / 20 / 23 / 26 / 30 |
| bolditalic | 9 / 11 / 13 / 15 / 17 / 19 | 15 / 18 / 21 / 24 / 28 / 31 |

The italic matches the roman **exactly** at all six slots and the bold runs
1 px over at every slot,
which is Blokland's drawing (401 / 418 / 419 per 1000 em across the three) and
not a mismatch to correct. Per-style `scale:` is for MIXED-SOURCE families;
these three are one typeface.

### 3.5 The measured tier, for context

Every row read off the built `.cpfont` in `lib/EpdFont/scripts/output/`, roman
style, x-height taken as the glyph's `top` (see §6.1):

| family | point sizes | x-height | advanceY | x-drift | leading drift |
|---|---|---|---|---|---|
| **VandenKeere** | 8/10/12/14/16/18 | 8/10/12/14/16/18 | 23/28/34/40/46/51 | **0** | **0** |
| WarblerText | 8/10/12/14/16/18 | 8/10/12/14/16/18 | 23/28/34/40/46/51 | 0 | 0 |
| Almendra | 8/10/12/14/16/18 | 9/11/13/15/16/19 | 23/29/34/40/46/51 | 5\* | 1 |
| Coelacanth | 9/11/13/15/18/20 | 8/10/11/13/15/17 | 23/28/34/39/46/52 | 4 | 2 |
| DanteMT | 10/12/14/17/20/23 | 8/10/12/14/16/18 | 23/28/33/40/47/54 | 0 | 5 |
| Doves | 8/10/12/14/16/18 | 8/10/12/14/16/18 | 23/29/35/41/47/52 | 0 | 5 |
| LibreFranklin | 7/9/10/12/14/16 | 8/11/12/14/16/18 | 23/30/33/39/46/52 | 1 | 5 |
| TeXGyreHeros | 7/9/11/12/14/16 | 8/10/13/14/16/18 | 23/29/36/39/46/52 | 1 | 5 |
| LibrisADF | 8/10/12/14/16/18 | 8/10/12/14/16/18 | 23/29/35/41/47/53 | 0 | 6 |
| LutetiaNova | 8/10/13/15/18/20 | 7/9/11/13/15/17 | 21/26/33/39/46/51 | 6 | 6 |
| Edgar | 8/10/12/14/16/18 | 8/10/12/13/15/17 | 22/27/32/38/43/49 | 3 | 11 |
| TeXGyreSchola | 8/10/12/14/16/18 | 8/9/11/13/15/17 | 21/27/32/37/43/48 | 5 | 14 |
| InknutJunicode | 8/10/12/14/16/18 | 8/9/11/13/15/17 | 21/26/31/36/41/47 | 5\*\* | 20 |

\* Almendra's x-drift is **deliberate**: its ramp is anchored on ink per
character rather than on x-height, so it runs over this target by design
(docs/almendra-size-match-2026-08-26.md). \*\* InknutJunicode's ramp has always
run +1 on point size for the same kind of reason; see its recipe.

---

## 4. Ligatures and kerning

**No GSUB at all** — the DTL Romulus and Golden Cockerel case exactly. `fi` and
`fl` are drawn and cmapped at U+FB01/FB02 and nothing reaches them, so
`synth_ligatures: [fi, fl]` builds the rule with feaLib. Nothing else is drawn:
no `ff`, `ffi` or `ffl` in any of the three faces. The built files carry
`ligs=2` in all four styles — verified in the style TOC and again through the
render harness.

**Kerning.** GPOS `kern` is present; the converter's own extractor returns
**959 / 952 / 954** pairs over `reading` for regular / italic / bold, and the
built files carry `kernL=69 kernR=73` in every style. Modest beside Doves'
18,852 — this is a 1995 file with a Mac-era charset.

**A legacy `kern` table is ALSO present** (996 / 1000 / 997 pairs) and **945 of
its pairs are duplicated in GPOS**. The extractor takes the GPOS value and says
so rather than summing them, which would double the designer's correction —
this is the family that exercises that path, and it is the change made earlier
today in `8022e2bce`.

**Spot-checked against `hb-shape`** (`/opt/homebrew/bin/hb-shape`,
`--font-size=1000`, kern read as the shaped advance minus the `hmtx` advance),
nine pairs, at the shipped scale (upem 840 after k = 1.19) and 12 pt / 25 ppem:

| pair | hb-shape, design units | expected fp4 | extractor |
|---|---|---|---|
| AV | −100 | −47.6 | **−48** |
| AW | −100 | −47.6 | **−48** |
| To | −88 | −41.9 | **−42** |
| Va | −87 | −41.4 | **−41** |
| y. | −46 | −21.9 | **−22** |
| Pa | −15 | −7.1 | **−7** |
| r, | −30 | −14.3 | **−14** |
| LT | −67 | −31.9 | **−32** |
| ov | −5 | −2.4 | **−2** |

**9 / 9 exact.**

---

## 5. The synthetic bold italic

Three of the four styles are real. **No SD bold italic exists on disk**, so that
one style is cut from the real italic.

`embolden_em: 0.034` = 0.50 × the italic's 0.068 em stem, the family's own
1.5× bold ratio applied to the italic rather than the 1.6× synthetic-bold
target — a family that ships a bold has already answered that question.
`y_ratio: 0.35` as every other synthetic style in this file uses.

`baseline_shift_em: 0.00595` = `embolden_em × y_ratio / 2`, exactly half the
vertical growth the embolden's own re-centering translate gives back
([round five](doves-italic-tuning-2026-09-07.md)). **It is arithmetic, not a
fit, and it was measured off the built `.cpfont` rather than assumed** — a
control build with the line removed was made for the comparison:

| | bolditalic glyphs off the italic's baseline row | |
|---|---|---|
| without `baseline_shift_em` | 11,583 / 15,876 | 73% |
| **with** `baseline_shift_em: 0.00595` | **421 / 15,876** | **2.7%** |

That residual sits exactly where round five left the other corrected families
(LutetiaNova 2%, Golden Cockerel 3%): glyphs whose rounded bottoms grow across a
pixel boundary under the embolden itself. **Round five's ruling is not to chase
it**, and no offset sweep was run here for that reason.

**UNCONFIRMED ON DEVICE.** The strength was derived from stems, not judged on
glass. If it reads heavy or thin, `embolden_em` is one number.

---

## 6. Verification, and what it caught

Everything in §3 and §4 is read out of a built file. The two things that a
prediction would have got wrong:

1. **The advanceY model's `y_scale`** (§3.1) — 62 of 104 cases wrong before it
   was validated against real FreeType. Had the sweep been trusted unvalidated,
   the leading would have been fitted against a fiction.
2. **The synthetic baseline** (§5) — the recipe's first draft claimed "0 of 2004
   glyph/size pairs off the baseline" from arithmetic. The real number is 421 of
   15,876, and the comment now says so.

### 6.1 A measurement convention worth knowing, found on the way

**"x-height" off a `.cpfont` has two readings and they differ by a pixel for
some faces**: the glyph's `top` (its ink above the baseline) and its bitmap
`height` (`rows`, which also counts any overshoot *below* the baseline).
Warbler Text's `x` dips 1.35/1000 em below the baseline, so it reads
9/11/13/15/17/19 by `height` and 8/10/12/14/16/18 by `top` — its recipe's
"EXACT" is right, under the `top` reading. Lutetia Nova is the same shape.

**VandenKeere's `x` dips 3/1000 em, which rounds away at every slot, so it reads
8/10/12/14/16/18 under BOTH conventions.** The fit is exact either way, and that
is not luck worth relying on for the next family — state the convention.

---

## 7. Built, and checked clean

* **All three tiers build**: `--scale 1`, `2` and `3`, first attempt, **no
  `hires_drops:` needed** — verified by building all three rather than by
  reasoning about the top slot. The whole `reading` charset rasterises through
  the real fallback chain at 1×, 2× and 3× with only the global
  `tier_drops: {3: [0x2E3B]}` applied; nothing else in this family crosses
  `EpdGlyph`'s uint8 at any tier. The top slot is 18 pt, the same as
  WarblerText's; Dante's 23 pt top slot is what makes its 2× line necessary.
  Re-verify if the ramp grows.
* **`installed_families:` is thirteen.** The prose in `sd-fonts.yaml` that
  describes that list was updated with it, since a stale count there is what
  the 2026-09-07 "twelve vs thirteen" correction already had to fix once.
* **`src/FontDisplayNames.h`** carries the row; the string literals are pure
  ASCII, so the greedy-`\x` hazard does not arise here (nothing in "Ghent",
  "'s-Hertogenbosch", "Hendrik van den Keere" or "Frank E. Blokland" needs an
  escape). The apostrophe opening `'s-Hertogenbosch` is legal in a C string.
* **`test/settings_display_order`**: 15/15 pass, with the installed set and the
  expected picker order updated. VandenKeere sorts **eighth**, between Edgar
  (1722 London) and Dante (1501 Venice).
* **Specimen rendered** through the real `GfxRenderer` / `SdCardFont` path:
  `./render_harness inline VandenKeere` → `fs_/inline_VandenKeere_{0..5}.bmp`.
  Slots 0, 3 and 5 were read as images. Roman, italic, bold and the synthetic
  bold italic sit on one baseline in the four-style header line at every slot
  read; the italic is a real cut and reads as the same printer's ink beside the
  roman inline.
* **The face's figures are OLDSTYLE by default** (`30,865`, `41,072` in the
  specimen render as text figures). That is the face, not a build setting, and
  the other installed faces were not surveyed for it. Noted so it is not
  reported as a bug.
* **The Medium cuts were NOT staged.** DTL's SD series includes a Medium and a
  Medium Italic; the reader has four style slots and the Bold is what "bold"
  means, per the Dante ruling of 2026-09-06. If the Bold ever reads too black on
  glass, the Medium exists and reversing is two `path:` lines — but that is a
  ruling, not a tidy-up.
* **`local_fonts/` stays gitignored** (`.gitignore:38`, confirmed with
  `git check-ignore -v`). A clean clone cannot rebuild this family, as with
  Dante, Lutetia Nova, Doves, WarblerText and the two cut families. The recipe
  references paths, not bytes.

## 8. Unconfirmed on device, stated plainly

* **The synthetic bold italic.** Derived from stems to the family's own 1.5×
  ratio; never seen on glass.
* **The tall capitals.** cap/x-height 1.67 is the largest in the tier and shows
  as 2–3 px over Doves and Warbler at the upper slots. It reads correctly in the
  1× plates at slots 0, 3 and 5; whether a page of it reads *busy* on the phone
  is the part only the phone can answer.
* **Nothing was shipped.** `install-sim-fonts.py` and `publish_fonts.py` were
  deliberately not run — build 184 was in flight. The `.cpfont` files are in
  `lib/EpdFont/scripts/output/VandenKeere/` at 1×, 2× and 3×, and a copy is in
  `tools/calendar_preview/fs_/.fonts/VandenKeere/` for the harness.
