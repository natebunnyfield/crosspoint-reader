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

**Branches are not a fifth tracker, and a sweep on 2026-08-17 confirmed it.**
Every feature branch in both repos was checked with `git log --oneline main..<b>`
and every one returned **empty** — `crt-red-grey-fw` and `bw-outlines` here,
`crt-all-phosphors`, `crt-sepia-blue`, `crt-red-grey` and `bw-outlines` in the
simulator. All six are fully merged; nothing is owed from a branch, and no work
is stranded on one. Recording the negative result so the next person does not
re-audit six worktrees to learn that they are empty.

Two consequences, neither of them work items:

- The six branches and their worktrees are `cptidy` candidates — merged branches
  plus prunable worktree records, including two under a session scratchpad.
  **Owner ruling 2026-08-17: he runs `cptidy`, no session deletes them.** Same
  end state either way since all six are 0 ahead; the reason is that the
  simulator repo had another session committing to it at the time, and the
  standing convention is that tidy and merge are his commands, not a session's.
- The simulator's `main` was **9 commits ahead of `origin/main`** at the time of
  the sweep (the page-colour palette line, plus `ST-009` filed against it), and
  its `TODO.md` was being written to in the same minute by another session. Not
  pushed from here on purpose: an unpushed branch that someone else is actively
  committing to is theirs to push. **Owner ruling 2026-08-17, same reason:**
  leave `ST-005` alone too — its mockups need approval before code, and two
  sessions rendering mockups into one tracker is how an entry gets half-written
  twice. Simulator work waits for that session to finish.

---

## OPEN

### [T-023] XS and XXS sizes for every shipping family

Owner, 2026-08-25: *"cut XS and XXS versions of every s tiers shipping font."*

The reader ramp is `BUILTIN_READER_POINT_SIZES[] = {12, 14, 16, 18}` — S, M, L,
XL, with M the default. XS and XXS are two more sizes BELOW that, presumably
10 pt and 8 pt, for all eight shipping families. `sd-fonts.yaml` carries
`sizes: [12, 14, 16, 18]` per family and would gain them.

**THE TRAP, and it is the reason this is written up rather than just done.**
`fontSizeSlot` is persisted as an INDEX, not a point size. Inserting sizes below
S re-points every value already stored on every card: a reader sitting on M (1)
would silently land on a different size after the update. This is the same
"append, never insert" discipline the preset list and the settings enums follow,
and this exact field has been through it twice already — the comment at
`CrossPointSettings.h:90-95` records that 1.5 stored an absolute point size and
broke every family switch, and that 1.4 stored a 0..3 slot in the same key, which
is why `fontSizeSlot` exists as a separate key at all.

**OWNER RULING 2026-08-26, IN TWO PARTS. Insert in size order, and DO NOT
BUILD A MIGRATION:** *"the reindexing never matters because it is just me using
this."*

That second half removes the entire risk of this item, so read it before
inheriting the caution below. There is one user. If a stored slot shifts
meaning, the cost is that he re-picks his size once -- not a silent fleet-wide
regression. So the ramp simply becomes `{8,10,12,14,16,18}` in order, no
`fontSlotNeedsMigration` path, no version gate, no idempotence proof.

**This is a scope ruling, not a claim that the field is safe.** The scars below
are real and the discipline still applies to anything that ships beyond one
device; it is waived here because the population is one. If this fork ever gains
users, the migration is the thing to build first.

The superseded reasoning, kept because it is what makes the waiver a decision
rather than an oversight:

~~INSERT IN SIZE ORDER AND MIGRATE.~~ Asked with both
options and their costs, he chose the ordered ramp over the append. So indices
stay in size order forever after -- `{8,10,12,14,16,18}` -- and every card
already holding 0..3 must be shifted +2 exactly once. That migration is now the
whole risk of this item: get it wrong and every reader silently reads at a
different size, which is the failure this field already carries scars from
twice. It must be idempotent or version-gated so it cannot run twice and shift
by four.

The alternative, kept because it is what makes the ruling reversible: the ramp
could have been APPENDED to (XS and XXS take indices 4 and 5, and the
picker sorts for display rather than storing sort order — the same split the
preset list already uses) or migrated deliberately through
`fontSlotNeedsMigration`, which exists for precisely this and is consumed at
`SdCardFontSystem.cpp:55`. Decide which BEFORE building fonts; the font build is
the cheap half.

**Cost to weigh before starting.** The seed tree is 316 MB for eight families at
four sizes and two tiers. Two more sizes is roughly +50% on the font payload, so
expect a materially larger app — the installed figure is what matters, since CPZ1
squeezed 117.7 MB to 34.8 MB and the ratio should hold. Measure it rather than
estimating.

**And check legibility before committing to 8 pt.** Glyphs are 2-bit with four
levels, quantized at build time from FreeType coverage at the target ppem. At
8 pt the stems are close to one pixel and the quantizer has almost nothing to
work with; whether XXS is readable on the panel is an empirical question, not a
build-configuration one. Render a page at both new sizes and look before
shipping eight families' worth of them.

**What "done" looks like:** both sizes present for all eight families at 1x and
2x, every existing install still rendering the size it rendered before the
update, a measured installed-size figure, and a rendered page at each new size.

