# Adversarial review, batch 7 (2026-11-28)

A read-only agent that did not write the code reviewed `origin/main..campaign-b7`
(base `5546b040b`): the eight code commits of batch 7 plus the SDK move inside
`d60f30da0` (`freeink-sdk` branch `campaign-b7`, pin `119f3ed` plus a hand-resolved
cherry-pick of upstream SDK `e040e9a2`). Its brief was to refute, to try to
disprove each finding before reporting it, and to list what it checked and found
clean. Model: Sonnet 5.5, as were the ports.

How sure: everything below is **by reading** unless marked. One probe was written
(the read-aloud one, F2), compiled against the real `ReadAloudCapture.h`. Nothing
ran on a device and no PlatformIO build was run by the reviewer; the builds, the
latency gate and the host suite had already passed. `gh_release_rc` was never
built, so F1 rests on the code alone.

## Findings

**F1. The app descriptor version is no longer stamped in `default` and
`gh_release_rc` builds.** Silent wrong output, by reading. Commit `d7f4858d2`
(upstream `bbb45c39a`). `scripts/stamp_app_desc.py:97-116` reads the version from
`env.get("CPPDEFINES")` and is registered post-link for every base env
(`platformio.ini:259`). `scripts/git_branch.py:131-148` now adds the define only
to a per-source cloned env, so the project env no longer holds it. For the two
envs that script handles, `_version_from_flags` returns None, the stamp prints
"leaving the descriptor alone", and the image ships the stale prebuilt descriptor
version (the B-033 bug): OTA metadata and the web build line report it, the boot
screen does not. `gh_release` and the other envs set the define as an ini literal
and are unaffected. No gate catches it, because a skipped stamp is not an error.
Smallest fix: have `inject_version` also record the string outside `CPPDEFINES`
(e.g. `env.Replace(CROSSPOINT_VERSION_STR=...)`) and have `_version_from_flags`
read that first; then build `gh_release_rc` and look for the stamp line.

Outcome: F1 dropped: `d7f4858d2` (upstream `bbb45c39a`) removed from the branch; recorded as SKIP in the Batch 7 section with what a future take needs.

