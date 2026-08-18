# Auditing matcha-reader's heap series against this fork

`eszter007/matcha-reader` is the first fork named in [T-014](../TODO.md), and
the owner ruled on 2026-08-17 that its heap/OOM work is what to mine first. This
file is what that audit found, **including the parts that do not apply**, which
is the half a chat summary drops and the half that stops the same six commits
being re-proposed every month.

Audited 2026-08-18 against `main` at `2c17295fc`, matcha at `matcha/master`.
Each verdict says whether it was verified against source or inferred.

## First, a correction to T-014's framing

T-014 says matcha is "only 31 behind upstream", and reads as though that makes
its patches close to ours. It does not. Measured:

```
$ git rev-list --left-right --count main...matcha/master
517   673
```

**517 ahead, 673 behind.** "31 behind upstream" is a statement about matcha's
distance from *upstream*, not from this fork. The two forks have diverged in
different directions — matcha builds vertical writing, StarDict dictionaries and
a reading-history subsystem this fork has never had, so a patch of theirs is as
likely to touch code we deleted as code we share.

## The six named items

| # | Commit | Concern | Verdict |
|---|---|---|---|
| 1 | `f6df3e35c` | per-call allocation out of `HalStorage::hasContent` | **N/A — verified** |
| 2 | `d8b220990` | EPUB indexing heap pressure | **mostly N/A — verified** |
| 3 | `f07d737d0` | history bounded by memory, not a corruption guard | **N/A — verified** |
| 4 | `16d4dd225` | cover thumbnails without HAL power locks | **N/A — verified** |
| 5 | `2ea69f77a` | reader-settings OOM | **already the shape we have — verified** |
| 6 | `07b35f966` | font cache freed before settings save | **open candidate — inferred** |

### 1. `hasContent` — N/A

There is no `hasContent` in this fork. `grep -n hasContent lib/hal/*.h
lib/hal/*.cpp` returns nothing; it is a method matcha added for its own Library
scan. Nothing to port.

### 2. EPUB indexing heap pressure — mostly N/A

Both hunks raise a 64 KB gate to 96 KB, and both sit on vertical-writing code:
`lib/Epub/Epub/VerticalSection.cpp`, and `SILENT_VBUILD_MIN_ALLOC` in
`silentIndexNextChapterIfNeeded()`. This fork has neither — no
`VerticalSection.*` in `lib/Epub/Epub/`, and no `silentIndex` anywhere in
`EpubReaderActivity.cpp`.

The third hunk moves the `extractItemToFile` chunk-size threshold from 64 KB to
96 KB of max-alloc heap. Our `Epub.cpp` has no `getMaxAllocHeap` call at all, so
even that one does not apply as written.

**What is worth taking from it is the question, not the patch:** our
`EpubReaderActivity` gates on `ESP.getMaxAllocHeap()` in three places
(`:226`, `:269`, `:1694`) with three different constants, and nobody has
re-measured them since. That is our own item to raise if it is ever worth
raising — it is not this commit.

### 3. History bounded by memory — N/A

The bug is real and nasty in its own tree: `reserve()` sized from an on-disk
count, with `-fno-exceptions`, so a corrupt count calls `abort()` and the device
boot-loops. It needs a reading-history subsystem to be in. `grep -rl
"languageDays\|BookStats\|ReadingHistory" src lib` returns nothing here.

Worth noting the general lesson, which our CLAUDE.md already states: a sanity
guard against corruption is not a memory limit, and `reserve()` from untrusted
data is an `abort()` waiting to happen. If reading stats ever land here, size
the caps by what the device holds.

### 4. Cover thumbnails without HAL power locks — N/A

Touches `HalPowerManager` (which we have) and `RecentBooksActivity` (which we
have) — but the thing being fixed is not present: `grep -n
"PowerLock\|powerLock\|thumbnail\|Thumb" src/activities/home/RecentBooksActivity.cpp
lib/hal/HalPowerManager.h` returns nothing. We do not generate library cover
thumbnails on that path, so there is no lock to take off it.

### 5. Reader-settings OOM — we already do this

matcha's fix "filters embedded settings before copying the shared list so
opening Reader Settings does not require one large contiguous heap block."
`SettingsActivity::rebuildSettingsLists()` here already filters **before** the
copy — `if (setting.category != StrId::STR_CAT_SYSTEM) continue;` precedes the
`push_back` (`src/activities/settings/SettingsActivity.cpp:51-53`). Same shape,
arrived at for a different reason (the withdrawn Display/Controls/Reader tabs).

One genuine nit visible while checking: `deviceSettings` is filled by
`push_back` with no `reserve()`, against CLAUDE.md's rule 7. It is ~32 entries,
so this is fragmentation hygiene, not a crash — small enough that it belongs in
a passing cleanup rather than its own PR.

### 6. Font cache freed before settings save — the one still open

matcha reclaims renderer-owned font tables before serializing settings, on the
grounds that a font preview can leave too little contiguous heap to write
`settings.json`.

This fork calls `SETTINGS.saveToFile()` from five places in `SettingsActivity`
(`:126`, `:277`, `:314`, `:341`, `:374`) and has no `unloadAll` /
`clearSdCardFonts` anywhere in that file. So the shape matcha guards against is
*possible* here.

**Marked inferred, deliberately.** Nobody has measured the contiguous heap at
those save points on this fork, and no bug report describes a failed settings
save. Porting it on the strength of matcha's reasoning would be adding a free +
reload to a path that may have plenty of room. The next step, if this is picked
up, is a measurement — log `ESP.getMaxAllocHeap()` immediately before
`saveToFile()` with SD fonts resident — not a patch.

## What this means for T-014

Five of the six named items are closed by this audit: four do not apply to code
this fork has, and one is already implemented here by another route. One remains
and needs a measurement before it needs a patch.

**The entry's premise was too optimistic**, and that is the finding worth
carrying forward: forks diverge in *what they contain*, not only in how far
behind they are, so the next fork on the list (CrossInk, folio, crossmux,
crosspoint-jp, cpr-vcodex) deserves the same "does this code even exist here"
pass before any of its commits are read as candidates.
