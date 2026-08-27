# "L size inknut is missized" — the 2x tier that was built at 1x

*Written 2026-08-26 against firmware `2508a1eb4` and the simulator working tree.
Every number here is measured out of a shipped `.cpfont` or off a rendered
frame; nothing is derived.*

Owner report, with a screenshot: **InknutJunicode at the L slot on a dark page,
every character separated by a gap** — "In defense of the potato" setting as
`I n  d e f e n s e  o f  t h e  p o t a t o` — and lines unusually tall.

## The answer in one line

`build/seedfonts/InknutJunicode/2x/InknutJunicode_14.cpfont` — the file the iOS
bundle ships as the L slot's 2x companion — **was a 14 ppem render, not a
28 ppem one.** It is the 2x cut of the **7 pt** slot, left under the wrong
filename by a build that failed before it could rename its output.

## Why that draws gaps rather than small text

The advance grid and the glyph bitmap come from **different files**, and only
one of them was wrong.

`GfxRenderer::renderTextInternal` (`lib/GfxRenderer/GfxRenderer.cpp:820-850`)
accumulates the pen from the **base 1x font's** `advanceX`, then multiplies:

```cpp
lastBaseX += fp4::toPixel(prevAdvanceFP + kernFP);   // logical px, 1x font
...
const int devX = lastBaseX * renderScale();          // device px
renderCharImpl<TextRotation::None, true>(*this, renderMode, *hiRes, cp, devX, devY, ...);
```

So the spacing lattice is correct — 1x metrics doubled — and the ink dropped
onto it is whatever the companion holds. A companion rasterised at half the
ppem puts a half-width glyph in every full-width cell. Measured on the L slot
(14 pt, render scale 2), bitmap width against the device-pixel advance it is
given:

| ch | 1x advanceX | advance grid, device px | shipped bitmap w | fill | corrected bitmap w | fill |
|---|---:|---:|---:|---:|---:|---:|
| a | 18.125 | 36 | 17 | 0.47 | 32 | 0.89 |
| e | 18.750 | 38 | 15 | 0.39 | 30 | 0.79 |
| m | 30.312 | 60 | 29 | 0.48 | 57 | 0.95 |
| n | 21.000 | 42 | 20 | 0.48 | 38 | 0.90 |
| o | 19.562 | 40 | 16 | 0.40 | 32 | 0.80 |
| H | 27.812 | 56 | 26 | 0.46 | 51 | 0.91 |
| I | 12.688 | 26 | 11 | 0.42 | 21 | 0.81 |
| M | 31.375 | 62 | 29 | 0.47 | 58 | 0.94 |
| **21 letters, summed** | | **838** | **380** | **0.45** | **743** | **0.89** |

**0.45 against 0.89 — the ink filled exactly half its advance**, and the other
half is the gap the owner saw. The doubled word space is the same arithmetic
applied to `U+0020`, which is why word boundaries read as double gaps.

The tall lines are the same fault on the other axis. The line advance is
`45 logical -> 90 device px` from the base font; the shipped companion's own
`advanceY` was **45**, the corrected one is **90**. Glyphs half as tall, in a
line box sized for full ones.

The owner's "the glyphs themselves look correctly sized" is the one part that
reads as a miss and is not: at 2x the panel presents at ~1:1 on a phone, and a
14 ppem letter in a 28 ppem grid is a legible letter — it looks like a smaller
size, not like a broken one. The headline of his report is the accurate part:
*missized*.

## The mechanism, from the top

1. `EpdGlyph.width`/`.height` are `uint8`, so a glyph over 255 px cannot be
   written and `fontconvert_sdcard.py:1265-1281` raises rather than truncating.
2. InknutJunicode's italic carries `scale: 1.2`, so its em is wide. **U+2E3B
   THREE-EM DASH rasterises 289x5 px at 32 ppem** — 2x of the 16 pt slot.
   Reproduced this session, verbatim:

   ```
   ValueError: 1 glyph(s) rasterise over the 255 px limit of EpdGlyph's uint8
   width/height: U+2E3B (289x5 px).
   ```

   It arrived when the family moved to `reading` intervals on 2026-08-20, whose
   `U+2E00-2E7F` block `latin-ext` never reached.
3. **`build-sd-fonts.py --only InknutJunicode --scale 2` therefore could not
   succeed at all** without `--drop-codepoints 0x2E3B`.
4. `rename_to_slot_names()` runs **only on success**
   (`build-sd-fonts.py:788,805` before this fix). fontconvert names each file
   for the ppem it rasterised, so the five sizes that built before the abort
   stayed as `_14`, `_18`, `_20`, `_24`, `_28` — the 2x cuts of 7/9/10/12/14 pt.
5. **One of those names collides with a real slot.** `2 x 7 = 14`, and 14 pt is
   itself a slot in this ramp. `InknutJunicode_14.cpfont` in the `2x/` directory
   is exactly what `SdCardFontManager::hiResCompanionPath`
   (`lib/EpdFont/SdCardFontManager.cpp:32-36`) looks for, so it loaded, with no
   error anywhere:

   ```
   [SDMGR] Loaded hi-res /fonts/InknutJunicode/2x/InknutJunicode_14.cpfont
   ```

