# Per-ligature control, and the Typography Settings screen

Date: 2026-08-24. Firmware at `b6d785eca` + this change. Everything below was
verified against source or measured on a render; where it was inferred, it says
so.

Owner ruling, verbatim: *"give a full subpage of Typography Settings that gives
all available typography options with full granularity, including toggling each
individual ligature."* Placement ruling, same day: *"Typography Settings should
be between Text Settings and Editing Font."* — that first screen is named
**Reader Font** since 2026-08-24; the quote keeps the words he used.

What produced it: *"almendra has at least one distracting ligature (st)"*, then
*"can the weirder ligatures (like st) be a firmware setting?"* So `st` is the
motivating case and the instrument is the ask.

## 1. Why this needed no font rebuild

Ligature substitution is a RUNTIME lookup. `EpdFont::applyLigatures`
([EpdFont.cpp](../lib/EpdFont/EpdFont.cpp)) walks the text and consults the
sorted `EpdLigaturePair` table for each adjacent codepoint pair at draw time,
and `getLigature` beside it is a binary search over that table. Nothing is baked
into a glyph run.

So switching one off is a decision made at draw time. Neither the `.cpfont`
format, nor the converter, nor a single byte of any installed family changes,
and the whole feature is a gate in one `if`.

## 2. The identity is the INPUT PAIR, not the output codepoint

This is the load-bearing decision, and the opposite choice would have shipped a
row that lies about what it does.

A pair's **output** codepoint is the font's private business. The Unicode
presentation forms U+FB00–U+FB06 (ff fi fl ffi ffl ſt st) are shared, but
everything a face carries beyond them lands in the Private Use Area, and PUA is
per-family by definition: Edgar ships nine at U+E000–U+E008, and U+E000 means
`fb` only because Edgar's converter said so.

The **input** pair does not have that problem. `st` is U+0073 U+0074 in every
face ever cut, so a preference expressed that way survives a family switch, a
font rebuild, and a card moved between devices.

**Almendra proves the output side is not even self-consistent.** Its regular
face carries `f`+`h` → **U+FB00**, the codepoint that means `ff` everywhere
else, and it has no `f`+`f` rule at all. That is faithful extraction rather than
a pipeline bug — Almendra's own cmap maps U+FB00 to a glyph named `f_h`
(`docs/cpfont-format.md` gotcha 17, verified with fontTools 4.63.0 against
`lib/EpdFont/scripts/downloaded_fonts/Almendra/H4ckBXKAlMnTn0CskyY6.ttf`).

Had the rows been labeled from the output codepoint's Unicode name, **Almendra's
row would have read "ff" and would have controlled "fh"**. Confirmed on a render
in §6.

## 3. What each family actually ships

Dumped from the shipped `.cpfont` files on the card, 2026-08-24, by parsing the
style TOC and the ligature block directly. Regular face unless noted.

| Family | Regular-face pairs |
|---|---|
| Almendra | `fh`→U+FB00, `fi`→U+FB01, `fl`→U+FB02, `st`→U+FB06 |
| Edgar | `fb`→U+E000, `ff`→U+FB00, `fh`→U+E005, `fi`→U+FB01, `fj`→U+E006, `fk`→U+E007, `fl`→U+FB02, and the six chained ones off U+FB00: `ffb`→U+E001, `ffh`→U+E002, `ffi`→U+FB03, `ffj`→U+E003, `ffk`→U+E004, `ffl`→U+FB04 |

Two facts about that table shape the UI:

* **The set is per-family and open-ended.** Edgar's are not Almendra's. There is
  no fixed enumeration a bit index could refer to, which is why the storage is a
  string and the rows are built from the font's own table.
* **The set is per-STYLE too.** Almendra's italic and bold carry only `fi` and
  `fl`; Edgar's italics add `gy`→U+E008 that its roman does not have. The
  Typography screen takes the UNION across the four styles, because a row
  missing for that reason would read as "this face has no gy ligature", which is
  false.

**So there are two counts for Edgar and both are correct in their own place:
thirteen in the regular face (the table above), fourteen in the union the screen
lists** (`gy` comes from the italics). Every "how many rows does the screen
draw" figure in the code comments is the union. Almendra is four either way.

## 4. What was ruled out, and why — record the negatives

* **A bitmask over the seven presentation forms.** Wrong twice over: it cannot
  express a PUA ligature at all, and Almendra shows the FB00–FB06 range is not
  a reliable naming either.
* **Keying on the output codepoint.** §2. Would have mislabeled the exact family
  the owner is looking at.
