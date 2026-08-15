# Open work

Things to do that are not defects. Defects live in [BUGS.md](BUGS.md); this file
is for work that was asked for and has not landed.

It exists for the same reason `BUGS.md` does: todos were being carried in chat,
where they survive only as long as the session does.

Format: `### [T-NNN] Title` then what it is, why, and what "done" looks like.
An item leaves this file when it ships or when it is ruled out — not when it is
started.

## Where the rest of the work lives

Four trackers across two repos. Run `scripts/tracker-check.sh` for all of them
with open counts and the next free id — do not hand-pick an id, it has produced
a duplicate `T-009` and a `T-001` in two files.

| Tracker | Ids | Holds |
|---|---|---|
| **TODO.md** (this file) | `T-` | Firmware work that is owed |
| [BUGS.md](BUGS.md) | `B-` | Firmware defects |
| `../crosspoint-simulator/TODO.md` | `ST-` | Simulator work that is owed |
| `../crosspoint-simulator/BUGS.md` | `S-` | Simulator defects |

Each tracker holds only its own prefix. Not tracked as numbered items: the
upstream backlog ([docs/fork-sync.md](docs/fork-sync.md)) and the sibling-fork
candidates ([docs/fork-ecosystem.md](docs/fork-ecosystem.md)).

**If it only exists in a chat transcript, it is already lost.** That is how this
file came to exist, how the simulator's did, and how T-017 and T-018 nearly went
missing on 2026-08-15. File it the same turn you find it.

---

## OPEN

### [T-019] Withdraw three more Settings rows
**scope: device Settings UI · ruled 2026-08-07 · not started**

*Moved here from `BUGS.md` on 2026-08-15, unchanged. It was filed there as
`[T-001]` because at the time "there is nowhere else it would be found" — this
file did not exist yet. That left two different items both called `T-001`.*

Remove from the device Settings screen:

| Row | Field | Key | Defined at |
|---|---|---|---|
| Clock Format | `clockFormat` | `clockFormat` | `src/SettingsList.h:463` |
| Sleep Screen Cover Mode | `sleepScreenCoverMode` | `sleepScreenCoverMode` | `src/SettingsList.h:417` |
| Sleep Screen Cover Filter | `sleepScreenCoverFilter` | `sleepScreenCoverFilter` | `src/SettingsList.h:419` |

**Do it the withdraw way, not the delete way.** All three are plain
`SettingInfo::Enum` rows with a `valuePtr`, so deleting the entry from
`getSettingsList()` would stop the field being written by `toJson()` at all and
drop it from the web settings API — the trap CLAUDE.md documents and that this
fork has already been bitten by. The procedure, same as the System font row on
2026-08-07 (`169540d2`), is three steps and all three are needed:

1. Change `category` from `STR_CAT_SYSTEM` to `STR_CAT_DISPLAY`, a retired
   category `rebuildSettingsLists()` drops. The row keeps persisting and stays
   web-settable.
2. Pin the value in `CrossPointSettings::normalizeRetiredSettings()`, so a save
   written while the picker existed cannot hold the old choice forever.
3. Make the field initialiser in `CrossPointSettings.h` match the pin, or fresh
   installs and upgraded ones disagree — pinning alone is a half-fix.

Decide the pinned value per row before starting; the current initialisers are
`clockFormat = 0` (24-hour), `sleepScreenCoverMode = FIT`,
`sleepScreenCoverFilter = NO_FILTER`.

**Verify by save cycle, not by reading the file after boot.** `normalize` fixes
the in-memory value and nothing has called `saveToFile()` yet, so a
read-back-after-boot check passes whatever you do. Seed the old value, boot,
enter Settings and press Back (which saves), then read the file.

### [T-017] Light sleep (#2525) is on main and unconfirmed on device
**scope: verification · opened 2026-08-15**

`ade9dac91` carries upstream #2525 — light-sleep between input polls when idle,
a two-stage idle backoff (downclock at 500 ms, sleep at 1 s, replacing the flat
3 s window), and a CPU downclock during the e-ink BUSY wait. Upstream measured
it on an X3 with a Nordic PPK2: idle 9.68 mA → 2.78 mA, session average
~11.6 → ~3.6 mA, which is the ~3.2x active-reading-time figure.