The other four orphans were unreachable dead weight; `_32` never existed; and
the 7 and 9 pt slots had no companion at all, so those two rendered
1x-replicated (blocky, but the right size — which is the fallback working).

## Why the knowledge was in the wrong file

`scripts/install-sim-fonts.py` **already knew**, and had since 2026-08-20:

```python
FAMILY_TIER_DROPS = {"InknutJunicode": {2: ("0x2E3B",), 3: ("0x2E3A",)}, ...}
```

But that script writes the simulator's **card** (`fs_/fonts/`) and nothing else.
The tree the iOS app bundles is `crosspoint-simulator/build/seedfonts`, and the
documented way to fill it (`docs/ios-app-size.md`) is a bare loop over
`build-sd-fonts.py --scale $sc`, which never comes through
`install-sim-fonts.py` and so had no way to know. **The card was correct the
whole time and still is** — audited this session, all eight families, both
tiers, filenames and scale ratios. Only the bundle tree was wrong.

That is the actual defect: one table, two callers, and the knowledge parked in
the caller that did not need it.

## Is it a regression from the 2026-08-26 ramp change?

**Yes, for the L slot.** Before that day `build/seedfonts/InknutJunicode/2x/`
held 10/12/14/16 from 2026-08-17, all at the correct 2x scale — measured
`advanceY` ratios 2.00 — so L rendered correctly. The rebuild that added the
XS/XXS sizes overwrote `2x/_14` with the 7 pt slot's cut.

**No, for the underlying overflow.** The old ramp `[10, 12, 14, 16]` also
reached 32 ppem at 2x, which is why the drop table existed before this. What
the new ramp added is the *collision*: 7 pt is the first slot whose doubled ppem
lands on another slot's number, which is what turned a merely incomplete tier
into a wrong-file-loads tier.

## Which other families share it

**None.** Audited by reading every shipped file, both trees:

| Tree | Result |
|---|---|
| `crosspoint-reader/fs_/fonts/` (the card), 8 families, 2x + 3x | clean — every slot present, every `advanceY` ratio within 0.12 of its tier, no orphans |
| `crosspoint-simulator/build/seedfonts/`, 8 families, 2x | clean except InknutJunicode |

Almendra, Coelacanth, Edgar, LibreFranklin, LibrisADF, TeXGyreHeros and
TeXGyreSchola all measured 1.96-2.05 at every slot. Recorded here so the next
pass does not re-read them.

Two things were found alongside and are *not* this bug:

* The three surviving 2026-08-17 files in the bundle tree (`2x/_10`, `_12`,
  `_16`) carried **1094 glyphs against the 1x set's 2693** — the pre-`reading`
  coverage. Right scale, stale charset, so ~1600 codepoints rendered
  1x-replicated at those slots. The rebuild replaces them.
* The card's Almendra is still the old `6/8/10/12/14/17` ramp, superseded by
  `2508a1eb4`'s `8/10/12/14/16/18`. Internally consistent, so not this bug —
  but reprovisioning is owed.

## The fix