**DONE 2026-08-26.** The ramp is `{XXS, XS, S, M, L, XL}`, inserted in size
order, no migration, per the ruling above. Proof page (every family, both new
slots, native panel pixels): https://claude.ai/code/artifact/7fa23ef7-b0ba-4ccd-9356-ea8f76273adb

**8 pt IS legible, on all eight** — the gating question, answered against
rendered pages rather than reasoning. Four styles, accented capitals, the math
row and the confusables row all hold at the new bottom slot. The stems survive
because the panel has four gray levels to spend on them; a 1-bit panel is what
would have killed this, not a small one. **Almendra is the marginal one** and is
the one to re-judge if any gets rejected: the lightest face in the set, and its
XXS line is proportionally the tightest at 19 px of leading against 23
everywhere else. That is whole-pixel rounding on a 6 pt cut, not a collision —
the plain-text clearance audit (`~/Downloads/crosspoint_fonts_s_tier_sources/cpfont_render.py`)
reads +4 there, better than shipped Coelacanth's +1, and all sixteen new cuts
pass it.

**The sizes are NOT "two points off each ramp", and that matters.** The eight
families have never shared point sizes, they share measured x-height — so the
new slots were derived the way the original four were, by sweeping every family
6-11 pt and reading x-height back out of the built `.cpfont`s. The picks:
Edgar 8/10, TeX Gyre Schola 8/10, Libris 8/10, Coelacanth 9/11, Libre
Franklin 7/9, TeX Gyre Heros 7/9, Inknut 7/9, Almendra 6/8. Seven land on
x-height 8/10 exactly; Inknut lands on 9/11 because its whole ramp has always
run +1. Derivation and the Almendra 6-vs-7 pt call are written up in the
`Slot uniformity` block of `sd-fonts.yaml`.

**Measured cost** (the estimates in this entry were both wrong, in opposite
directions):
- **Installed fonts +14,958,002 B.** CPZ1 1x+2x seed tree 51.9 MB -> 66.2 MB,
  +27.5%. Raw +35.4 MB (179.6 -> 206.7 MB), so +20.7% raw, not the +50% guessed
  above — file size goes as ppem^2 and these are the smallest cuts in the set.
  The new files compress WORSE than the old (0.40 vs 0.30) because small glyph
  bitmaps hold less redundancy per byte. The "117.7 MB -> 34.8 MB" figure quoted
  above is stale: it is `docs/seed-font-compression.md`'s SEVEN-family, 56-file
  measurement, and the tree is eight families / 64 files now.
- **Firmware flash +266,882 B**, 76.9% -> 81.0% of 6,553,600 (`-e default`).
  RAM +64 B. That is the built-in Libre Franklin fallback gaining its own 8 and
  10 pt cuts in four styles across all three tiers. It is not optional: the slot
  is an index into whatever ramp is active, so a reader with no SD family would
  otherwise pick a slot the binary cannot draw.

**Adversarial review found one real regression in this change, now fixed.**
`getSmallestReaderFontId()` is the wide-table step-down and returned a bare
`LIBREFRANKLIN_READER_12_FONT_ID`. That was safe while 12 was the FLOOR of the
ramp; with XXS and XS below it, a reader on slot 0 or 1 got a rotated wide table
set at 12 pt on a page set at 8 or 10 — a "size down" that is a size up, and
`tableFontForRotation()` (`ChapterHtmlSlimParser.h:209`) takes it
unconditionally. It clamps to the body size now, so the step is monotone at
every slot. `TheWideTableStepDownIsClampedToTheBodySize` pins it.

The same review found the FIRST version of
`EveryBuiltinRampSizeHasAFontIdCase` was not testing what it claimed: it
searched the whole rest of the file rather than the function, and asserted the
`case` and the id SEPARATELY, so a transposed ladder
(`case 8: return ..._10_FONT_ID;`) passed it — the exact bug class the test is
named for. It asserts the pairing by regex now, bounded to the function, and
both a transposed ladder and a wrong `default:` arm are proven to fail it.

Also corrected on the way past: `lib/hal/HalGPIO.h:105` and the `/api/settings`
example in `docs/webserver-endpoints.md` both still said S/M/L/XL — stale as of
this change, not before it.

**Two follow-ups, both deliberate, neither blocking:**
1. `crosspoint-simulator/ios/seedfonts/` still holds the four-size tree. It is a
   different repo and was out of this task's lane; copy the 32 new files across
   (1x + 2x, from `fs_/fonts/`) before the next TestFlight build or the phone
   offers two slots it cannot render.
2. `fallbackReaderLineHeight()` in `src/activities/reader/EpubReaderActivity.cpp`
   still lists only 12/14/16/18. It picks the nearest built-in line height for
   read-aloud rects when no SD font is resident, so at XXS it over-estimates by
   ~10 px rather than breaking. Left alone because that file was another agent's
   lane at the time; adding `{8, ...}` and `{10, ...}` to `kBuiltins` closes it.

Also fixed on the way past, because it would have gone silent: the four specimen
loops in `tools/calendar_preview/render_harness.cpp` were a hardcoded `< 4`, so
after this landed they would have stopped photographing the top of every ramp
without failing. They take their bound from `installedOrdinalCount()` now. The
A/B `fonts` mode deliberately still says 4 — it is keyed on nominal point size,
not on slots.