* **`dlig` / `hlig` extraction.** Not attempted, and it should not be: the
  converter extracts `liga` + `rlig` only
  (`LIGATURE_FEATURES`, `fontconvert.py`), then filters through
  `is_presentation_ligature` (`fontconvert_sdcard.py:198`). Probing Junicode
  found **all 31 `dlig` and all 153 `hlig` rules output UNENCODED glyphs**, so
  enabling those features would extract nothing that could be named or rendered.
  Turning them on is not a small win being left on the table; it is a no-op.
* **A three-state master (Off / Essential / All).** Considered and dropped. A
  mode that can override the granular switches puts two sources of truth behind
  one visible row state, and the row would then have to say "on, but not really"
  — which this list has no widget for. The master is a plain switch that
  DOMINATES, and while it is off the per-pair rows are not offered at all.
* **A "reset all" action row.** The master switch already reaches "no ligatures
  anywhere" in one press, and per-pair state is preserved across it, so the
  reset would only ever discard work.

## 4b. The screen, as it ships

Owner ruling 2026-08-24, after the ligature work landed: *"put or move line
grid, line spacing, letter spacing, justified text to Typography Settings"* and
*"rename Text Settings to Reader Font."*

**Typography Settings**, top to bottom — coarse to fine, so the rows most
readers want are not below fourteen ligature toggles:

| Row | Kind | Where it came from |
|---|---|---|
| Line Spacing | enum, Tight/Normal/Wide | row REINSTATED (deleted 2026-08-21) |
| Line Grid | toggle | moved off the Settings list |
| Justified Text | enum, the character ladder | moved off the Settings list |
| Ligatures | toggle, master | new |
| one row per pair | toggle | new; from the loaded family's own table |

**Settings**, after the moves: Reader Font · Typography Settings · Editor Font ·
Dark Mode · Keyboard · Sleep Screen · Clock UTC Offset · then the device-only
actions.

Three things about how the move was done:

* **The rows are SELECTED out of `getSettingsList()`, not redefined.** Each keeps
  its one definition — one label, one JSON key, one accessor — so appearing on a
  different screen cannot change what it stores or what the web API calls it.
  The move itself is one word per row: `STR_CAT_SYSTEM` → `STR_CAT_READER`,
  which is the withdrawn category `rebuildSettingsLists()` drops.
* **Line Spacing's row came back, and only its row.** The FIELD never went: it is
  the one entry in the reading-taste block that stayed non-`constexpr`, because
  the reader has a designed chord (Confirm held + a side button) that steps it.
  The 2026-08-24 ruling supersedes the 2021-08-21 reduction **for this field
  only**. Its hand-written `toJson`/`fromJson` pair is retired with the row's
  return — a row with a `valuePtr` is carried by the generic loop, and two
  writers of one key is the thing that pair existed to work around.
* **Letter spacing is deliberately absent.** There is no `letterSpacing` field
  and no tracking anywhere in the layout engine, so it is a new feature — a
  per-glyph advance adjustment, therefore re-pagination and a section-cache
  bump — not a move. A row for a setting with no renderer behind it is worse
  than no row.

`settingrow::valueText` / `activate` ([SettingRowUi.h](../src/activities/settings/SettingRowUi.h))
are shared by both screens, so a row cannot draw or behave one way on Settings
and another way here. That file exists because the alternative was a second copy
of four separately-paid-for traps (`enumCount()` vs `enumValues.size()`, label
source independent of value source, bounds-checked indexes, display order
carried by value).

## 5. Where the change lands

| Concern | File |
|---|---|
| The pure model: spec parsing, canonical form, editing, fingerprint, spelling | [lib/EpdFont/LigatureControl.h](../lib/EpdFont/LigatureControl.h) / `.cpp` |
| The gate | `EpdFont::applyLigatures`, [EpdFont.cpp](../lib/EpdFont/EpdFont.cpp) |
| Storage | `ligaturesEnabled`, `ligaturesOff[]` in [CrossPointSettings.h](../src/CrossPointSettings.h); two `STR_CAT_READER` rows in [SettingsList.h](../src/SettingsList.h) |
| Cache invalidation | `ReaderRenderSpec::ligatureFingerprint`, written/compared in [Section.cpp](../lib/Epub/Epub/Section.cpp), `SECTION_FILE_VERSION` 50 → 51 |
| The screen | [TypographySettingsActivity](../src/activities/settings/TypographySettingsActivity.h) |
| Tests | `test/ligature_control/` |

**The two `STR_CAT_READER` rows are there for persistence and the web settings
API, not for the device list.** Reader is a withdrawn category that
`rebuildSettingsLists()` drops. Deleting the rows instead would stop both
settings persisting at all and drop them from the HTTP API — the trap
`CLAUDE.md` documents and this repo has paid for twice.