**F2. Read-aloud speaks "wellknown" for any hyphenated word that wraps.** Silent
wrong output, **reproduced**. Commit `e9b5b859d` (upstream `9dfa92ba2`), with the
fault in `src/activities/reader/ReadAloudCapture.h:124-137`. A word with a hard
hyphen is now several tokens, so a line can end on one. The line-final `-` join
heuristic ("a line-final '-' joins to the next line's first word, dropping the
hyphen") then fires; before this commit it fired only on hyphenator splits. Probe:
a line ending `well-` followed by a line starting `known` produces
`[A wellknown thing]`. `Section::getTextFromSectionFile` (`Section.cpp:1284`) also
joins every token with a space ("well- known"); it has no callers outside
`Section.h`. Smallest fix: do not pop the `-` unless the token ended in a soft
hyphen, or keep the hyphen always and accept "ex-ample" in speech.

Outcome: F2 fixed in `f262d9b34`: the layout records how each line's last token joins the next (`TextBlock::lineEndJoin`), the capture follows it; a hyphen the hyphenator inserted is dropped, one that belongs to the source word is kept and joined with no space. Ten new host cases. `Section::getTextFromSectionFile` has no caller and was left alone.

**F3. The X3 gauge capacity load can be cut off by sleep.** Latent, X3 only, by
reading. Commit `d60f30da0` (upstream `0a9077722`), `src/main.cpp:1408-1417` and
SDK `BatteryMonitor.cpp`. The sequence runs about 8 s from boot, one step per loop
pass, and nothing aborts it on sleep. Powering off in that window leaves the gauge
unsealed or in CONFIG UPDATE (no fuel-gauge updates) until the next boot, whose
step 0 closes it (`dc == mah && (cfg || security != 3)`). It starts only after the
gauge lost power (Design Capacity reads 3000), not on every wake. Smallest fix, if
wanted: skip steps once `deepSleepInProgress` is set, or close before sleeping.

Outcome: F3 left as is (recorded, latent, X3 only, recovered at the next boot).

**F4. The non-CJK UI fallback switches on the Noto coverage face, and a font
switch then wipes it.** Cosmetic and latent, by reading; heap cost not measured.
Commit `811fe3460` (upstream `fdf5f76c0`). `applySystemFont` (`main.cpp:497-506`)
registers a Noto UI face as the fallback for the three UI ids on every boot. It
never fired for non-CJK text; now any codepoint at or above 0x80 that Libre
Franklin lacks and Noto has sends the whole string to Noto. An interval-table diff
found 435 such codepoints (combining marks, U+017F and others); no character in
the english or spanish translations is among them, so only book titles and
filenames change typeface. `SdCardFontManager::unloadAll` (`:211`) calls
`clearFallbackFonts()`, which also drops the Noto entries, so after any SD family
switch the coverage fallback is gone until the next boot. Separately,
`setupUiFallbacks` now loads the 8, 10 and 12 pt UI sizes of any family with
Greek, Cyrillic or U+0259, so most Latin families pay that RAM and SD cost.
Smallest fix for the wipe: re-run the Noto mapping after `unloadAll`, or make
`clearFallbackFonts` skip built-in ids.

Outcome: F4 dropped: `fdf5f76c0` and `28971493d` (`811fe3460`, `ef24f4758`) removed from the branch; recorded as NOT PORTED, needs a heap measurement and the `unloadAll` fix first.

**F5. The chapter list can open one row off when no section is live.** Cosmetic,
by reading. Commit `cc282e1d6` (upstream `aad48b40d`),
`EpubReaderActivity::currentTocIndex`. With `section` null it builds a throwaway
`Section` and reads whatever `.bin` is on disk against `nextPageNumber`, without
checking the file's version or viewport, so anchor pages from a stale layout can
select a neighboring TOC entry.

Outcome: F5 left as is (recorded, cosmetic).

Not a defect, but worth knowing: the SDK pointer `65c8bd8` exists only on the
local `freeink-sdk` branch `campaign-b7`; a fresh clone cannot fetch it until it
is pushed. One process slip by the reviewer: a `git checkout --detach` was issued
by mistake and denied by the permission system; nothing changed.

## Checked and found clean

- **Hyphen and dash tokens (`e9b5b859d`), apart from F2:** every piece of a word
  gets the same source offset, so the resume and progress anchors, Find and
  footnote word-index tracking are unaffected. Pieces are `continues` plus
  `noSpaceBefore`, so `isJustifiableGap` is false: no space or stretch lands
  between pieces. Kerning across a boundary is applied in the greedy breaker,
  Knuth-Plass `gapBefore` and `extractLine`; the RTL reorder keeps the same flags.
  The hyphenator is offered one piece at a time and a split piece's remainder gets
  `continues=false`. Traced: `--`, a leading hyphen and `a-` give no break; `1-2`
  and `a--b` break once after the run; U+2011 and a soft hyphen never create a
  break. The CJK and Hangul interaction is consistent. `pagemetrics` is
  simulator-only and approximate.
- **Soft flush (`887557824`):** `topSpacingSpent` lives on the `ParsedText`, and
  every block is a fresh one, so it cannot leak to the next block. The early
  returns fall inside the lost-block case, and merge and reuse paths cannot reach
  a 750-word flush. Image, table and hidden blocks never call `makePages`. Gap
  cap, collapse reset, scene-break lift and line grid run once (`startOfBlock`);
  the widow/orphan tail, anchor and footnote fallbacks and bottom spacing run once
  (final call). The indent check sits inside `resolveFirstLineIndent`, which every
  caller uses; the table-caption probes run on copies. The continuation justifies
  normally.
- **Redaction blocks (`587d26f5d`):** measure and draw all use `getGlyphMetrics`
  (`getTextBounds`, both `getTextAdvanceX` branches, `getTextInkBounds`,
  `drawText`, both rotated draws, `renderCharScaled`, `renderCharImpl`); the
  remaining `getGlyph` sites are unrelated. Both rotation rectangles map
  correctly. At scale 2 and 3 the hi-res companion is skipped, so the rectangle is
  drawn in logical 1x units on the base face, matching the layout measure with
  chunkier edges. It draws in the BW pass only, as the notdef box does. It never
  overrides a face that has the glyph and fires only for U+2588 and U+25A0.
- **UI fallback (`811fe3460`), apart from F4:** all six resolver callers plus
  `prewarmFallbackText` use the same `resolveTextFontId`, so measure and draw
  agree. Cost is one interval search per non-ASCII codepoint, with no allocation
  and no SD access. The reader's own font ids have no fallback entry.
- **Gauge and SDK (`d60f30da0`), apart from F3:** step 0 writes only when Design
  Capacity is 3000 or Full Charge Capacity is more than a quarter above 650; a
  healthy gauge gets two or three reads. It runs once per boot. X4 and Picco
  return false at once (`designCapacityMah` is 0), and the simulator is guarded
  (`ec15eeb71`). The SDK diff against `119f3ed` is additive: `enterShipMode`,
  `configureCharger` and `isExternalPowerPresent` are intact. The low-battery
  guard sustains 120 s, longer than the gauge window.
- **Version scoping (`d7f4858d2`), apart from F1:** every user of the macro names
  it, the one header user (`ColophonData.h`) is in `VERSION_MARKERS`, and
  `lib/hal/HalSystem.cpp` compiled in the `default` gate, so the middleware does
  reach libraries. Only `default` and `gh_release_rc` get the define; every other
  env sets an ini literal. I found no change to what makes PlatformIO wipe build
  directories.
- **Sub-chapter list (`cc282e1d6`), apart from F5:** anchor pages are 0-based and
  compared with `<=`, so a chapter start selects the right entry; a section with
  no anchors, or none reached, keeps the spine's first entry; several entries at
  one page choose the later index; partials read in-build anchors only. No cache
  bump is needed: it reads only the existing anchor map.
- **`docs/file-formats.md` rows 81-83** match the code (`SECTION_FILE_VERSION` 81,
  82, 83; "binary layout unchanged" holds).

## Not done

- No builds, tests or device checks; `gh_release_rc` was not built to confirm F1.
- No Hebrew or RTL probe of the full line breaker, and no scale-2 or scale-3 pixel
  check of the block draw.
- No heap measurement of F4's extra UI font sizes.
- `findAnchorDuringBuild` inside `getTocIndexForPage` during a live build was read,
  not exercised; a spine item at the end-of-book position was not examined.

## Follow-up: the read-aloud fix itself (`f262d9b34`)

The F2 fix grew past the capture: the layout now records, per line, whether its
end hyphen was inserted (`TextBlock::lineEndJoin`, one serialized byte, folded
into section v83). The same reviewer read that one commit, by reading only.
No defects. Checked clean: the byte's writer and reader are the only two sites
and every section path goes through `TextBlock::deserialize`, which clamps an
out-of-range value; `wordHyphenInserted` is kept parallel to `words` at every
insert and erase (all splits go through `splitWordAt`); the join rule for an
inserted hyphen, a source hyphen, an em dash, a hyphen before a space and a
paragraph's last line; nothing per word is serialized or kept after layout.
Two notes, neither a regression: a build of the unpushed pre-fix v83 would
leave caches one byte short per line (none was ever flashed or pushed, so the
version stays 83); and a word split across a PAGE boundary is still not
rejoined in speech, as before this batch.