### [T-022] Claude results: the FRONT pair should page too

Owner, 2026-08-24: *"in claude results, front rocker switch should activate
PgUp / PgDn (currently side buttons work but front ones don't)."*

Taken at face value and consistent with the code. In `ClaudeChatActivity`, the
paging is bound to `Button::Up` / `Button::Down`, which in this firmware's
vocabulary is the SIDE pair — the physical rocker (`:424`, `:429`, `:482`,
`:487`). The FRONT cluster's `Left` / `Right` are spent on `repeatCol`
(`:480-481`), which is column navigation, so in the results view the front pair
does nothing for scrolling.

**What "done" looks like:** in the results view — and only there — the front
`Left`/`Right` page the same way the side rocker does, sharing one code path so
the two cannot drift. Everywhere else in that activity the front pair keeps its
current job.

**The thing to get right:** `repeatCol` owns `Left`/`Right` for the composer and
the daisy picker, so this is a per-VIEW binding, not a global remap. Establish
which views are live when results are on screen (`panel.isDaisy()` already gates
two of the four sites) and bind only there. A global change would break column
navigation in the composer, which is the kind of regression that only shows up
when someone tries to type.

**Also check the X4.** `HalGPIO::hasEdgeSideButtons()` is FALSE for X4, so on
that board the side rocker is not on the edge at all — which is the profile
where a working front binding matters most. Whatever lands should be verified
against an X4 profile as well as X3.

Not started.


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

**Superseded on the TestFlight line, 2026-08-17: build-82 is up** — 50 MB IPA,
delivery `3f4451a4`, tagged and pushed as `build-82` in the simulator repo. It
carries firmware `c36dba242` (this file's T-015 fix) and simulator `2e9e4c3`,
on top of the iPad-landscape change `eaaa048`. All four `testflight.sh` gates
passed: desktop canary, source set current (132 TUs), `crosspoint_core` carries
`SIMULATOR_DEVICE_X3` + `CROSSPOINT_RENDER_SCALE=3`, and no purpose string is
demanded by the binary.

One honest caveat, so nobody tests the wrong thing: **the progressive-JPEG fix
is NOT exercised by this build.** It lives in a JPEGDEC patch, and the iOS
target substitutes stb_image for JPEGDEC entirely — that path was already
correct there. The fix needs the device.

**Where the two repos stand against build-82, re-measured 2026-08-17 15:25:**

| Repo | Against the build |
|---|---|
| firmware | build-82 carries `c36dba242`, which is still the newest **code** on `main` — the only commit since is `809702e91`, docs. So the firmware half of build-82 is current. |
| simulator | `main` has moved **9 commits past the tag**: the page-colour button, the palette rework (Sepia CRT retired, Reading at three paper temperatures replacing Soft and Cool Gray), and `ST-009`. None of it is on TestFlight. |

That split matters when reading the simulator's "SHIPPED, unverified on the
phone" items: `ST-008`, `ST-004` and `ST-006` are testable in build-82, the
palette work is not in any build yet.

The rest of the entry below is unchanged and still owed.

**DONE 2026-08-18 — the bin is staged and the checklist is written.**
`~/crosspoint-archive/staged/20260818T2146Z-crosspoint-b39eea60.bin`, built from
`/private/tmp/rel-wt` at `b39eea60c`, 4,955,360 bytes, sha256 `b1baea7b…`,
Flash 75.4% / RAM 16.5%, version string `1.5.0-B2`. The ordered checklist is
[docs/device-verification-checklist.md](docs/device-verification-checklist.md)
and it, not this entry, is what to work through with the device in hand.

**It was checked against [B-029] after linking**, which is the whole reason that
figure is recorded: the commercial editor faces are gitignored, a worktree build
without them succeeds silently and 430,674 bytes smaller, and 68.8% flash would
have been the only visible sign. `nm firmware.elf | grep -c
"nittitypewriter\|pragmatapro"` reports 100 on this bin. Since `b39eea60c` the
check is mechanical — a release env refuses to build without them — so this is
belt and braces rather than the only guard. The superseded `2c17295f` bin was
deleted rather than left beside it. Cards are NOT reprovisioned — that still
waits for a mount.

**Owner ruling 2026-08-17: stage a bin and write one checklist.** Asked what to
prepare for the device batch. A `gh_release` build from a clean throwaway
worktree at current `main`, named `<UTC>-crosspoint-<sha>.bin` and staged in
`~/crosspoint-archive/staged/`, plus a single ordered checklist covering every
device-blocked item across both trackers — what to flash, what to watch, what
supersedes what. Cards are NOT reprovisioned as part of this; that waits for a
mount. The point is that one card mount plus one sitting then closes nine items
instead of rediscovering them from four tracker entries.

**Not done — needs the hardware in hand:**

- **The SD cards.** None were mounted, so `cpcards` never ran. They are still on
  `bb614f73`, roughly forty commits back. This is the only step blocked on a
  physical act.
- **Confirm on device**, since it will be in hand anyway: the seven Lucide icons
  on Home, the caret advancing a full space in Create Note, the clock preview on
  the offset screen, and the WebDAV update flow — that last one matters most,
  because its erase-write-reboot step is the ONLY part that cannot be exercised
  off-device at all.
- **One-button firmware update** (`ee6fad7e5`, Home → Update Firmware,
  2026-08-16). Added after this list was written. `CROSSPOINT_NO_DEVICE_FLASH`
  (`HomeActivity.cpp:34-39`) compiles the Home row out for **iOS only** — that
  is where `CrossPointIOSExclusions.cmake` defines it — so build-82 does not
  carry the row, while the desktop simulator does draw it. (Corrected
  2026-08-19: this entry previously claimed no host build could reach it.)
  What still cannot be exercised anywhere but the device is the install itself. What to watch is the whole chain — check, install,
  reboot, and the **rollback**, which is the part that was genuinely missing
  before that commit (`network/OtaCommit.cpp` confirming a healthy boot). Full
  write-up in [docs/one-button-firmware-update.md](docs/one-button-firmware-update.md).
- **Older debt, still unconfirmed:** power-off-while-typing (build-33+), iOS file
  transfer (build-37), B-018 back-nav, B-017's notes half.

**What this loop taught, worth keeping:** the iOS archive found a link error that
neither the device nor the desktop build could see — `SdFirmwareUpdateActivity`
is excluded from the iOS source set, and a new call into it from a shared TU only
fails when that target links. The iOS archive belongs IN the loop, not after it.
`xcodebuild -target CrossPointX3 -sdk iphoneos build CODE_SIGNING_ALLOWED=NO` is
the cheap version and needs no keychain.

---

## Finished

### [T-014] Sibling-fork improvements — AUDITED, one ported, 2026-08-19
**scope: upstream-adjacent · opened 2026-08-15 · closed 2026-08-19**

**All six forks are audited.** matcha in
[docs/matcha-heap-audit.md](docs/matcha-heap-audit.md) (five of six items N/A,
one closed by measurement), the other five in
[docs/fork-audits.md](docs/fork-audits.md).

**What actually came across: one commit.** crosspoint-jp's CSS-parse heap floor.
`CssParser` here capped the rule COUNT and refused to START parsing below 64 KB,
but nothing checked the heap between those points — and under `-fno-exceptions`
the map insert aborts rather than fails. Both registration paths now carry a
32 KB floor.

**Everything else was already here, ruled out, or written against code this fork
does not have:** vertical writing (crosspoint-jp's CJK abort), a diverged line
breaker (crossmux), OPDS and Calibre (crossmux again), a progressive indexer this
fork already implements another way (cpr-vcodex), 1-bit font packing ruled out by
[T-013] (folio), and table work superseded by [T-012] and [T-021] (CrossInk).

**The audit's own finding, filed as [B-032]:** the B-030/B-031 sweeps covered
explicit `new` and not container growth, and a failed `reserve()` aborts exactly
the same way.

**Closed rather than left open for "more PRs".** The premise — that these forks
hold a queue of work worth importing — was tested against all six and did not
hold. Re-open it when a specific commit is named, not as a standing task.

**Original entry follows.**

**The matcha half is AUDITED and mostly closed, 2026-08-18 — full write-up in
[docs/matcha-heap-audit.md](docs/matcha-heap-audit.md).** Five of the six named
items are done with: `hasContent` does not exist here, the indexing patch is
vertical-writing code we do not have, the history fix needs a reading-stats
subsystem we do not have, the cover-thumbnail lock is not on our path, and the
reader-settings OOM fix is a shape `rebuildSettingsLists()` already has. Only
"font cache freed before settings save" survives, and it needs a heap
MEASUREMENT at the five `saveToFile()` sites before it needs a patch.

**Correcting this entry's own framing while here:** "only 31 behind upstream"
measures matcha against UPSTREAM, not against us. Against us it is 517 ahead /
673 behind, and it contains whole subsystems this fork never had. Give every
remaining fork on the list a "does this code even exist here" pass before
reading its commits as candidates.

**Owner ruling 2026-08-17: start here, and start with the matcha heap series.**
Asked as "what do I pick up next" against T-012 (tables) and a tidy/device-prep
pass; the heap/OOM work won because it is real crash fixes against the 380 KB
ceiling, it can be verified correct on a host, and matcha is only 31 behind
upstream so the hunks still apply. T-012 stays open and unstarted.

From [docs/fork-ecosystem.md](docs/fork-ecosystem.md), which surveyed 1,443
forks. Bring the worthwhile work across as **several small PRs for review** —
one concern each, not one large drop. Per `docs/fork-sync.md`: per commit, live
hunks only.

Named forks, in the owner's order:

| Fork | What to mine |
|---|---|
| `eszter007/matcha-reader` | The heap/OOM series — EPUB indexing pressure, reader-settings OOM, font cache freed before a settings save, per-call allocation out of `HalStorage::hasContent`, history bounded by memory, cover thumbnails without HAL power locks. Richest vein, and only 31 behind upstream. |
| `uxjulia/CrossInk` | Table rendering + colSpan (feeds T-012). Skip themes, carousels, tilt and touch. Its progressive-JPEG work was checked under T-015 and is already present here — do not re-propose. |
| `folio-etc/folio` | 1-bit `.cpfont` packing (feeds T-013); font budgets during heavy activities; power-button claiming and button-hint rendering (T-011 removed ours; folio's approach is still the reference if hints ever return). |
| `0x1abin/crossmux` | Low-heap crash fixes — EPUB footnote allocation, web settings heap exhaustion. Skip the Apps hub and mini-games. |
| `zrn-ns/crosspoint-jp` | The `abort()` on page-turning very long CJK paragraphs. Skip vertical writing. |
| `franssjz/cpr-vcodex` | Progressive EPUB indexing. Skip KOReader profiles, flashcards, dashboards. |

**Do not re-propose** the four already checked and present: CrossInk's progressive
JPEG cover support ([docs/progressive-jpeg.md](docs/progressive-jpeg.md)), HTML 4.01 entities
(all 252 in `htmlEntities.cpp`), `<hr>` (handled at
`ChapterHtmlSlimParser.cpp:601` and `:992`), the hidden-file toggle
(`FileBrowserActivity.h:31`).

**Done looks like:** one branch + PR per concern, each building `-e default`
AND `-e simulator` from a cold cache, each stating what was verified on host
and what still needs the device.


### [T-021] Wide tables: rotate the page, fall back to a key block — SHIPPED 2026-08-19
**scope: reader · ruled + shipped 2026-08-19 · `6e357bead` + `d7ea20c86`**

**Done.** Columns upright where they fit; otherwise a clockwise-turned page of
its own at the smallest size of the reading family; otherwise the names once as
a bold block with each row beneath. Verified in the simulator on a generated
five-column EPUB (`test/epubs/test_wide_table.epub`).

**Four bugs, every one found by looking at a render rather than by a green
build**, which is the part worth carrying forward:

1. Columns OVERLAPPED — the floor allowed a column narrower than its longest
   word, and a word cannot be split, so cells overflowed onto each other. The
   floor is each column's widest word now, with a sweep test asserting it.
2. The squeeze gave up as soon as the WIDEST column hit its floor, even with
   room in the others, sending fitting tables to the fallback.
3. The rotated page was set at reading size and so did not fit: word-floors
   summed to 899 px against 668 available. It is a size down now, which needed
   `smallFontId` threaded through `ReaderRenderSpec`.
4. `drawTextRotated90CCW` anchors a band by its RIGHT edge where the CW twin
   takes the left, and the row axis runs the other way — the first version drew
   11,197 out-of-range pixels and printed the header at the foot of the page.

**Still owed:** device confirmation. Rotation, a page the reader physically
turns, and 2 px margins on real e-ink are all things a host render cannot
settle. Added to [docs/device-verification-checklist.md](docs/device-verification-checklist.md).

**Original entry follows.**

**scope: reader · ruled 2026-08-19 against nine real renders**

**Owner ruling: E when it does not truncate, I when it would.** A table that
cannot be columns upright becomes a **rotated full page** — turned 90 degrees
CLOCKWISE, at the smallest reading size, 2 px margins, no chrome — provided
every cell fits without truncation. When it would truncate, it falls back to
**I**: the five column names stacked as a bold block, then each row beneath it
as plain values, every row identical in shape.

What this replaces: today a table the planner refuses is flattened with its
column name repeated in front of every cell ([T-012], shipped `6c0151f09`).
That form is retired by this ruling — I is the flattened form now.

Rejected, with reasons, so they are not re-proposed: squeezing columns to a
floor (truncates whole words), the smallest size upright alone (still truncates
four of five columns on a 528 px page), and **any counter-clockwise rotation**
(see the standing ruling in [docs/ui-conventions.md](docs/ui-conventions.md) —
he is right-handed).

**The blocker, found while scoping: there is no clockwise draw call.**
`GfxRenderer::drawTextRotated90CW` draws COUNTER-clockwise content; its name is
the reader's turn, not the transform. `tools/table_preview` faked E by turning
the finished framebuffer 180 degrees, which a page renderer cannot do per page.
So E needs `drawTextRotated90CCW` first: `TextRotation` (GfxRenderer.cpp:322)
gains a variant, `renderCharImpl` gains four mirrored branches at :453, :473,
:496 and :534, the advance walks the other way, and the hi-res branch comes free
because it sits inside the same template.

**Then the page-level half**, which is the larger one: a rotated table has to
become a whole page the reader can turn to, which touches `Page` serialization,
the `section.bin` format (36 -> 37), and the word-anchor path that assumes every
page is upright.

**Done looks like:** a wide table renders rotated and complete on device; one
that cannot fit even rotated renders as I; the section cache is bumped; and the
truncation decision is made before anything is emitted, the way the column
planner already does it.


### [T-012] Tables in EPUB content — SHIPPED 2026-08-19 as columns + a header rule
**scope: reader · opened 2026-08-15 · ruled 2026-08-18 · shipped `6c0151f09`**

**Done.** A table that plans as columns is emitted as columns: bold header row,
numeric columns flush right, one 2 px rule under the header, nothing else drawn.
Verified in the simulator on `test/epubs/test_tables.epub` — the small tables
render as columns, and the big-table chapter falls back to the flattened form.

The planner is `lib/Epub/Epub/parsers/TableColumnLayout.{h,cpp}`, pure and
covered by 10 tests. The parser buffers a candidate table (text plus bold/italic
runs) and emits at `</table>`, because columns cannot be measured mid-stream.

**The old flattened path is still there and is still reachable**, which is the
part worth knowing before touching this: buffering is abandoned mid-table the
moment a cell holds anything richer than inline emphasis — an image, a footnote
link, a list, a nested table — and past 3 KB / 64 rows / 5 columns. Those all
work through the streaming path and would have been silently dropped by a
text-and-styles buffer.

**Rows move as a unit.** A row is measured before it is emitted and the page is
completed first if it does not fit; a row taller than a page abandons columns
for the whole table before anything is emitted.

**Two bugs found by looking at the output, not by the build:** a column sized to
fit its widest cell exactly still wrapped it (the line breaker works in whole
words — `kColumnSlack` is 2 px), and a table with no `<thead>` lost its first
row while a `<thead>`-only table rendered as nothing.

`section.bin` 35 -> 36: identical bytes, bumped because table LAYOUT changed and
a cached section would otherwise render the old shape forever.

**Still open, and deliberately not answered:** what a table too wide for the page
should do. Today it flattens, which is a fallback nobody ruled on — every
mockup fit. Worth a decision if a real book turns up a wide table.

**Owner ruling 2026-08-18, after seeing four real renders: COLUMNS + HEADER
RULE.** Real columns, the numeric column flush right, and a single 2 px rule
under the header row. Nothing else drawn — no outer box, no row rules, no
column rules.

The four candidates were rendered through the real `GfxRenderer` onto the real
528x792 page in the reading face (`tools/table_preview/`), not described in
prose: flattened (today), columns with no rules, columns with a header rule, and
a full grid. What the ruling rejects is worth keeping: the full grid is the only
option where a wrapped cell cannot be misread as a new row, and it was still
turned down, because on 1-bit ink every rule is the same black as the type and
the grid is the heaviest thing on the page.

**A second question follows and is NOT answered:** what a table too wide for the
page does. The mockups all fit. Flattening the overflow case is the obvious
fallback, but it has not been ruled on.

**Also ruled: no setting.** The entry was written as a flat-vs-tabular choice;
the owner's clarification was that the ask is tables in EPUB content rendering
properly. One good render, no row in Settings, no persisted value, no
cache-invalidation branch for a toggle. Flattening stays only as the fallback
for tables that cannot fit.

**Owner ruling 2026-08-18: [B-031] first in one pass, then this, same session.**
Both want `ChapterHtmlSlimParser`, so landing a table renderer on top of
allocations that are being rewritten is the worse order — whichever goes second
takes the conflicts, and the memory sweep is the one already framed. B-031
landed in `e0a1715b8`.

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


### [T-016] READMEs no longer describe what these repos are — DONE 2026-08-16
**scope: docs · opened 2026-08-15 · both repos landed 2026-08-16**

**Done, with the simulator's paired `ST-007`.** This README gained Manage Files,
Create Note (full-screen editor, timestamped notes at the card root), Claude
(`/claude-key.txt`, `claude-haiku-4-5`, transcript appended to
`/claude-chat.md`, BLE torn down for the TLS exchange), the two text-entry
styles behind `TextEntryFactory`, Bluetooth keyboards, and text antialiasing
(`SETTINGS.textAntiAliasing`, off by default) — each checked against the tree
before being written. It also gained a short section pointing at `SCOPE.md` and
`docs/fork-sync.md` for what was deliberately removed, described as what those
two files actually are rather than what would have been convenient.

**The "advertises removed features" premise turned out to be already handled.**
Greping this README for KOReader, Calibre, bookmarks, auto page turn, the status
bar and OPDS returns **zero hits each** — the removals had already been kept up
with, and two of them (the four themes, the wireless OTA screen) are named
inline with their dates. What was actually missing was the other direction: the
features this fork ADDED and never wrote down. Recording that here so the next
person does not re-audit the removal half.

**Original entry follows.**

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

### [T-015] Progressive JPEG decode — REPRODUCED and FIXED 2026-08-17
**scope: reader · opened 2026-08-15 · full write-up in [docs/progressive-jpeg.md](docs/progressive-jpeg.md)**

Real, and worse than the item assumed. A progressive JPEG whose DC coefficients
are split into one scan per component desynchronizes JPEGDEC's MCU loop, which
reads chroma blocks that the scan does not contain. On a large image that is
`rc=0 lastError=2`; on a small one **there is no error at all** and the preview
is silently scrambled — measured at mean |diff| 107.6 (4:4:4) and 113.5 (4:2:0)
against a Pillow reference while reporting success.

Reproduced on real material: `illustration-108.jpg` in the Standard Ebooks
Beatrix Potter, the one non-interleaved file among ~1,900 JPEGs in the EPUBs on
this machine. It is 4:2:0 — which matters, because that is the case matcha's
patch refuses.

Fixed by `scripts/jpegdec_patches/0003-decode-non-interleaved-dc-scans.patch`:
re-derive the traversal from the luma component's own block grid, which is what
T.81 A.2.2 prescribes for a non-interleaved scan and which happens to be
JPEGDEC's existing 1:1 geometry, so the subsampled case needs no special
handling. Colour output is refused rather than composed against chroma that was
never read. Error integers are named at both decode sites now
(`lib/JpegToBmpConverter/JpegDecodeError.h`, the idea from matcha).

`test/progressive_jpeg/` — 7 tests, 4 of which fail against an unpatched
decoder. The four unaffected layouts decode byte-identically to before; the two
broken ones now match the interleaved encoding of the same image byte for byte.

**CrossInk's half was already present** — SOF2 detection, forced 1/8 scale, the
geometry through the scaler, all three, and via `getJPEGType()` rather than
re-parsing. Do not re-propose it.

**Still owed:** device confirmation that such a cover renders on Home and as the
sleep screen. The simulator cannot answer it — it swaps stb_image in for the
real decoder, and stb_image gets this layout right on its own.

### [T-020] Boot and sleep: mark only — DONE
**scope: ui · asked + done 2026-08-15**

Owner ask, in two passes: first "remove words so it is just logo and device
owner" for the sleep screens, then "remove crosspoint and booting words" for
boot.

The icon half needed nothing — `237cd034b` (PR #9, 2026-08-09) had already
regenerated `Logo120` from the striped uniform-paper cut, and both draw sites
point at it. Proven with rendered screenshots before touching anything.

**Sleep** (`SleepActivity.cpp`, both dark and light — one code path):
dropped `STR_CROSSPOINT` and `STR_SLEEPING`. Mark and owner name only.

**Boot** (`BootActivity.cpp`): dropped `STR_CROSSPOINT` and `STR_BOOTING`.
The **version string stays** — it was not named, and it is the only thing on
that screen saying which build just started.

All three of `STR_CROSSPOINT`, `STR_SLEEPING` and `STR_BOOTING` now have zero
call sites. **The translation entries stay in the YAMLs on purpose**: `StrId` is
a generated enum, so removing entries shifts every subsequent value, and that
churn is not worth reclaiming three short strings.

Verified by rendering all three screens headless through the real renderer —
boot, sleep-dark (mean brightness 49.3, genuinely inverted) and sleep-light
(246.1). Builds `-e default` and `-e simulator`.


### [T-007] Short-press power — RULED SLEEP, and a real mismatch fixed
**scope: fork sync · raised 2026-08-06 · closed 2026-08-15**

Opened because upstream issue #2863's short-press power behaviour is present in
this fork's code and upstream issues are invisible from this tracker.

**Investigating it found a live inconsistency, not just an untracked issue.**
`CrossPointSettings.h:271` initialised `shortPwrBtn = IGNORE` while
`normalizeRetiredSettings()` (`.cpp:169`) pinned it to `SLEEP` on every load.
Because the Controls tab is withdrawn from the device UI, nothing on the device
could reconcile them:

- a factory-fresh unit never runs `fromJson()`, so it kept `IGNORE` — the power
  button did nothing;
- any unit that had saved settings once got `SLEEP`.

That is exactly the "pinning is only half" trap CLAUDE.md documents, shipped.

**Owner ruling 2026-08-15: SLEEP on both.** The initialiser now reads `SLEEP`
and carries a comment saying it must stay in step with the pin. Side effect,
deliberate: `CrossPointSettings.h:403` shortens the power-hold window from
400 ms to 10 ms when the value is `SLEEP`, so a fresh unit now also gets the
short window.

Verified on a genuinely fresh profile — `settings.json` deleted, booted, then
the documented save cycle (enter Settings, press Back). File comes back with
`shortPwrBtn = 1`. Reading the value after boot without a save proves nothing
here, which is why the save cycle is the check.

Cold-cache builds for `-e default` and `-e simulator`.


### [T-013] A setting for 1-bit or 2-bit chrome fonts — RULED, keep 2-bit
**scope: display · ruled 2026-08-15 · no code change**

Asked for as a Settings option. **Measuring it changed the question**, twice
over, so the numbers are recorded here rather than re-derived.

**A runtime setting costs flash, it does not save it.** `--2bit` is a flag on
`fontconvert.py` at *generation* time (`convert-builtin-fonts.sh:81,124,149`),
and that script already notes at :132 that the 1x and 2x cuts must share a bit
depth, because the renderer blits both through one path. A live switch would
therefore need BOTH depths compiled in — roughly **+120 KB**, the opposite of
the intended saving. Only a build-time choice actually saves anything.

**What the depth is worth**, measured from `firmware.elf` with
`riscv32-esp-elf-gcc-nm --print-size`, summing the six chrome bitmap symbols
that `applySystemFont()` binds (Libre Franklin 8/10/12, regular + bold):

| Symbol | Bytes |
|---|---|
| `librefranklin_8_regularBitmaps` | 25,462 |
| `librefranklin_8_boldBitmaps` | 28,148 |
| `librefranklin_10_regularBitmaps` | 38,250 |
| `librefranklin_10_boldBitmaps` | 42,177 |
| `librefranklin_12_regularBitmaps` | 53,354 |
| `librefranklin_12_boldBitmaps` | 59,659 |
| **Total, 2-bit** | **247,050 (241.3 KB)** |
| 1-bit, projected | ~123,525 (120.6 KB) |

So the whole prize is **~120 KB, 1.9% of the 6.4 MB budget**, against a build
currently at 64.5% used — paid for by giving up the antialiasing that
`1a18de260` was merged to get. Owner ruling: **keep 2-bit.**

`folio-etc/folio`'s 1-bit `.cpfont` packing stays in
[docs/fork-ecosystem.md](docs/fork-ecosystem.md) as the reference if flash ever
becomes the binding constraint. It is not one today.


### [T-019] Withdraw three more Settings rows — DONE
**scope: device Settings UI · ruled 2026-08-07 · done 2026-08-15**

Clock Format, Sleep Screen Cover Mode and Sleep Screen Cover Filter withdrawn
from the device Settings screen. Pinned to the current field initialisers
(owner ruling 2026-08-15): `clockFormat = 0` (24-hour), `sleepScreenCoverMode
= FIT`, `sleepScreenCoverFilter = NO_FILTER`. Fresh installs therefore see no
change; only someone who had picked 12-hour, CROP or a filter is moved.

Done the withdraw way, not the delete way — all three rows keep their
`SettingInfo::Enum` entry with its `valuePtr`, so they still persist and stay
web-settable. `category` moved `STR_CAT_SYSTEM` -> `STR_CAT_DISPLAY`, the
retired category `rebuildSettingsLists()` drops.

**Step 3 was already satisfied** and needed no edit: the initialisers in
`CrossPointSettings.h:189,191,206` already carried exactly the pinned values,
so fresh and upgraded devices agree.

Verified two ways rather than by reading the file after boot, which the item
warned would pass regardless:

- Seeded `settings.json` with 1 / CROP / a filter, booted the simulator, and
  the file came back 0 / 0 / 0 with all three keys still present — pinned, and
  still persisting.
- Rendered the Settings screen headless: Clock Format no longer sits between
  Clock UTC Offset and Clock Synced, and neither Cover row appears. The
  separate `Sleep Screen` row is untouched.

Builds `-e default` and `-e simulator` from a cold cache.


### [T-018] The SDK pin — RULED, rebased and re-pinned
**scope: submodule · ruled + done 2026-08-15**

The pin sat on `fix/keyboard-alt-hint-dedupe` at `159f40f`: 9 ahead, **41
behind** `upstream/main`, no PR open, gap widening. Owner ruling: rebase and
re-pin, keeping the nine fork-only rather than sending them upstream.

All nine rebased onto `upstream/main` with **no conflicts**. New tip `8403043`,
now **0 behind / 9 ahead**. The pre-rebase tip is preserved as tag
`pre-rebase-2026-08-15` on the SDK fork, because the branch force-moved.

Verified against the rebased SDK before pinning: `-e default` (RAM 16.5%,
Flash 64.5%) and `-e simulator` build, headless run 0 errors. Flash grew
~4.8 KB — that is the 41 upstream commits arriving, not our nine. Also checked
the documented trap: `BleHidHost.cpp.o` exists at 523 KB with 46 nimble refs in
its `.d`, so the BLE include path survived.

**The nine stay fork-only by decision.** Not a candidate for re-proposal as
"should these go upstream" — that was asked and answered.

**2026-08-16 — the pin is now reachable from a permanent branch.** Two more
commits landed (the Return arrowhead work), taking the tip to `352098e`, and
for a while that commit existed ONLY on `fix/return-arrow-triangle`. A pin
reachable from a single feature branch is a live fragility: delete the branch in
a routine tidy and the commit becomes unreferenced, GitHub eventually collects
it, and `git submodule update` breaks for any fresh clone — with an error that
looks nothing like its cause. The SDK fork's `main` was fast-forwarded to
include it and the feature branch deleted. Verified after the fact:

```
$ git branch -r --contains 352098e
    origin/HEAD -> origin/main
    origin/main
```

**Check this before deleting any SDK branch:** if `--contains` names only one
branch and that branch is not `main`, do not delete it — merge it first.


### [T-011] Side buttons drawn on the UI — RULED, removed
**scope: ui · ruled 2026-08-15**

Owner observation from the simulator: side buttons visualised on screen, seen
on the Claude screen.

**Investigated first — it was deliberate, not a leak.** `drawSideButtonHints()`
is a long-standing theme method (`BaseTheme.cpp:165`, overridden in
`LyraTheme.cpp:479`), but only two call sites existed outside the themes, both
text-entry screens and both guarded by `!panel.isDaisy()`. Both arrived in
`46b0dd60f` (2026-08-06, keyboard UX pass), commented "same convention as the
full-screen keyboard".

**Ruling: remove them.** Too much chrome. The side buttons still navigate rows;
they are simply no longer labelled. A partial revert of `46b0dd60f`.

Both call sites deleted, and with them the 30 px `sideGutter` each screen
reserved *for* those hints — keeping the gutter after removing the labels would
have been strictly worse than before, paying the cost with none of the benefit.
The text column gets that width back.

`drawSideButtonHints()` itself is kept in the themes. It is theme API with no
remaining callers, not dead private code, and the reader's rotated hints still
exercise the same rotated-text path.

Verified: `-e default` and `-e simulator` build; note editor rendered headless
showing no hints and the keyboard grid using the full width.


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

**The key was served over the network by every build up to 2026-08-07, and the
SSID was public from 2026-08-03.** A scrub limits future exposure and retracts
nothing — anyone who cloned still has both.

**CLOSED 2026-08-17, owner: the key has been rotated.** The line above used to
end "Rotate the Anthropic key" and it stayed owed for ten days inside an entry
already marked DONE, which is where an action goes to be forgotten. Nothing else
about this entry changes: the file is still servable by design, the file
transfer server still has no authentication, and AP mode is still an open
network. That remains a known, accepted risk on this fork — do not "fix" it
without asking.