**Why the invalidation is a fingerprint and not the string.** The spec is
variable-length and the section header is a fixed-shape struct compared field by
field. Nothing needs to read the preference back out of a section file, only to
notice that it moved, so a 32-bit FNV-1a over the canonical packed pairs is
enough. Canonical is doing real work there: `"st,fh"` and `"fh,st"` are the same
preference, and a fingerprint that moved on a re-ordered hand edit would
repaginate every book on the card for nothing.

**`fingerprint(false, …)` ignores the suppression list on purpose.** With the
master off, the list has no effect on the page, so editing a row that is
currently doing nothing must not repaginate.

## 6. The proof, measured

Specimen: `fs_/books/ligatures.epub`, four paragraphs — `stst stst stst`,
`ff ff ff`, `fh fh fh`, `fi fl`. Almendra, slot XL (17 pt), X3 geometry,
`CROSSPOINT_SIM_GRAIN_SEED=1` pinned (without it the sim's per-launch grain seed
is ~2 code values and swamps the effect — `crosspoint-simulator/CLAUDE.md`).
Card state rebuilt identically for each arm.

Mean absolute difference per paragraph band, panel pixels:

| band | control (two identical runs) | `ligaturesOff = "st,fh"` |
|---|---|---|
| `stst` | 0.00, max 0 | **7.93, max 183** |
| `ff` | 0.00, max 0 | 0.00, max 0 |
| `fh` | 0.00, max 0 | **3.41, max 183** |
| `fi fl` | 0.00, max 0 | 0.00, max 0 |

Three things that table settles:

1. The control is **bit-identical**, so the model is deterministic and every
   nonzero figure beside it is the feature.
2. Exactly the two named bands moved. One row does not leak into another.
3. **`ff` is bit-identical** — confirming §2 on a render. Almendra has no `f`+`f`
   rule, so "ff" was never ligated, and switching off the row whose OUTPUT
   codepoint is U+FB00 (the "ff" codepoint) correctly leaves it alone while
   changing "fh". A codepoint-named row would have promised the opposite.

Rendered crops at native pixels, 4× NEAREST: `qa/lig/st_{on,off}_4x.png`
(content coverage 17.2%, effect delta mean 18.4, 14.1% of pixels past 4 levels)
and `qa/lig/ffh_{on,off}_4x.png`. **`qa/` and `fs_/` are both gitignored**, so
the figures and the specimen are not in the repo and the table above is the
durable record. Regenerate either from the recipe below.

<details><summary>Reproduction recipe</summary>

The specimen is four paragraphs — `stst stst stst`, `ff ff ff`, `fh fh fh`,
`fi fl` — as a minimal EPUB at `fs_/books/ligatures.epub`. Each arm rebuilds the
card state identically, which matters: a run leaves its page position behind, so
without the wipe the second arm starts somewhere else and the "effect delta"
measures the book rather than the setting.

```bash
rm -rf fs_/.crosspoint && mkdir -p fs_/.crosspoint
cat > fs_/.crosspoint/state.json <<'J'
{"openEpubPath":"/books/ligatures.epub","readerActivityLoadCount":0,"showBootScreen":false}
J
cat > fs_/.crosspoint/settings.json <<'J'
{"sdFontFamilyName":"Almendra","fontSizeSlot":3,"fontSize":17,"ligatures":1,"ligaturesOff":"st,fh"}
J
CROSSPOINT_SIM_GRAIN_SEED=1 \
CROSSPOINT_SIM_INPUT_SCRIPT='9000:QUIT' \
CROSSPOINT_SIM_SCREENSHOTS='7000:qa/lig/off.bmp' \
SDL_VIDEODRIVER=dummy .pio/build/simulator_x3/program
```

`CROSSPOINT_SIM_GRAIN_SEED=1` is not optional. Without it the simulator re-rolls
its grain and scanline seed every launch, two identical runs differ by ~2 code
values, and the first attempt at this measurement showed a CONTROL delta larger
than the effect. Captures are BMP whatever the file is named
(`crosspoint-simulator/docs/headless-qa.md`).

For the cache half, run the same arm twice without the `rm -rf`, changing only
`ligaturesOff`, and grep the log for `[SCT] Deserialization`.

</details>

**The device write path is covered end to end**, not just by hand-written
settings.json: driving the real UI headlessly (Home → Settings → Typography
Settings → Confirm on the `st` row → Back) wrote `{"ligatures":1,
"ligaturesOff":"st"}` to the card, and the next launch read it back and rendered
without the ligature. That run's input settings.json carried NO ligature keys at
all, so it doubles as the old-file compatibility test.