**None of that is confirmed here.** Host evidence only: five environments build
from a cold cache (`default` RAM 16.5%, Flash 64.4%), ASAN clean, and a headless
session boots, opens a book, idles past both thresholds and turns pages. Power
and felt timing cannot be observed off-device at all.

Its stated dependency on freeink-sdk PR#8 is already satisfied —
`setBusyWaitSliceHook` exists in the pinned SDK (`FreeInkDisplay.h:260`), so no
submodule bump is owed.

**What to watch on device:** input latency after the device has been idle;
whether page turns still feel immediate; and that USB serial still works while
connected, since light sleep is deliberately suppressed on USB (it kills the CDC
link).

**Done looks like:** confirmed on an X3 or X4, or a reported regression with
enough detail to trace. Separate from [T-008], which covers the 2026-08-06
backlog; this is one named commit.

### [T-018] The SDK pin is 9 commits off upstream, on a branch, going nowhere
**scope: submodule · opened 2026-08-15**

`freeink-sdk` is pinned to `fix/keyboard-alt-hint-dedupe` at `159f40f` — checked
against the gitlink at `ade9dac91`, the pin and the branch tip are the same
commit, so this branch is load-bearing, not a leftover.

It is **9 ahead / 41 behind `upstream/main`**, with no PR open. Nine keyboard-icon
commits (Space/Return strokes, the alt-hint band reserved per row, a one-cell Ok
key drawing a return arrow) exist only on this fork. The local `main` in the
submodule is 137 behind and 0 ahead — purely stale, contributing nothing.

Doing nothing is the expensive option: the gap grows, and every upstream SDK fix
lands further out of reach.

**Two ways out, owner's call:** open the PR upstream and let the commits land,
or rebase the nine onto current `upstream/main` and re-pin the submodule. Either
way `scripts/bump-submodule.sh` is the mechanism ([T-006a]).

**Done looks like:** the pin sits on something that is not diverging, and the
nine commits are either upstream or deliberately fork-only with that recorded.

### [T-012] A setting for tables: flat or tabular
**scope: reader · opened 2026-08-15**

Today tables are always flattened. `ChapterHtmlSlimParser.cpp:481-519` tracks
`tableDepth` and handles `thead`/`tr`/`td`/`th`, but there is no `colspan`
handling and nothing draws a border — verified by grep, no hits. PR #10 added
header labels to flattened cells, which is the current best behaviour.