**Data.** `build/seedfonts/InknutJunicode/2x/` rebuilt with the drop, six slot
names, no orphans. **Installed size 19,934,593 -> 20,282,085 bytes, +347,492
(+1.7%)** — eight files become six: four stale or misnamed ones replaced, four
unreachable orphans deleted, and the two slots that had no companion at all
gained one. (An earlier draft of this line said +13.0 MB; that was the
four-file A/B arm's directory misread as the shipped one.)
The one deliberate loss is U+2E3B in this family's 2x tier, which now falls back
to the 1x-replicated glyph — a resolution loss on one character, and the only
alternative is not shipping the tier.

**Cause, three changes.**

1. **The drop tables move into the recipe.** `sd-fonts.yaml` gains a top-level
   `tier_drops:` (the global layer, `{3: [0x2E3B]}`) and a per-family
   `hires_drops:` (InknutJunicode `{2: [0x2E3B], 3: [0x2E3A]}`, Coelacanth
   `{3: [0x261C, 0x261E]}`). `build-sd-fonts.py` unions them **per family** for
   whatever `--scale` it was given, so every caller gets them — including the
   bare loop that fills `build/seedfonts`. `install-sim-fonts.py` keeps no copy,
   and no longer has to split a tier into one invocation per drop set, since
   `--drop-codepoints` is no longer how the drops travel.
2. **A failed build no longer leaves loadable garbage.** `build_family` snapshots
   the `.cpfont` names present before it runs and deletes exactly what it
   created on any failure path (non-zero exit, timeout, exception). Deleting by
   pattern would have taken out a good file from an earlier build; this is the
   only precise version.
3. **A gate, not a paragraph.** `rename_to_slot_names()` now returns a reason
   instead of `None`, and fails the family when any slot has no file under its
   1x name, or when a file **this run made** survives under a name no lookup can
   reach — the two conditions a half-finished tier violates and nothing
   downstream checks. A *pre-existing* non-slot file is only WARNED about: it is
   a stale cut from an older ramp rather than this build's doing, and failing on
   it would reject a legitimate ramp change and then discard the good output
   along with it. The rename is also two-phase now: it worked before only
   because `sizes:` happens to be ascending, and the 7-vs-14 collision is
   exactly the case that made that accidental.

## What was NOT changed, and why

* **The renderer.** Reading the pen from the base font and the ink from the
  companion is correct and is what makes a *missing* companion degrade
  gracefully. The bug was a wrong file, not a wrong policy.
* **`crosspoint-simulator/docs/ios-app-size.md`**, whose recipe block taught the
  drop-free loop. That repo was writable for `build/seedfonts/` only this
  session; the correction is owed there and is the one loose end.

## Delivered

Rendered proof (before / after / a healthy reference family, lossless PNG at
native panel pixels):
<https://claude.ai/code/artifact/85b1e070-fa0f-4e21-bda7-c790cead757e> — repair
that page in place rather than publishing a second one.

## Verified

* `ctest` 570/570 (2 disabled sweeps, as always).
* `pio run -e default` SUCCESS.
* Rendered A/B through the real firmware at `CROSSPOINT_RENDER_SCALE=2`, both
  arms identical but for the one file: same book, same page, same line breaks
  (pagination is done in the 1x font, which never changed), `GRAIN_SEED` pinned,
  `.crosspoint` reset between arms. Effect delta before-vs-after over the text
  block: mean 20.5 levels, max 192, 12.1% of pixels moved by more than 4.
* **The cause fix, fault-injected.** The exact build that failed
  (`--only InknutJunicode --scale 2`, no hand-passed drop) now succeeds and the
  collision resolves: `_7` = 1,379,937 B (14 ppem), `_14` = 4,681,367 B
  (28 ppem), `_16` = 6,039,408 B (32 ppem — the size that could not be built at
  all before). With the drops stripped back out, the failure path deleted the
  partial it had made and left a planted pre-existing file untouched. A stale
  non-slot file warns and does not fail, and the good output survives it.
* **Not confirmed on a phone.** No TestFlight build in this session. The fix is
  a file on disk and the render is the real firmware's, but nobody has seen it
  on glass.

## Both loose ends closed, same day

Written later on 2026-08-26, against `crosspoint-simulator` at `1b9b787`.

**1. There is now a mechanical gate on the shipping tree.** The section above
records that every gate we had passed this build and a human looking at a page
was the only thing that caught it. `crosspoint-simulator/tools/validate_seed_fonts.py`
is the answer to that: it reads each `.cpfont`'s own 32-byte header and style
TOC and refuses a tree where a hi-res tier's `advanceY`, `ascender` or
`descender` is not that tier's multiple of the 1x base — plus a missing
companion, an orphan name, a non-ascending ramp, a stale charset, a style set
that disagrees, and a 1x ramp that disagrees with `sd-fonts.yaml`'s `sizes:`.
No rasterizer, no rendering, 0.11 s for the eight-family tree. It runs at iOS
configure time (un-skippable — every iOS build configures) and again as a named
section in `ios/testflight.sh` (visible — the deploy sends cmake's stdout to
`/dev/null`). No override flag.

Fault-injected against the real tree: the exact broken file reconstructed by
copying the 7 pt 2x cut over the 14 pt 2x name is rejected with
*"THIS FILE IS NOT A 2x RENDER OF 14 pt. styles 0,1,2,3 advance_y 45 where a 2x
cut needs ~90 (1x reads 45; ratio 1.000, not 2.000)"*, exit 1, naming the path.
The tolerance is 3 px at 2x against a worst real rounding of 1, measured across
all eight families and all four styles. Writeup:
`crosspoint-simulator/docs/seed-font-integrity-gate.md`.

**2. The card's Almendra is reprovisioned.** `python3 scripts/install-sim-fonts.py
--families Almendra` rebuilt all three tiers on the superseded
`6/8/10/12/14/17` ramp and pruned the two stale slots from each:

```
  pruned stale fonts/Almendra/Almendra_17.cpfont
  pruned stale fonts/Almendra/Almendra_6.cpfont
  installed Almendra 2x: 6 sizes ... 3x: 6 sizes ... 1x: 6 sizes
```

`fs_/fonts/` now passes the gate clean at **1x, 2x and 3x, all eight families**.
Before the rebuild it failed on exactly one thing — Almendra's ramp — which the
gate found on its own, having been told nothing about it.

**3. `crosspoint-simulator/docs/ios-app-size.md` is corrected**, which the
section above called the one loose end. Its recipe block already carried the
drop-table correction; what was still stale was "every tier the app can render
— 1x, 2x and 3x, since `renderScale` defaults to 3" (the ceiling has been 2
since 2026-08-23), and it now points at the gate.