**Coverage gap, stated rather than papered over.** `ligaturesOff` is the first
live user of `SettingInfo::String` anywhere in the tree, so it activates four
previously dead branches: the `stringOffset` paths in `toJson`/`fromJson` and
the STRING cases in the web settings API's GET and POST. The first two are
exercised by the round trip above. **The two web branches are read but not
executed** — the web server only runs inside `CrossPointWebServerActivity`
(the File Transfer screen) and needs a Wi-Fi association, so it is not reachable
from a headless script. No host test round-trips a STRING setting either. If
that path ever misbehaves, this paragraph is where to start.

**Cache invalidation, separately.** With a section cache already built and
ONLY the setting changed:

```
control (nothing changed):  [SCT] Deserialization succeeded: 1 pages
change  (st suppressed):    [SCT] Deserialization failed: Parameters do not match
```

Both halves matter. The second is the feature; the first is the proof that the
fingerprint is stable, i.e. that this does not silently repaginate the card on
every boot.

## 7. Memory safety of the parser, checked rather than argued

The spec string reaches `parse()` from a hand-edited `settings.json` and from
the web settings API, so it is attacker-adjacent input landing in a fixed
`uint32_t[MAX_SUPPRESSED]`. Rather than reason about the bounds, it was run
under ASan + UBSan (clang, `-fsanitize=address,undefined`) against: the empty
and null specs; a spec of nothing but commas; one-, three- and zero-codepoint
tokens; a 300-byte token; a 4,000-byte spec; invalid UTF-8 (`\xFF\xFE`,
`\xE0\x80`, a truncated sequence); astral-plane codepoints; a spec already at
the `MAX_SUPPRESSED` ceiling with eight more insertions attempted; and 20,000
random byte-soup strings up to 400 bytes, each put through `specSuppresses`,
`fingerprint`, `specWith` (both directions), `canonicalize`, `configure` and
`allowed`. **No diagnostics.**

`spellPair` was additionally given a malformed table naming a ligature as its
own ancestor (`U+FB00 + i → U+FB00`). It terminates on the depth-4 guard and
returns a nonsense but bounded label. No real font can produce that table; the
guard exists so that a corrupt one cannot spin.

## 8. Known, deliberate, not fixed

**The empty state pads three rows.** When the master switch is on and the loaded
family carries no pairs, the Ligatures row gets a subtitle explaining why there
is nothing under it — and `LyraTheme::getListRowStep` sizes **every** row in a
list from one `hasSubtitle` flag, so Line Spacing, Line Grid and Justified Text
each draw a blank second line's worth of height too. Hit-testing and paging stay
correct (`loop()` is passed the same flag), so this is purely vertical padding.

Not fixed, for two reasons. The flag lives in shared theme code that every list
in the app sizes itself from, and per-row heights would be a change to all of
them for one screen's cosmetics. And the state is **unreachable on a shipped
card**: rendered 2026-08-24, every reading family present — Almendra, Coelacanth,
Edgar, InknutJunicode, LibreFranklin, LibrisADF, TeXGyreHeros, TeXGyreSchola —
draws pair rows. Only a user-installed face with no ligature table reaches it.
Found by adversarial review, not by looking at the screen.

* **The SD-font prewarm still warms suppressed ligatures' glyphs**
  (`SdCardFont.cpp:1007`, `FontDecompressor.cpp:319`). Those walk the pair table
  directly rather than going through `applyLigatures`. The cost is a few glyph
  bitmaps read that will not be drawn; the benefit of gating it is that the
  preference would have to reach a second layer, where getting it wrong means a
  MISSING glyph rather than a wasted one. Left alone on purpose.
* **The gate is device-wide, not reader-only.** `ligatures::allowed` is a
  process global asked from `EpdFont`, so switching `st` off switches it off in
  the UI chrome, the note editor and the sleep screens too -- not only in the
  reading face whose pairs the Typography screen enumerated. In practice the
  chrome face (Libre Franklin) carries only `fi`/`fl`, and a preference about
  type is not usually a preference about which screen the type is on. Stated
  here because it is not obvious from a screen that lists one family's pairs.
* **`MAX_SUPPRESSED` (24) is a shared budget, not a per-family one.** The key is
  family-independent by design, so every family's switched-off pairs land in the
  same list. Realistic worst case is two rich families with disjoint sets --
  Edgar's 14 plus Almendra's non-overlapping `st` is 15 -- so the headroom is
  real, but at the ceiling `specWith` refuses and the toggle no-ops. Raising it
  means raising `SPEC_BUF_SIZE` with it; the ceiling test checks the pair.
* **Almendra's `fh`→U+FB00 is not corrected.** It is the font's own cmap, the
  extraction is faithful, and rewriting it would put this repo's opinion above
  the foundry's. It is recorded in `docs/cpfont-format.md` gotcha 17 and here.