Wanted: an owner-facing choice between the flattened reading order and a real
tabular render. `uxjulia/CrossInk` has both halves already (#89 table rendering,
#90 colSpan as header/footer rows) — see
[docs/fork-ecosystem.md](docs/fork-ecosystem.md).

**Traps before writing the setting** (`docs/` + CLAUDE.md rules, all of which
have bitten before):

- An ENUM row persists its **index**. Two values only, so a bool row is simpler
  and safer; if it becomes an enum, values APPEND — inserting one re-points every
  saved `settings.json` at a different choice.
- Anything that changes layout **must bump the `section.bin` version**
  (currently 35) or stale caches render the old shape. See
  `docs/file-formats.md`.
- A row with no `valuePtr` does not persist; if it becomes a getter/setter row,
  add explicit lines to BOTH `toJson` and `fromJson`.

**Done looks like:** the setting exists, both modes render correctly on a real
table-bearing EPUB, and switching it invalidates the layout cache.

### [T-013] A setting for 1-bit or 2-bit chrome fonts
**scope: display · opened 2026-08-15**

The 2-bit chrome font work (merged `1a18de260`) traded flash for a UI that can
be antialiased at all. `folio-etc/folio` went the other way and packed bundled
theme `.cpfont`s to 1 bit. Both are defensible; make it the owner's choice
rather than a build-time ruling.

Needs a measured flash delta for each mode before the setting is worth having —
if the saving is small the setting is not worth its own complexity, and that is
a legitimate outcome to record here.

Interacts with the `_2x`/`_3x` hi-res companions and `CROSSPOINT_RENDER_SCALE`,
so read `all.h` and `applySystemFont()` in `main.cpp` first. Note
`insertFont()` refuses to overwrite — a live change must use `replaceFont()`,
or the setting will read as changed while every pixel stays on the old face
until reboot.

**Done looks like:** the setting exists, the flash cost of each mode is
recorded, and changing it re-binds the chrome faces without a reboot.

### [T-014] Sibling-fork improvements, as reviewable PRs
**scope: upstream-adjacent · opened 2026-08-15**

From [docs/fork-ecosystem.md](docs/fork-ecosystem.md), which surveyed 1,443
forks. Bring the worthwhile work across as **several small PRs for review** —
one concern each, not one large drop. Per `docs/fork-sync.md`: per commit, live
hunks only.

Named forks, in the owner's order:

| Fork | What to mine |
|---|---|
| `eszter007/matcha-reader` | The heap/OOM series — EPUB indexing pressure, reader-settings OOM, font cache freed before a settings save, per-call allocation out of `HalStorage::hasContent`, history bounded by memory, cover thumbnails without HAL power locks. Richest vein, and only 31 behind upstream. |
| `uxjulia/CrossInk` | Table rendering + colSpan (feeds T-012); progressive JPEG cover support. Skip themes, carousels, tilt and touch. |
| `folio-etc/folio` | 1-bit `.cpfont` packing (feeds T-013); font budgets during heavy activities; power-button claiming and button-hint rendering (feeds T-011). |
| `0x1abin/crossmux` | Low-heap crash fixes — EPUB footnote allocation, web settings heap exhaustion. Skip the Apps hub and mini-games. |
| `zrn-ns/crosspoint-jp` | The `abort()` on page-turning very long CJK paragraphs. Skip vertical writing. |
| `franssjz/cpr-vcodex` | Progressive EPUB indexing. Skip KOReader profiles, flashcards, dashboards. |

**Do not re-propose** the three already checked and present: HTML 4.01 entities
(all 252 in `htmlEntities.cpp`), `<hr>` (handled at
`ChapterHtmlSlimParser.cpp:601` and `:992`), the hidden-file toggle
(`FileBrowserActivity.h:31`).

**Done looks like:** one branch + PR per concern, each building `-e default`
AND `-e simulator` from a cold cache, each stating what was verified on host
and what still needs the device.

### [T-015] Pull in the progressive JPEG decode fix, if it is real
**scope: reader · opened 2026-08-15**

Two independent forks hit the same area: `matcha-reader` has "decode
progressive JPEGs whose DC scan is non-interleaved", `CrossInk` has "improve
progressive jpeg cover support". Two forks converging is decent evidence the
bug is real.

**Conditional on its own merit** — read both diffs against
`lib/Epub/Epub/converters/JpegToFramebufferConverter.cpp` first and find a book
whose cover actually reproduces it. If it cannot be reproduced here, record that
and close the item; a decoder change with no failing case is not worth the risk.

**Done looks like:** either a reproduction plus the fix plus the cover rendering
correctly, or a written finding that our decoder already handles it.

### [T-016] READMEs no longer describe what these repos are
**scope: docs · opened 2026-08-15**

Both `crosspoint-reader` and `crosspoint-simulator` have drifted a long way from
their READMEs. The firmware fork has deleted whole subsystems (KOReader sync,
Calibre, the status bar, bookmarks, auto page turn, the reader menu, four of
five themes) and added others (Manage Files, notes, summarization, the keyboard
redesign, 2-bit chrome). The simulator has grown an iOS target, a read-aloud
channel, host text entry and pad-contrast presets.

A README that advertises removed features is worse than a thin one: it is the
first thing a new contributor reads, and every wrong line costs someone a
session.

**Do both repos.** Check each claim against the tree before keeping it — the
rule is the same as everywhere else, no claim without a grep.

**Done looks like:** each README describes what its repo does today, names what
was deliberately removed and why (pointing at `SCOPE.md` / `docs/fork-sync.md`),
and no longer lists anything that is not there.

### [T-011] Side buttons are drawn on the UI — decide whether that is the intent
**scope: ui · opened 2026-08-15**

Owner observation from the simulator: the side buttons are now visualised on
screen, seen on the Claude screen. Recorded as asked; **the ruling is not made
yet**, and it splits two ways that want opposite work:

- **Intended** — then the question is coverage and consistency: which screens
  show them, whether the labels follow `MappedInputManager` when the front
  buttons are remapped or `sideButtonLayout` is swapped, and whether the reader
  screen's existing rotated hints are now duplicated by them.
- **Not intended on this screen** — then it is a leak from whichever component
  draws them, and belongs in `BUGS.md` instead of here.

Not investigated: which component draws them, or when it started. Both need
pinning down before anything is changed — see `docs/ui-conventions.md`, which
rules on which surface owns which affordance.

**Done looks like:** an owner ruling recorded, and either the coverage made
consistent across screens or the drawing removed where it does not belong.

### [T-008] Everything since 2026-08-06 is staged but unproven on hardware
**scope: verification · opened 2026-08-07**

Build, package and TestFlight are DONE. The device half is not, and the two keep
getting reported together, so this entry exists to keep them apart.

**Done 2026-08-07, from a clean tree at `91b4a8fc`:**

| Artifact | State |
|---|---|
| `gh_release` firmware | built, staged at `~/crosspoint-archive/staged/20260808T0201Z-crosspoint-91b4a8fc.bin`, sha256 `96f77816…` |
| Mac app (`CrossPointX3.app`) | packaged and `verify`-clean (all 3 purpose strings); NOT TestFlighted, per owner ruling |
| iOS TestFlight | **build-38 uploaded**, 19,545,915 bytes, delivery `d3e7a3ba` |

**Not done — needs the hardware in hand:**

- **The SD cards.** None were mounted, so `cpcards` never ran. They are still on
  `bb614f73`, roughly forty commits back. This is the only step blocked on a
  physical act.
- **Confirm on device**, since it will be in hand anyway: the seven Lucide icons
  on Home, the caret advancing a full space in Create Note, the clock preview on
  the offset screen, and the WebDAV update flow — that last one matters most,
  because its erase-write-reboot step is the ONLY part that cannot be exercised
  off-device at all.
- **Older debt, still unconfirmed:** power-off-while-typing (build-33+), iOS file
  transfer (build-37), B-018 back-nav, B-017's notes half.

**What this loop taught, worth keeping:** the iOS archive found a link error that
neither the device nor the desktop build could see — `SdFirmwareUpdateActivity`
is excluded from the iOS source set, and a new call into it from a shared TU only
fails when that target links. The iOS archive belongs IN the loop, not after it.
`xcodebuild -target CrossPointX3 -sdk iphoneos build CODE_SIGNING_ALLOWED=NO` is
the cheap version and needs no keychain.

### [T-007] Upstream issue #2863 reproduces here and is tracked nowhere
**scope: fork sync · raised 2026-08-06 · confirmed 2026-08-07**

The short-press power behavior upstream reports is present in this fork's code:
`lib/hal/HalGPIO.cpp:207` carries a `TODO` describing the same thing. Upstream
issues are not visible from this repo's tracker, so it would otherwise be
rediscovered rather than remembered.

**Close by:** decide whether the fork wants upstream's fix or its own behavior,
then either take the patch or write the divergence down in `docs/fork-sync.md`.

---

## Finished

### [T-010] settings.json / state.json writes are not atomic — FIXED
**scope: data durability · found by the 2026-08-08 P0 audit · fixed 2026-08-08**

Every settings and state save went through `SDCardManager::writeFile`, which
removes the target and then opens it `O_TRUNC` — so power lost in that window
left NO file, and the device booted having forgotten its Wi-Fi, reading position
and owner name.

Fixed at the fork's `PersistableStore`, not in the submodule. FAT cannot replace
a file in one step (SdFat's rename fails if the destination exists), so the order
is: write the temp in full, remove the target, rename the temp over it. The
window shrinks to the remove→rename gap, and — the part that matters — a
COMPLETE copy of the data is on the card throughout it.

`readDocFromFile` is what makes that recoverable rather than merely narrower: a
temp beside a missing target is promoted, a temp beside an intact target is
stale and deleted. A promoted temp is parsed first, because a crash during the
temp write itself leaves a truncated one and promoting that would turn a
recoverable state into a corrupt file.

**Verified against the simulator, all three paths:** a normal save leaves no
temp; a complete temp with the target deleted is recovered with the data intact
(`deviceOwner = RECOVERED-FROM-TEMP`, which under the old code would simply have
been gone); a truncated temp is discarded and not promoted. 235 host tests.

### [T-009] A 0 ms redraw delay for iOS — DONE (option), measurement still owed
**scope: iOS display · asked 2026-08-08 · shipped 2026-08-08**

`Typing Redraw Delay` now offers **0 ms**, and it is honored off-device only.

Two things this had to get right, both recorded here because they are the traps:

- **0 is appended, not inserted.** The INDEX is what `settings.json` persists,
  so putting 0 at the front — where it belongs numerically — would silently
  re-map every card in the field, turning a saved 250 ms into 100 ms. The picker
  reading `… 500 ms, 1000 ms, 0 ms` is the price of not corrupting settings.
- **The device clamps it.** On e-ink a refresh is ~570 ms with a real waveform,
  ghosting and battery cost, so 0 would mean a full refresh per keystroke. A
  card carrying 0 (written on a phone, then moved to hardware) is clamped to the
  shortest real step rather than obeyed. Verified per platform: index 6 resolves
  to 0 ms in the simulator and 25 ms on device, the 250 ms default is unchanged
  on both, and an out-of-range index still falls back to the default.

**Still owed, and deliberately not claimed:** the measurement half of this entry
— whether the debounce is what makes typing feel slow on the phone at all. If a
keystroke already coalesces into the next frame, 0 ms changes nothing and the
lag is elsewhere. The option is now there to A/B against, which is the cheapest
way to answer it.

### [T-004] Make the simulator stop lying about the device — TRACKED IN S-001
**scope: simulator fidelity · closed as duplicate 2026-08-08**

This entry and `S-001` in the simulator's [BUGS.md](../crosspoint-simulator/BUGS.md)
are the same work, tracked twice in two repos. S-001 carries the actual table of
six reversals and their status, so it is the one to read. Two are now fixed (the
heap, and battery/USB); four remain (async refresh, the panic path, the OTA
partition stub, the pinned OTA check).

Closed here rather than kept in parallel, because two trackers for one piece of
work is how a thing gets half-done twice.

---

## Carried over, still owner decisions

These were raised in the audit and have not been ruled on. They are not defects,
so they are not in `BUGS.md`.

### [T-005] Cruft with zero references — RULED and actioned
**scope: repo hygiene · raised 2026-08-06 · ruled 2026-08-08**

Ruling: delete the QA captures and the spike scripts, keep the rest.

**Deleted:**

| Thing | Note |
|---|---|
| `qa_x4/`, `qa_x4b/`, `qa_sleep/`, `qa_west/`, `qa_west2/` | 492 KB of PNGs from one debugging session on 2026-08-03. Byproducts of a run |
| `spike-build.sh`, `spike-capture.py`, `spike-drive.py`, `spike-run-exchange.py` | BLE spike scaffolding |

**Kept:**

| Thing | Why |
|---|---|
| `src/util/StringUtils`, `UrlUtils`, `HtmlToPlainText` | library-shaped and small; `HtmlToPlainText` is what an OPDS description or a Claude response would want |
| `docs/flared-semiserif.html` | a typography specimen, not a byproduct — 220 KB because the faces are embedded |

**The one thing worth saying about the deletion:** only `spike-build.sh` was
actually superseded — `7fee9a8c` removed the `lib_ignore = BLE` entry that made
its `-I` injection necessary, and `docs/ble-editor-spike.md` had already
recommended deleting it. The other three were **working device automation**
(unattended capture, driving `CMD:BLEEDIT`, running a whole Claude exchange from
one port owner), removed as spike scaffolding rather than as dead code. What
each did is recorded in that doc's Reproducing section, and they are one
`git revert` away if unattended capture is wanted again.

### [T-006] `freeink-sdk` bumped, and made repeatable — DONE (host half)
**scope: dependency · ruled and actioned 2026-08-08**

Pin moved `e514a868` → `24fbab75`: **71 commits, 79 files, +17.3k lines**, a
clean fast-forward with nothing local to preserve. (It was 54 when filed two
days earlier — it drifts about a commit a day, which is the argument for
`scripts/update_sdk.sh` rather than for doing this by hand.)

Several of them are fixes this fork wants: *"Fix grayscale waveform LUTs for
CrossPoint rendering"*, *"fix(x3): one forced full sync after begin(), not
two"*, *"preserve pixels outside partial-width image blits"*, *"correct
voltage-to-percentage conversion for battery"*. There is also new hardware
support (M5Stack Paper Mono, UC8279 for X4 800x480) which is purely additive
here.

**Verified off-device:** `gh_release` and `simulator_x3` build, 235 host tests,
9 simulator tests, `test_read_aloud_capture` including the rect-geometry
assertion — and Home and a book page render **pixel-identical** before and
after (0 of 418,176 differ).

**NOT verified, and this is most of what the bump carries:** waveform LUTs, the
GPIO/PWM ordering change, holding the display RESET pin high in deep sleep, and
the battery curve. None of those execute on a host at all. Flash it, read a few
pages watching for ghosting or a changed refresh, sleep and wake, and look at
the battery reading.

### [T-006a] Submodule bumps are now one command
**scope: process · added 2026-08-08**

[scripts/update_sdk.sh](scripts/update_sdk.sh) — `--dry-run` to see the gap and
what it touches, no argument to fast-forward and prove it, or a ref to pin
somewhere specific.

It gates on: submodule initialized (a fresh worktree does not inherit it),
submodule clean, **fast-forward only** (a pin ahead of the target means someone
committed to the SDK locally — that is a merge with a decision in it, and the
script refuses), device build, desktop build, host tests, and a pixel diff of
Home and a book page against a pre-bump reference. Any failure puts the pin back
where it was.

What it deliberately does NOT do is claim success. It ends by naming what a host
gate cannot execute and listing what to look at on hardware, because the
tempting failure here is a green run being read as "verified". Both guards were
tested: the non-fast-forward refusal fires, and the render comparison is the
gate that would catch a layout regression.


Kept for the reasoning, not the status. Each records something that would
otherwise have to be rediscovered.

### [T-001] Switch all icons to the Lucide set — DONE
**scope: UI · asked and ruled 2026-08-07 · SHIPPED `see below`**

All 14 icons are Lucide now, at both 24px and 32px, generated by
`scripts/gen_lucide_icons.py` from the SVGs already vendored in the SDK.

| UIIcon | Now |
|---|---|
| Transfer | `arrow-right-left` |
| Folder | `folder` |
| Book | `book` |
| Recent | `clock` |
| Settings | `settings-2` |
| ManageFiles | `folder-cog` |
| CreateNote | `square-pen` |
| Library | `library-big` |
| Wifi | `wifi` |
| Hotspot | `radio` |
| ClaudeMark | authored `claude-mark.svg` |
| Text | `file-text` |
| Image | `image` |
| File | `file` |

**The rotation trap, resolved.** `GfxRenderer::drawIcon`
(`lib/GfxRenderer/GfxRenderer.cpp:1450`) plots stored `(row, col)` to screen
`(x + size-1-row, y + col)` — a 90° turn baked into the draw, which its own
comment says reproduces what the old byte-aligned blit did. So assets must be
stored **pre-rotated 90° CCW** for the two to cancel, and the generator does
that before packing.

Note this is the OPPOSITE of the SDK's own `libs/assets/Icons/tools/gen_icons.py`,
whose header states its `freeink::Icon` bits are deliberately not pre-rotated
because that renderer maps logical coordinates itself. Both are right for their
own draw path; using either generator with the other's renderer ships sideways
icons. That is why this firmware got its own script rather than adopting the
SDK's — recorded so nobody "simplifies" it later.

**Also done:** 16 superseded headers deleted (all confirmed zero-reference
first), including the five size-variant duplicates. `src/components/icons/` is
now `lucide_icons.h`, `claude-mark.svg`, plus `cover.h` and `BackspaceIcon.h`
which are different assets still in use. ManageFiles no longer borrows the plain
file glyph at 24px — every icon exists at both sizes now.

**Verified:** decoded three generated bitmaps back through drawIcon's exact
transform before trusting them; Home and Manage Files screenshotted after;
device `gh_release` and desktop both build; 215/215 host tests.

**Not confirmed on hardware** — rendering was checked on the desktop panel, not
on e-ink.

### [T-002] `src/notes/` versus `SCOPE.md` — DONE
**scope: docs · ruled 2026-08-07 · keep the capability, amend the docs**

Ruling: notes and Claude chat stay in device builds. Nothing about the binary
changed; the documentation was what was wrong.

`SCOPE.md` and `ROADMAP.md` are **upstream's** contribution policy, and they now
say so, with a fork banner naming the two divergences and the reason. Upstream's
own words are what make it coherent — it says these features "belong in other
forks" and that it "generally defers to that fork." This is one of those forks.

Upstream's text is left verbatim under the banner rather than edited in place.
Rewritten bullets would conflict on every upstream change to those sections, and
until someone noticed, this repo would be publishing a distorted copy of another
project's policy under that project's filename.

The three-way contradiction is resolved by picking the narrow reading and
writing it down: no always-on radio and no feed readers, but Wi-Fi the owner
starts, uses once and stops is in scope here. Recorded in
[docs/fork-sync.md](docs/fork-sync.md) so a later sync does not "fix" it back.

### [T-003] The published SSID and the API key — DONE
**scope: security · ruled and executed 2026-08-07**

Both halves are closed.

**The key — protected, then deliberately UNprotected.** `claude-key.txt` was
added to `HIDDEN_ITEMS` in both `src/network/CrossPointWebServer.cpp` and
`src/network/WebDAVHandler.cpp`, which took the download endpoint and WebDAV GET
from **200 with the key in the body** to 403, took DELETE to 403, and removed it
from the listing.

**That was then reverted on owner ruling the same day, and the file is servable
again by design.** The ruling: the key being readable over Wi-Fi is acceptable,
and it will be rotated as needed. What decided it was measuring the cost —
`HIDDEN_ITEMS` blocks `PUT` too (403, against 201 for an ordinary file), so the
protection removed the only way to WRITE a rotated key over File Transfer.
Rotation would have meant pulling the SD card. Blocking the write while the read
is acceptable is pure cost.

Recorded rather than dropped because the exposure is real and someone will
rediscover it: the file transfer server has **no authentication of any kind**,
and AP mode is an open network (`AP_SSID = "CrossPoint-Reader"`,
`AP_PASSWORD = nullptr`, "Open network for ease of use"). Anyone in Wi-Fi range
can join and read the key while the File Transfer screen is up. That is a known,
accepted risk on this fork — not an oversight. Do not "fix" it without asking.

For reference if it is ever revisited: a leading dot would NOT protect it, since
WebDAV deliberately serves dot-paths so a card mirror can sync `/.crosspoint`
and `/.fonts`. `HIDDEN_ITEMS` is the only list that covers WebDAV. A write-only
arrangement (PUT allowed, GET blocked) was offered and declined.

**The SSID.** Gone from the code — `connectWifi` walks saved credentials like
the Wi-Fi picker's auto-connect — and scrubbed from published history on
2026-08-07. 166 commits rewritten (2026-08-03 forward), force-pushed to the
fork. Root commit, commit count and shared ancestry with `upstream/develop` all
verified unchanged afterwards.

**Two things worth remembering from doing it:**

- `git filter-repo` strips GPG signatures. Run unscoped, it rewrote the *root*
  commit — which upstream signed — and cascaded to all 1361 commits, severing
  the fork from upstream entirely. Scope any future rewrite with
  `--refs <first-bad>^..main --partial`.
- `--replace-text` rewrites file contents only. Four commit **messages** still
  carried the SSID until `--replace-message` was added with the same rules file.

Cost paid: 7 of the rewritten commits were GPG-signed and are now unsigned.
Pre-scrub history is preserved at
`~/crosspoint-archive/crosspoint-reader-prescrub-20260807-1555.bundle` and as
`refs/prescrub/main` locally.

**Still outstanding, and it is not a code change:** the API key was served over
the network by every build up to today, and the SSID was public from 2026-08-03.
A scrub limits future exposure and retracts nothing — anyone who cloned still
has both. **Rotate the Anthropic key.**

