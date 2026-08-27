# Font size slots: six names, an unbounded ramp

*2026-08-27. Written from the owner's report — "XXL is shown as XXS and XXL to
inknut becomes XL in reading mode" — and the audit it triggered.*

## The model

The reader stores a **slot**, `SETTINGS.fontSizeSlot`, not a point size. The slot
is family-independent, which is what lets a size survive a font switch. The point
size it resolves to comes from whatever that family has **installed on the card**
— `readerFontPointSizes()` → `SdCardFontFamilyInfo::availableSizes()`, which
lists files, not the recipe.

That is the whole tension. There are exactly **six names** (XXS XS S M L XL,
`READER_FONT_SLOT_COUNT`), but the number of installed sizes is whatever is on
the card, and both steppers clamp to `sizes.size() - 1` rather than to six. A
family with seven files therefore has seven reachable slots and six names.

**The extra slots stay reachable on purpose.** A user-built family, or one
installed over WebDAV, may legitimately ship more sizes, and making an installed
size unselectable would be a silent loss of something the user put there.

## How a card grows a seventh size

Nothing on a real SD card prunes. When a slot's point size moves, the file the
old ramp vacated stays. Inknut Junicode's S slot went **10 → 11 pt** on
2026-08-27, so every card that already held a 10 pt Inknut now has seven files:
7, 9, 10, 11, 12, 14, 16.

The iOS app is the exception and prunes: `seedBundledFontFamilies()` either
symlinks the family directory straight at the bundle (so the card cannot drift
from it) or falls back to copy+prune. Only **bundled** families are pruned —
user-installed ones are left alone, which is correct.

## The bug

The slot NAME was written out twice, and the two copies disagreed about the case
that matters:

| site | out-of-range slot resolved to |
|---|---|
| `SettingsList.h` | the **last** name — "XL" |
| `FontSelectionActivity.cpp` | **index 0** — "XXS" |

So on a card with seven Inknut sizes, the top slot read "XL (16pt)" on the
settings row and **"XXS"** on the font preview. That is the report, exactly.

The direction is what makes it more than an imprecision. A slot past the end is a
slot too **large**; answering it with the smallest name is backwards, and a
control that names the biggest size "XXS" is one a reader stops trusting.

## The fix

One definition, `readerSlotLabel()` in `src/ReaderFontSizes.h`, used by both
screens and therefore by the web settings API too:

- a slot past the end clamps to the **largest** installed size, never the first;
- a family whose installed count is not exactly six gets **point-size-only**
  labels ("20pt"), because the seventh slot is not "XXL" — it has no name, and
  borrowing one is what produced the bug.

`test/reader_slot_label/` pins both properties plus the absence of a second name
table, and is mutation-verified: restoring either the clamp-to-zero rule or the
always-name rule fails a named check.

## What is NOT a bug, and stays

**Switching to a family with fewer sizes shows a smaller slot.** At slot 6 on a
seven-size family, switching to a six-size Inknut renders slot 5 — "XL". There is
no seventh Inknut size to show, so clamping is the only honest answer.

It is **not destructive**: `fontSizeSlot` is the persisted truth and the switch
does not rewrite it (`SdCardFontSystem.cpp` says so at its head, and means it).
Switching back restores the original slot. What was wrong was never the clamp —
it was that the phantom slot had been given a *name* it had no right to, so it
looked like a real size that then vanished.

## If the extra sizes are unwanted

Deleting the orphan is a card edit, not a code change, and it is deliberately not
automatic: pruning sizes off a card would delete files a user may have installed
themselves. `tools/validate_seed_fonts.py` reports orphans against the recipe for
the seed tree, which is where this one was caught before shipping.
