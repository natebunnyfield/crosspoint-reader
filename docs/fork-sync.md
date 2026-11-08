# Keeping this fork in sync

`natebunnyfield/crosspoint-reader` is a **selectively divergent** fork, not a
tracking mirror. As of 2026-08-03 the fork's branch was 91 commits ahead of
`upstream/develop` and 2 behind. The gap is permanent and intentional, so the
usual advice — "just merge upstream regularly" — is actively wrong here.

**The fork's branch is `main`; upstream's is `develop`.** Renamed 2026-08-04 as
an owner convention — the fork has never had a `develop`/`main` pair, so there
is exactly one branch and `main` is it. The asymmetry is the thing to watch:
`upstream/develop` below is NOT a typo and must not be "corrected" to
`upstream/main`, which does not exist. Upstream also has a `master`, which is
what `.github/workflows/ci.yml` still triggers its push job on — inherited, and
it has never fired on this fork.

Run `scripts/repo-status.sh` to see where everything stands. It only reports;
it never merges.


## Where the backlog count lives now

**B-002 was closed on 2026-08-19** and its content moved here, which is where it
should always have been: an unmerged upstream backlog is a standing cost of a
deliberately divergent fork, not a defect, and a bug entry that can never close
only makes the open count lie.

Measured 2026-08-19: **540 ahead / 66 behind** `upstream/develop`. It grows at
roughly a commit a day, so any number written down is wrong within the week —
re-measure with:

```
git fetch upstream && git rev-list --left-right --count main...upstream/develop
```

Both commits B-002 named are resolved. `9c48609f` (bookmarks survive
re-pagination) needs `lib/KOReaderSync/`, deleted here with bookmarks;
`0f747b82` (content-based sync positions) is superseded by this fork's own
`section.bin` v35 word-anchor LUT, which gives the same guarantee by recording
source byte positions rather than counting visible characters.

Two cautions, both learned the hard way:

* `scripts/repo-status.sh` **overstates** the backlog — a commit already applied
  by hand still counts as unmerged (6 of 45 on the last pass).
* Never probe applicability with `git cherry-pick -X ours`. It reports conflicts
  as clean.


## Intentional divergences from upstream's scope

These are deliberate. They are listed here so a future sync does not "fix" them
back, and so nobody sends them upstream as a PR.

| What | Upstream says | Why it is here |
|---|---|---|
| `src/notes/` note editor (Create Note) | Out-of-scope: *Interactive Apps*, *Writing / Authoring Tools* | Built on purpose; ships in every device binary |
| `src/notes/ClaudeChat.cpp` | Out-of-scope: as above, plus the connectivity freeze | On-demand only — the owner starts it, it asks one question, it stops |
| Reading of the connectivity rule | Three upstream passages disagree: §3 "temporary freeze", Out-of-Scope "RSS/news/browsers", ROADMAP "active connectivity features" | This fork takes the narrow one: no always-on radio, no feed readers; owner-initiated Wi-Fi is fine |

`SCOPE.md` and `ROADMAP.md` carry upstream's text verbatim with a fork banner at
the top, rather than edits threaded through the body. That is deliberate: a
banner conflicts once and obviously, whereas rewritten bullets conflict on every
upstream change to that section and quietly misrepresent upstream's policy in
the meantime.

## Never blind-merge upstream

This fork has **deleted whole subsystems** that upstream still develops:

| Removed | Removal commit |
| --- | --- |
| KOReader sync, Calibre, the status bar | `08d5bdee` |
| Bookmarks, auto page turn | `e0509aef` |
| The reader menu | `9494d88e` |
| The tabbed Text Settings editor | `9fdd7dfe` |

`git merge upstream/develop` resurrects them. Attempted 2026-08-03: **18
conflicted files**, six of them `modify/delete` — git reinstating
`lib/KOReaderSync/`, `src/util/BookmarkFile.cpp`, `src/BookmarkEntry.h`,
`EpubReaderBookmarksActivity.cpp`, `KOReaderSyncActivity.cpp` and
`TextSettingsPreview.cpp` because upstream edited files this fork removed.
Resolving that by taking "theirs" silently undoes four deliberate refactors.

So: merge upstream **by hand, per commit**, or not at all.

## How to actually take an upstream change

1. `scripts/repo-status.sh` lists each unmerged upstream commit as `N/A` or
   `REVIEW`. `N/A` touches only removed subsystems — skip it and move on.

   **The count is an upper bound, not the real backlog.** It counts by
   ancestry, so anything applied without recording the merge still shows as
   unmerged. Measured 2026-08-15: six of the reported 45 produced an *empty*
   cherry-pick. Check before working a commit —

   ```bash
   git cherry origin/main upstream/develop            # patch-id; '-' = present
   git cherry-pick --no-commit <sha> && git diff --cached --stat   # empty = present
   ```

   The two disagree (each caught cases the other missed), so run both.
   **Never probe with `git cherry-pick -X ours`** — it resolves conflicts by
   discarding upstream's side and exits 0, so conflicting commits report clean.
   That mistake once turned 9 genuinely-clean commits into a map claiming 28.
2. `REVIEW` means the commit straddles live and removed code. Read it before
   doing anything: `git show <sha> --stat`.
3. Take only the live hunks. `git cherry-pick -n <sha>` then unstage the
   removed paths, or apply the specific files with `git checkout <sha> -- <path>`.
4. Build and, where the change touches layout or caching, bump the cache
   format version — see `docs/file-formats.md`.

### Standing rulings — N/A by decision, not by subsystem

`repo-status.sh` classifies by which files a commit touches, so a commit that
is live code but unwanted keeps reappearing as `REVIEW` forever. Those are
recorded here instead.

- `b1d53ba81` "add orangutan translation (#2992)" — **N/A, owner ruling
  2026-08-15.** This fork ships two languages deliberately; a novelty third is
  out of scope. Declined on scope, not on risk: the commit is mechanically
  safe, since `_order: 34` appends and no persisted `language` index shifts.

- **TeX Gyre Heros' tight `at` — accept, owner ruling 2026-08-24.** He reported
  it ("treatment has `t`s that are very close"), it was measured, and the fit
  is the face's own: 35/1000 em at that junction against Libre Franklin's 107,
  plus a `t` whose 16 px bitmap eats its own right sidebearing. Nothing in this
  pipeline is implicated — the ASCII kern shortcut is exact across 79,524
  pairs. Offered loosening pairs and dropping the family; he chose neither.
  Heros ships unmodified and this does not reopen.
  Full measurements: `docs/heros-letterfit-2026-08-24.md`.

### The two currently outstanding (2026-08-03)

Both are `REVIEW`, both are genuinely mixed, and neither is a drive-by:

- `9c48609f` "Make bookmarks survive re-pagination" — live parts touch
  `Section.cpp/.h`, `Page.h`, `ChapterHtmlSlimParser.cpp`,
  `EpubReaderActivity`; the rest is bookmark/KOReader code this fork deleted.
- `0f747b82` "make EPUB sync positions content-based (#2805)" — live parts
  touch `ParsedText`, `Section`, `ChapterHtmlSlimParser`, `EpubReaderUtils.h`
  and three docs; the rest is KOReader sync.

Both land in the Epub layout engine, which this fork has already changed (the
per-page word-anchor LUT, `section.bin` v35). Treat them as real work, not
housekeeping.

## Sync of 2026-11-07/08 — what was taken, what was not, and the backlog

Owner: *"commit, push, pull in all upstream work"*. Measured before starting:
**949 ahead / 466 behind** `upstream/develop`; the merge base is `1a7f5a9e3`
(2026-07-31), so everything since has come over by cherry-pick only. `git
cherry` put **459** commits genuinely absent (7 already present by patch id).

**Triage (scratch script, reproducible from the commands below).** 4 touch only
the removed prefixes (`repo-status.sh`'s list); 63 name an out-of-scope subject
(themes, KOReader, PDF, CI/docs churn — reviewed by subject, not taken); 392 were
candidates. A sequential `cherry-pick --no-commit` probe of all 392, in upstream
order, on a throwaway worktree off `origin/main`, with `freeink-sdk` pointer
moves dropped (the SDK is a fork branch here): **42 apply clean, 350 conflict**.

**Taken: 36 of the 42** (39 picked, 3 reverted below), each with `-x`, on branch `upstream-sync-2026-11-07`
(fixes, perf, build pins, bidi/keyboard UTF-8, XTC cover grayscale, progressive
JPEG patch, font interval dedupe, `USE_SPI_ARRAY_TRANSFER=1` for ~3x SD reads,
web upload niceties, docs/templates/license).

**Held for the owner (applies clean, but a user-visible choice):**
- `99c2f09d1` vector font (.ttf/.otf) upload in the web server — **RULED
  2026-11-08: SKIP, together with the renderer it needs, `aaf683798` ("Add
  TrueType fonts on PSRAM boards", 43 files, +2,161).** The campaign has not
  reached `aaf683798`; when it does, skip it on this ruling. Facts the owner
  ruled on: upstream compiles the TTF path out without PSRAM
  (`lib/EpdFont/VectorFontSupport.h`, `BOARD_HAS_PSRAM`), so it would be
  Picco-only; it needs FreeInkFont (FreeType) from `freeink-sdk/libs/font`,
  which the fork's pinned SDK `1dc07f67` lacks; the gain is skipping the
  builder (whole font, any size, drop-in), the cost is on-device outline
  rasterizing (speed unmeasured), losing the `.cpfont` tuning (synthetic
  styles, `baseline_shift_em`, ramps), a Picco-only look, ~110 KB flash.
  Owner chose "Skip both".
- `965722cf8` Libron in Manage Fonts downloads — **RULED 2026-11-08: SKIP.**
  Owner, after seeing the same page of *Anne of Green Gables* set in Libron, Edgar,
  TeX Gyre Schola and LibrisADF by the firmware's renderer: *"libron is too
  ugly"*. Do not re-offer Libron. Proof page (tailnet):
  `soup.taild108cf.ts.net/proofs/2026-11-08-libron/`.
- `bde0809a4` build release candidates from `develop` — a branch this fork does
  not use.

**Applied clean but reverted on this branch — they need code from conflicting
upstream commits:**
- `2a8c1ec55` cache-clear warning inside the dialog: calls
  `OptionPopup::showMessage`, which arrives in a conflicting commit.
- `0a9077722` X3 BQ27220 design capacity: needs `RenderLock::Mode::Try` and
  `BatteryMonitor::loadDesignCapacity`, likewise.
- `e31dcbb08` "don't recompile Arduino IDF libs": **REJECTED, do not retake
  without a ruling.** Its `scripts/patch_pioarduino_cache.py` REWRITES the
  shared pioarduino builder (`~/.platformio/platforms/espressif32/builder/
  frameworks/arduino.py`) for every checkout on the machine, and writes the
  `sdkconfig.defaults` reuse marker per chip family — the exact Picco/Sticky
  build trap in `docs/picco-port-2026-11-05.md` (both are esp32s3). It also
  broke the clean X4 build (`.dummy/sketch.cpp: Arduino.h`). The builder text
  was restored by hand and its `sdkconfig.crosspoint` removed the same night.

**The backlog that remains (as of `818a15400`, 2026-11-07):** the 350
conflicting firmware commits, the three held above; **simulator** upstream: 39
absent by patch id, 10 clean (mostly stubs for upstream firmware features this
fork does not have — SD plugins, clippings, `HalMemory`), 29 conflicting;
**freeink-sdk** upstream (`Free-Ink/freeink-sdk`, the fork's parent): 335 absent
by patch id against the pinned `1dc07f6`, many for boards this fork does not
build. Taking the conflicting set is a per-commit, by-hand campaign under the
rules above.

**Owner ruling 2026-11-08: "All of them".** Every applicable upstream commit is
taken, by hand, in upstream order, in batches, each batch built (X4, Picco,
simulator), gated (`tools/latency/gate.sh`, host tests) and pushed before the
next. "Applicable" still means the standing rules: hunks in removed subsystems
are dropped, scope rulings above stand (the three held commits stay held until
asked), and every skip is recorded below with its reason. Progress is logged
per batch under "Campaign log".

**Owner ruling 2026-11-08, FUI lists: "Adopt FUI".** Upstream `9e3cea2e5`
("convert all lists and tabs to FUI", 89 files) is taken: every list and tab
screen moves onto FreeInkUI's `UiListActivity` / `UiTabListActivity` /
`UiAppHost`. It is ported by hand as its own work item, and the fork's rules are
then re-applied on top of it, not dropped: cursor moves fire on PRESS with hold
paging (`docs/ui-conventions.md`, 2026-11-07), the press-vs-release Confirm
inventory, `OptionPopup` as the one modal, the Lyra Six list metrics, and the
lag work (windowed presents, no duplicate renders). The 27 later upstream
commits built on it are then taken normally.

### Campaign log

**Driver, from 2026-11-08 10:30:** the session "Planck play button mouse
clicks" (worktree `.claude/worktrees/campaign-b3`, branch `campaign-b3`) is
taking, in this order: the `a757b9d10` reader-base port (ruled "Adopt, keep
fork behavior") with `63fcbab02`, then batch 3 onward from `62e84a16f`. Reader port, batch 3 and batch 4 are pushed;
batch 5 (after `e33e3cf39`) is next, same driver. The
`81e192709` light-sleep revert is left to whoever gets the owner's answer. A
second session starting a batch or the reader port should fetch `main`, read
this line, and take something else (or nothing) to avoid porting the same
commits twice.

**Taken EARLY, outside the batches (2026-11-08, owner: "yes to any bug fixes",
fork survey §0).** The campaign driver should mark these ALREADY when its batch
reaches them, not port them again:

- `149dd53299` chunked zip size lookup for huge spines -> "fix(epub): chunk the
  zip size lookup so huge spines do not abort".
- `10cb2c3b29` try-lock for the reader's idle work -> its `RenderLock::Mode::Try`
  half only, in "fix(reader): loop() reads section only under a tried render
  lock…". The `main.cpp` hunk (loop skips a frame while the lock is busy) was
  NOT taken.
- `f8936ba67c` -> the early-stop half only ("fix(reader): a press stops the
  background chapter build…"); the deque and finish-in-idle halves were NOT
  taken.
- SDK `69349ec` ("Remove dark background hint feature", taken from upstream
  earlier) is REVERTED in the fork's SDK (`c7fc361`) and the hint is public;
  an upstream SDK move that deletes it again must not be taken blind.
- Not upstream commits, recorded here because the SDK pointer moved:
  `freeink-sdk` now points at `e05bb17` (Picco frontlight table, Picco battery,
  TMA525C track-id guard and its two-finger latch, UC8279 0x03/0x67, the dark
  re-drive), on top of the `picco-a-sdk` pin `1dc07f6`. A later SDK bump must
  carry these six.


| batch | upstream range | taken | partial | skipped (why) | pushed as |
|---|---|---|---|---|---|
| 1 | `0f747b82a`..`9e3cea2e5` (25) | 6 (`046827fc3` `1582e70ef` `596f0d3f7` `e4aae7bae` `2cac59714` `86eb1cbde`) | 3 (`5eec70de3` IPA except the two `reading`-set recipes; `d4733bfc6` CI split only, release.yml and sticky envs dropped per B-004; `7f8b005ae` AGENTS.md symlink only) | ALREADY 10 (incl. `d720891b2` dark mode = fork's whole-UI `c2d26ae29`; `4e6190357` X3 display/battery/crypto); SKIP 5 (`0f747b82a` superseded by word anchors, `9c48609f8` + `0423276d2` bookmarks/KOSync deleted, `fecb9eb3a` + `e9c3a166f` dictionary deleted in `1d37cb851`); FLAG 1 `9e3cea2e5` FUI -> ruled "Adopt FUI", ported separately | (this push) |
| FUI | `9e3cea2e5` (1, owner ruling "Adopt FUI") | | 1, by hand + 5 fork-rule commits (see "FUI port" below) | | (this push) |
| reader base | `a757b9d10` (1, owner ruling "Adopt, keep fork behavior") + `63fcbab02` | `a757b9d10` by hand as `69f7430aa` | | SKIP `63fcbab02` (TXT `skipPages()` end sentinel: the fork has no skipPages, chapter skip retired 2026-09-01) | `71dc70df5` |
| 2 | `e970abae8`..`57c692fe8` (40) | 10 (`e970abae8` `25cf99b21` `99724aa79` `618608e84` `6a501bba5` `c6fe67d48` `27f964b3e` `eef205044` `a4a03c6b4` `7e42d0707`) | 9 (`268c27f7d` Transparent sleep screen, appended as value 14; `48e39eb0e` reposition clearing on the fork's three-stage anchors; `4970ef1f6` PNG overlays + PNG viewer; `33f07db71` post-delete nav lock only; `bbca4886c` X3-UC8279 edge-button fix only; `d3b3b5669` dimension guard/8 MP/decode yield, not the image-margin clamp or single-base image refresh; `8eea7f865` Wi-Fi only; `a0daab994` font layer only; `93d572fcb` arabic preset only) | ALREADY 4 (`70faa29da` = `816011c42`; `e715532f9`; `080b1d719` = `ca1850a91`; `28377310d` fork has Wider/Widest); SKIP 15 (deleted subsystems: dictionary x2, status bar, Ubuntu UI face x2, font download; `b1d53ba81` ruling; `4d764de9c` press/release ruling; `413a1bd9d`+`151414d8c` revert pair; `81fd32237` would drop `.claude/skills`; `63fcbab02` needs `a757b9d10`; `b3d1e79d0` fixed-height Lyra rows; `75c079fe7` SDK-only; `da3d50c24` fork's own table columns); FLAG 2 (`a757b9d10` reader consolidation, `81e192709` light-sleep revert). Caches: section 67 -> 70 (renumbered when rebased over page-space v67, `docs/page-space-2026-11-08.md`), book.bin 11 -> 12 | (this push) |
| 3 | `62e84a16f`..`25bbfae76` (40) | 5 (`5182d27e2` `b6746c1df` `c484dc72b` `8141d7b9c`+`820d0b96a`) | 5 (`cdbe2a779` en/es only; `8bc1603e8` no SDK bump; `86976ab23` log only, fix ALREADY; `790a0817e` guard restores the prior mode; `52444a0c9` block-close insets, `!important` ALREADY) | ALREADY 6 (empty picks); REJECTED 1 (`e31dcbb08`); SKIP 16 (deleted subsystems, other boards, Swedish, fork Settings/list chrome; see "Batch 3" below); HELD 4 (frontlight panel x4, X4 Classic... see below) + RULED: wake rule x2 SKIP ("go with rec"), USB MSC BUILD for the Picco ("build"). Caches: section 70 -> 71, CSS stays 11 | (this push) |
| 4 | `84fcff848`..`e33e3cf39` (50) | 11 (`309923156` `ce6c6cbce` `5fcd26330` `93b6fe111` `b5fb406f5` `03c484778` `6eda8f0b7` `3e60e1302` `c33a8b0e8` `c80c537f2` `0475e6938`) | 7 (`badfa95ff` `365618e47` `9d2f234e1` `54337e6d7` `b7bceb56c` `f437bb506` `2b2ba72f0`; see "Batch 4" below) | ALREADY 7; SKIP 18 (deleted subsystems, other languages, fork-curated font ids); HELD 7 (keyboard layouts, link tapping, language fonts, pioarduino bump, grayscale SDK series x2, X4 Classic CI). Plus a fork fix from `da3d50c24` (skipped-subtree close guard). Caches: section 71 -> 77, book.bin 12 -> 13, CSS 11 -> 12 | (this push) |


### FUI port (`9e3cea2e5`), 2026-11-08

Upstream's "convert all lists and tabs to FUI" taken by hand, then the fork's
rules re-applied as separate commits (press-fired list moves, the Confirm
press/release inventory, Lyra Six painting the rows, a lag-gate check, the
Picco's input rules). Each commit message carries the detail; the shape:

- **On the base (`UiListActivity`):** Settings (flat, so not
  `UiTabListActivity`), Typography, the time zone list, Browse Files, Recent
  Books, both chapter lists, footnotes.
- **Fork adaptation of the base:** rows are described one at a time
  (`describeRow`) and only one page is described per render (`drawRows`),
  instead of upstream's prebuilt `ListItem` vectors; the rows are painted by
  `GUI.drawList` (Lyra Six) with one FUI hit rect per row, so the look and the
  paged layout are unchanged; `activePopup()` hosts `OptionPopup`.
- **Not converted (each on its own loop, with the reason in the port
  commit):** Wi-Fi selection, Manage Files, the interval slider, the font
  pickers, the colophon. `Activity::handleListTouch` and the list hit-test
  helpers stay for the first two.
- **Dropped from the commit:** OptionPopup's FUI rewrite and the FUI
  `drawHeader` (both change the Lyra Six look), BoardConfig viewable insets
  (same values; would pull BoardConfig into host tests), the edge-swipe
  refactor, every hunk for deleted subsystems, the reader's latched page turn
  (not list work; unevaluated against the fork's own turn path).
- **Evidence:** simulator screenshots of Home (both pages), Recent Books,
  Browse Files, Settings, the Reader Font picker and a chapter list are
  ink-identical to `origin/main`; a scripted session gives the same refresh
  count, frame hashes and present windows. `tools/latency/gate.sh` now also
  fails a Settings row move that is not a windowed present.
- **Open:** three new translation units (`UiListActivity.cpp`,
  `UiTabListActivity.cpp`, `UiAppHost.cpp`) make the simulator's iOS source
  list stale: regenerate `crosspoint-simulator/cmake/CrossPointSources.cmake`
  (CLAUDE.md, "Adding or deleting a translation unit"). Device builds (X4,
  Picco) were not run for this item.


### Batch 2 gates (2026-11-08, after rebasing onto `3487265ff`)

**Pushed as `ad6f614ce`** (21 batch commits rebased onto main, plus the gate
record). The branch `upstream-campaign` (worktree `.claude/worktrees/upstream-sync`)
still points at the PRE-rebase copies, head `ec1fd5f00`, whose section bumps
read 67-69. Do not push it or rebase it: point it at `origin/main` first. Batch
3 starts at upstream `62e84a16f` (392 non-merge commits after `57c692fe8`
remained on 2026-11-08). Both owner decisions are ruled (2026-11-08, below): `a757b9d10` "Adopt, keep
fork behavior"; `81e192709` "Keep, guard USB".

Run by a second session once the campaign session had stopped with batch 2
committed but unbuilt and unpushed. On the rebased tree: `pio run -e default`
(X4) SUCCESS, `pio run -e picco` SUCCESS, `pio run -e simulator` SUCCESS,
`tools/latency/gate.sh` OK (14 turns, 2 Home entries, 2 Settings moves), host
tests 786 of 787 pass. The one failure is `ActivityInputTest` NOT BUILT, and it
is not batch 2's: `src/network/SavedWifiJoin.h` gained `WiFi.mode/begin/
disconnect/setScanMethod/setSortMethod` calls in `d1f0f33cc` (2026-10-20) that
`test/activity_input/stubs/WiFi.h` (last changed `024f1a30f`, 2026-09-06) never
stubbed, so that suite has not compiled since 2026-10-20. Open, not fixed here.
The first X4 attempt died on `sdkconfig.h: No such file or directory`, the
fresh-worktree scaffold fault in `docs/contributing/device-build-from-a-clean-tree.md`
(fault 1); clearing it as that doc says fixed it.

### Batch 2 (2026-11-08): held for the owner, and what the SDK still lacks

Report: the campaign scratchpad `batch-02-report.md`; each commit message
carries its own detail. Recorded here because each is a decision, not a
port:

- **RULED 2026-11-08 — `a757b9d10` reader consolidation: "Adopt, keep fork
  behavior".** Taken, ported by hand like FUI, and the fork's reader behavior
  re-applied on top and proved unchanged: press-fired page turns, the
  side-button chords, Find, the fork's end-of-book screen, no reader menu or
  status bar, deferred anti-aliasing, the next-page prerender, the idle
  prewarm and its touch gating. `63fcbab02` then follows. (Original flag
  below.)
- **FLAG `a757b9d10` "consolidate reader activities"**: turns
  `ReaderActivity` into an abstract base the Epub/Xtc/Txt readers derive from
  (shared loop, refresh cadence, end-of-book options, back navigation). The
  fork's readers diverge heavily there (press-fired overlapped turns, chords,
  Find, its own end screen, no reader menu), so it is an architecture
  decision. `63fcbab02` (TXT `skipPages` end sentinel) waits on it.
- **RULED 2026-11-08 — `81e192709` light-sleep revert: "Keep, guard USB".**
  Not taken. Upstream's reasons, from PR #3077's discussion (not its commit
  message): devices freezing when USB was plugged in, a user who could not run
  without USB connected, and an unreproduced ~40 mA idle report; the revert put
  an X3 back at 9.79 mA idle. The fork keeps light sleep (upstream's own X3
  figure: idle 9.68 -> 2.78 mA) and the work built on it, and guards the
  plug-in path instead: the X3/Picco power check polls every 200 ms (was 1 s),
  light sleep stays off for 3 s after USB was last seen by either signal, and
  both nap paths (`HalPowerManager::lightSleep`, the e-ink BUSY slice) use
  `HalGPIO::isUsbSleepGuardActive()`. The SOF heartbeat stays in the guard: on
  an X3 with a full battery the BQ27220 charge-current check reads "no USB",
  and the heartbeat is the only signal. UNCONFIRMED on device; if a device ever
  hangs on plug-in, this path is the first suspect. (Original flag below.)
- **FLAG `81e192709` "revert light sleep code but keep battery drain fix"**:
  upstream withdrew #2525; this fork kept it (`816011c42`, [T-017]) and built
  on it (Picco slices `f09e426e2`, the lag plan). The part upstream kept is
  already here (`EpubReaderActivity.h` `skipLoopDelay`).
- **Held from `bbca4886c` (X4 Pro / PaperMono)**: the two board targets and
  their CI/release jobs, and the frontlight quick panel with its top-edge
  swipe, which would also reach the Picco (it has a frontlight). Only the
  X3-UC8279 edge-button fix was taken.
- **Held from `d3b3b5669`**: upstream's single base refresh for image pages
  in place of the double FAST; the fork's image path is the lag-plan F7
  shape and wants a device comparison first.
- **Not taken, no ruling needed:** `66a7ae10e`/`d72ae2ac8` restyle the
  Ubuntu UI face this fork no longer ships (Libre Franklin is pinned).
- **freeink-sdk commits upstream's pointer moves reach that the pin lacks**
  (`git cherry` against `1dc07f6`), worth a look on the SDK fork:
  `43b621e` (drop a redundant `exists()` in `openFileForRead`, a saved SD
  call per open), `5c01a9c` + `a4976be` (perceptual gamma for the
  frontlight; `HalFrontlight` drives `FrontlightManager` (Picco only, per its fork note), so this likely moves
  the Picco's brightness curve). The rest are touch, list-layout (`fui::list`,
  unused here) and other boards.

Commands:
```bash
git fetch upstream && git cherry origin/main upstream/develop      # '+' = absent
git rev-list --reverse --no-merges origin/main..upstream/develop   # probe order
# per commit, on a throwaway worktree: cherry-pick -x --no-commit; drop the
# freeink-sdk path; CONFLICT -> abort; empty -> present; else commit
```

### Batch 3 (2026-11-08): `62e84a16f`..`25bbfae76`, 40 commits

Probed one at a time against `69f7430aa` on a throwaway worktree, ported in
upstream order on `b3-work`, then rebased onto `a5e076efd`. Each commit
message carries its own detail; the decisions:

- **TAKEN (5):** `5182d27e2` htmlToPlainText heading breaks (with its
  `HtmlToPlainTextTest`; the dictionary caller is gone but the converter is
  still built); `b6746c1df` CSS cache retry, merged by hand. The fork's heap
  scratch, the heap floor (it now also stops rule growth), book notes and the
  stale-cache delete are kept; `CSS_CACHE_VERSION` 11. `c484dc72b` sparse
  kerning and Zopfli: the fork's larger `convert-builtin-fonts.sh` matrix kept,
  `--zopfli` on every `--compress` call, and all 169 tracked headers
  regenerated, because `EpdFontData` gains fields mid-struct and the headers
  initialize it by position. That includes the 32 retired-family 2x/3x headers
  the script no longer makes. `8141d7b9c` style-aware prewarm, with
  `820d0b96a` folded in; the new suite gets a one-macro `HalDisplay.h` stub.
- **PARTIAL (5):** `cdbe2a779` language ordering. Taken: `gen_i18n.py`, the
  `_bcp47` codes for en/es and `docs/i18n.md`; the other 31 yaml do not ship.
  `8bc1603e8` USB detection by charging state: the HalGPIO fallback is taken,
  the SDK pointer is not (`isCharging()` exists at `1dc07f6`). `86976ab23`
  ZipFile underflow: the fix was ALREADY here, and only the `LOG_ERR` is new.
  `790a0817e` Wi-Fi power-save off during downloads, with one fork change: the
  guard restores the mode it found, because `CrossPointWebServer.cpp:171`
  turns sleep off for its session and a fixed `WIFI_PS_MIN_MODEM` restore
  would undo that. It needs `esp_wifi_get_ps` in the simulator's stub (pushed
  there). `52444a0c9` paragraph spacing: the block-close restart drops the
  parent's top and bottom insets (section **v71**). Its `!important` half is
  ALREADY: every property path here strips it already.
- **ALREADY (6, empty cherry-picks):** `f58f9b89f` `6c5bc307d` `8665dd7b9`
  `ecc0c4f29` `88ddc9ad0` `a56a7f07a`.
- **REJECTED (1):** `e31dcbb08`, standing ruling 2026-11-07.
- **SKIP (16):**
  - Deleted subsystems: `5a6f3f57f` (the restored long-press shortcuts are
    all deleted subsystems), `b4cd1af6b` and `e7a3bb488` (reader menu),
    `14cdb288b` and `b42927b4f` (KOReader sync), `68be3aec7` (Calibre),
    `7a4c96dd3` (five deleted screens), `dc65ccd9d` (the Manage Fonts
    downloader doc).
  - Fork already differs: `62e84a16f`. Refresh "Never" targets
    `refreshFrequency`, which the fork replaced with `refreshCadence`; night
    mode is the fork's whole-UI dark mode; the touch default is kept by the
    Picco gesture rulings.
  - Settings, UI faces and list chrome: `139de1e76` (Settings is one flat FUI
    list here, with no tab band to hand focus from); `dc7bb07bd` (the Ubuntu
    faces are gone, `main.cpp:86`); `3b73e7fce` (reader-menu style; its list
    margins need SDK `setContentMarginFromScreen`, absent at the pin);
    `f24ef2095` (deletes the theme list drawing that the FUI port still
    paints rows with).
  - Other boards and languages: `f0c655062` and `4638119b2` (X4 Pro),
    `e82b51740` (Swedish).
- **HELD and RULED:**
  - The frontlight quick panel (the held remainder of `bbca4886c`), with
    `8575b1427`, `4f0b4b70f` and `25bbfae76` built on it. Decision: does the
    Picco (it has a frontlight) get the panel and its top-edge swipe?
    **RULED 2026-11-08: NO.** Owner: *"no quick panel"*. Skip the panel,
    its swipe, and `8575b1427`, `4f0b4b70f`, `25bbfae76` with it.
  - **RULED 2026-11-08: the wake rule, `cdd89b1d7` with `e37164ca9`. Keep the
    fork's wake.** Owner: *"go with rec, but make an option setting if it is
    needed"*. No setting is needed. `shortPwrBtn` is pinned to `SLEEP`
    (`CrossPointSettings.h:376`), so `verifyPowerButtonWakeup()` takes its
    `shortPressAllowed` fast path (`HalGPIO.cpp:414`) and every power-button
    wake is accepted at once, which is the short-press wake the owner keeps.
    (The question first told him a hold-duration check was live; it is not,
    and the ruling holds with that corrected.) Not taken from upstream: the
    10 ms "physically pressed at first sample and after debounce" check that
    sends a spurious wake back to sleep, which needs SDK
    `isPowerButtonPhysicallyPressed` (absent at `1dc07f6`), and
    `e37164ca9`'s narrower cold-boot classification, which would change
    whether a Picco cold boot sets `wakePowerReleasePending` (`main.cpp`,
    the PowerButton case). The USB-serial part is ALREADY (fork F8,
    `main.cpp:958`).
  - **RULED 2026-11-08: USB mass storage, `218dc6aaf` -- BUILD it, for the
    Picco.** Owner: *"build"* (asked with "skip" recommended, for the SD
    corruption risk of an unplug mid-write, B-073). Only the Picco can have it:
    its SD is native SDMMC (`platformio.ini:421`), which upstream's
    `HalStorage::beginUsbDrive()` requires (`#error` otherwise), and the
    ESP32-C3 in the X4/X3 has no USB OTG. Upstream enters it from
    NetworkModeSelection, deleted here, so the entry point and the screen are
    new fork work and visual-gated. The SDK fork has `UsbMassStorage` at
    `1dc07f6` but not `SDCardManager::detachFilesystemForRawAccess()`.
    Tracked as its own work item, not as a batch port.
  - The X4 Classic board target, `f021d1e5f`. It needs SDK board support the
    pin lacks; held with the other board targets.

Gates on the rebased tree: `pio run -e default` (X4) SUCCESS, Flash 4,853,635 B. That is 545,166 B (532 KiB) less than the reader-port gate on `69f7430aa` (5,398,801 B). Batch 3's only font or data change is `c484dc72b`, so most of that is likely its sparse kerning and Zopfli (inferred, not split per commit; upstream's figure was 323 KB, and the fork ships more built-in faces). `pio run -e picco` SUCCESS; `pio run -e simulator` SUCCESS; `tools/latency/gate.sh` OK (14 turns, 2 Home entries, 2 Settings moves). Host tests: the first run had four suites NOT BUILT. The CSS cache port calls `Storage.rename()`, which their suite-local `HalStorage` stubs lacked, and two of them tested `loadFromStream()` as a bool. Fixed in `test: four layout suites follow the CSS cache retry port`; after that, 884 of 884 pass. `ActivityInputTest` builds again (fixed in `fca3ea5e8`).


### Batch 4 (2026-11-08): `84fcff848`..`e33e3cf39`, 50 commits

A read-only agent triaged all 50 against `103df0a79`, and each TAKE/PARTIAL was
checked again before porting. Everything was ported in upstream order on
`b4-work`, except that `3e60e1302` moved up to sit right after `b7bceb56c`,
which its parsing depends on. Each commit message carries its own detail; the
decisions:

- **TAKEN (11):**
  - `309923156` Arabic Lam-Alef guard, with the guard moved into
    `applyLigatures()`, so `getLigature()` keeps reporting the face's table
    (the per-ligature switch reads it).
  - `ce6c6cbce` paragraph base direction.
  - `5fcd26330` README link.
  - `93b6fe111` glyph-arena retry.
  - `b5fb406f5` ETag / Cache-Control.
  - `03c484778` web path normalization, safe names and the files-page XSS fix
    (security).
  - `6eda8f0b7` the `hidden` attribute.
  - `3e60e1302` monitor PSRAM subplot.
  - `c33a8b0e8` unique ownership of page elements (ported by hand in its own
    worktree).
  - `c80c537f2` font-cache fragmentation. Upstream measured the back-turn on an
    X3 with idle prewarm running: 1,908 -> 1,720 ms median. The fork keeps its
    12 KB largest-block retention floor, and the hi-res companion takes the
    same accumulate flag.
  - `0475e6938` ROADMAP.
- **PARTIAL (7):**
  - `badfa95ff` vertical-align on internal links. Its test suite needs the held
    `7bcc3a692`.
  - `365618e47` Spanish only: the six keys whose fork value was the older
    upstream text. `STR_RESTORE_LIGHT_ON_WAKE`, from the fork's Picco port, is
    kept.
  - `9d2f234e1` Xtensa panic stack capture and `-DHAVE_MAX_FRAGMENT`.
  - `54337e6d7` the X4/X3-only warning in `fix-bricked-xteink.md`.
  - `b7bceb56c` per-render image failure memo, inflate OOM logs and the PSRAM
    MEM line. No `HalMemory` unit: `ESP.*` already reads `MALLOC_CAP_INTERNAL`.
  - `f437bb506` NFD Hangul composition, with display copies in Browse Files
    and Manage Files; book.bin v13.
  - `2b2ba72f0` `list-style-type: none`, plus the fork's `list-style: none`
    shorthand; CSS cache v12. Numbering was ALREADY here.
- **ALREADY (7):**
  - `4a679d5d4`: the SD part is Picco-only by fork choice (`HalStorage.h:19-23`).
  - `9ba160863`: the idle branch light-sleeps and polls at 10 ms while a
    debounce is pending (`main.cpp:1516-1541`).
  - `483c5cf68`: the `RenderLock` it removes never reached the fork.
  - `77b53171e` `fe6535985` `c4d8c395d` `e33e3cf39`: empty picks.
- **SKIP (18):**
  - Deleted subsystems: `84fcff848` (dictionary), `c3534ed89` (reader-menu
    gesture; the gate it removes is a pinned constant), `a7df86fe2` (font
    download), `da7feed5c` (EndOfBookOptions), `2bad1d09f` (reader menu),
    `8a1a8769d` and `472b5e485` (KOReader sync).
  - Already different here: `8cb0a3908` (four UX fixes, each a deleted path or
    dead code here), `fb0523d36` (a revert of things never taken), `b95965f47`
    and `32a0cadbc` (Ubuntu and upstream font ids; the fork's `fontIds.h` is
    hand-curated), `20d67cb3e` (flat Settings, OPDS deleted), `46d912535`
    (upstream's release asset naming).
  - Version bump and other languages: `3b2032641` (version bump),
    `e6139861a` `233f93ffb` `304ad7897` `320a97a94` (other languages).
- **HELD (7), for the owner:**
  - `d5d2d67a5`: keyboard layout set with a globe key. A new Settings screen,
    and it needs SDK `02d32e6`.
  - `7bcc3a692`: tap a link to follow it. The fork made links non-interactive
    on purpose (`ChapterHtmlSlimParser.cpp:2189`), and it changes the page
    format.
  - `1f9523eb4`: 12 Hebrew/Arabic/Korean families for the ruled
    `sd-fonts.yaml` list.
  - `7db14a015`: pioarduino 55.03.311. It edits the REJECTED
    `patch_pioarduino_cache.py` and adds a C3 ROM-libc linker patch.
  - `1f3d7458a` and `e5dcc64fd`: the grayscale-capabilities SDK series,
    deferred with the boards on 2026-09-14. They need SDK `4b8188e`,
    `3080ca2`, ... and an X3 device check.
  - `71342eacb`: CI for the held X4 Classic (`f021d1e5f`).

**Found while porting, fixed:** a skipped (display:none / `hidden`) element's
close ran the full close path. `blockStyleStack.pop_back()` removed the
enclosing element's style, so the paragraphs after a hidden block lost the
container's margins (x 29 -> 0 in the test), and a hidden nested list ended its
parent list. The guard exists upstream inside `da3d50c24`, which batch 2
skipped whole for its table columns; this live part went with it, so
`da3d50c24` is now PARTIAL (the guard). It is committed separately, before
`2b2ba72f0`, as section v76, with `SkippedBlock.*` tests that fail without it.

**Found while porting, NOT changed (a limitation):** an undirected paragraph is
set RTL when any of its first three words starts RTL (`ParsedText.cpp`,
`RTL_PARAGRAPH_PROBE_WORDS`, upstream `f5bc554ae`). So `<p>Alpha <span
dir="rtl">HEBREW</span> omega</p>` comes out RTL with its words reversed. The
heuristic exists so an unmarked Hebrew paragraph that opens with an English
term reads right. A refinement would ignore RTL words inside an explicitly
`dir`-marked inline element.

**Caches:** section 71 -> 77 (one bump per layout change: v72 direction, v73
footnote sup, v74 Hangul, v75 hidden, v76 skipped close, v77 list-style none);
book.bin 12 -> 13; CSS cache 11 -> 12.

Gates: `pio run -e default` (X4) SUCCESS, RAM 58,724 B, Flash 4,854,239 B (batch 3: 4,853,635 B, so +604 B). `pio run -e picco` SUCCESS, RAM 70,872 B, Flash 4,660,042 B (batch 3: 4,659,342 B, so +700 B). `pio run -e simulator` SUCCESS. `tools/latency/gate.sh` OK (14 turns, 2 Home entries, 2 Settings moves). Host tests: 923 of 923 pass (884 at batch 3; the new suites are ligature_guard, inflate_stream, fs_helpers and sd_card_font, plus the new cascade_lines cases). The unique-ownership port (`c33a8b0e8`) was built and tested on its own branch first: 909 of 909, X4, simulator and the latency gate. Device behavior (font-cache timing, the Xtensa panic capture, TLS max fragment length) is UNCONFIRMED on device.


## One working copy per project

Multiple clones is what actually costs time here. The live pair is:

- `~/src/crosspoint-reader` — firmware. `origin` is
  `natebunnyfield/crosspoint-reader`; `upstream` is the project. There is no
  separate `fork` remote — confirmed via `git remote -v` (2026-08-30); an
  earlier version of this line claimed one existed.
- `~/src/crosspoint-simulator` — simulator. `origin` is
  `natebunnyfield/crosspoint-simulator`, `upstream` the project.

Other clones exist and are **not** working copies: `~/src/crosspoint` and
`~/src/xteink/crosspoint-reader` both point `origin` straight at *upstream*,
so a push from either targets the upstream project rather than the fork.
`~/src/pluspoint-reader` is ngxson's separate OS rewrite.

Before deleting or consolidating any clone, check it for uncommitted work.
On 2026-08-03 `~/src/xteink/crosspoint-reader` held 451 lines of a ring-clock
activity that existed on no branch and no remote and would have been lost;
it is now `rescue/ring-clock` on the fork.

**`gh` follows `origin`, not this doc's advice — a bare invocation in this
checkout resolves to UPSTREAM.** `gh run list`, `gh pr list`, and
`gh api repos/...` with no `--repo` all default to the repo `gh` infers from
context, which for the `~/src/crosspoint-reader` clone is
`crosspoint-reader/crosspoint-reader` (upstream), not the fork — `origin` here
being the fork doesn't change what `gh` picks. Always pass
`--repo natebunnyfield/crosspoint-reader` explicitly, including inside
`gh api` endpoint paths (`gh api repos/natebunnyfield/crosspoint-reader/...`).
This already caused one near-misdiagnosis: [B-041], 2026-08-29.

## Submodule

`freeink-sdk` is pinned at a commit ahead of its own `main`
(`heads/main-25-ge514a86`). A fresh clone that skips it builds against the
wrong SDK with no obvious error:

```bash
git submodule update --init --recursive
```

`repo-status.sh` flags the submodule when it is uninitialised (`-`) or has
drifted off the pinned commit (`+`).

### 2026-09-14 — 17 display commits taken; CHERRY-PICK, do not rebase

We were at `352098e` (2026-08-15): **10 ahead / 151 behind**, the 10 all local
`KeyboardPanel` icon work. 44 of the 151 are display.

**A full rebase is the wrong move and this is the reason.** Upstream reworked
the same keyboard component our 10 commits live in — it solved the alt-hint
collision by reserving vertical headroom where we inset the hint into its
corner — so every one of our 10 conflicts, each needing a visual judgement call
about a keyboard the owner has already signed off. That is real cost for no
display benefit. Cherry-pick the area you actually want onto our head instead.

**Mechanics that matter if you repeat this.** Filter merge commits
(`git log --no-merges`) or the first merge in the range stops the run looking
exactly like a conflict. Of 37 non-merge display commits, **18 applied clean**.

**The 19 that did not are one coherent group, not 19 problems**: new board
support we do not ship — EEGO A4, Xteink X4 Classic, Waveshare
ESP32-S3-ePaper-3.97 — and the `grayscale-capabilities` series
(`3080ca2`, `70102f0`, `4a69a29`, `a3db714`, `d02deed`, `3c6e110`, `6644bf2`,
`5916724`, `2cca22f`, `4b8188e`, `39606d5`) which depends on those boards
landing first and mostly conflicts in `BoardConfig.h`. Taking them means taking
the hardware half too; that is a deliberate pass, not a cherry-pick.

**One of the 18 was dropped after it built red**: `208afe8`, the EpdBus shared-SD-rail
fix, references `BoardConfig::isOnePage` — an ESP32-C61 profile that is not our
hardware. Added by `3e5aff1`/`40503d5` in `libs/hardware/`, which we did not take.

Taken (our shas): `054f965` X3 skip POWER_ON when the UC8253 is already powered,
`ded0676` UC8279 WW/WB exchange for the X3 AA tables, `f409fb3` ghosting via
power-off after AA refresh, `44c6fa9` skip power-off when the screen is off,
plus the X4 Pro deep-sleep park and bounded wait, a UC8279 grayscale plane
inversion fix, and PaperMono window refresh.

Branch `sdk-display-2026-09-14` on our SDK fork. Verified: `default`,
`gh_release`, `sticky`, `simulator_x3` build; 751/751 firmware and 90/90
simulator tests. The panel behaviour itself is **device-only** — no simulator
shows ghosting or a power sequence.

### Standing ruling 2026-09-10 — PDF stays out of scope, autocrop framing considered and rejected

Filed as `T-030` on 2026-09-08 and **dropped the same week by owner ruling**:
*"Drop it — PDF stays out."* `ROADMAP.md` and `SCOPE.md` stand exactly as
written; nothing in either was edited.

**Recorded because the argument FOR it was a good one and will otherwise be
re-made.** The original ask was for PDF "in the most minimal way", same
navigation as epub, with margins autocropped — *"excluding noisy edges from bad
scans and dropping page numbers to increase zoom and readability"*. That framing
directly answers `SCOPE.md`'s stated objection, which is specifically about
*"constant panning and zooming"*: autocropping the margins is the mechanism that
removes the need to pan. It also does not ask for the "first-class" rendering
that `ROADMAP.md` excludes.

The owner heard that argument, with the option to carve out the exception in
both files, and **chose to leave the scope decision as it stands.** So this is
settled on the merits rather than by oversight, and a future session that
rediscovers the autocrop reasoning should recognise it as already considered.

If it ever reopens, the question that decides everything downstream was never
answered and should be asked first: whether the device rasterizes PDFs at all
(~380 KB RAM, no PSRAM, flash at ~81%), or whether conversion happens
off-device in `~/src/claude-tools`, where there is memory to crop well.

### Standing ruling 2026-08-15 — `pagination-right` dropped, not merged

The branch made the note editor's page counter a right-aligned, regular-weight
cell in the header instead of bold text concatenated onto the file path. It was
overtaken mid-flight by the owner ruling that **removed the Create Note title
bar outright, pagination included**, so its one caller no longer exists.

Merging it would have left `BaseTheme::drawHeader`'s new right-cell parameters
with zero callers — a speculative API, not a feature. The branch is deleted;
the ruling that superseded it is the one recorded above. If a page counter is
ever wanted in a header again, write it against the surface that needs it
rather than restoring this.

### Standing ruling 2026-08-15/16 — iconography sweep, and the palette dial

Two areas gained rulings worth not re-litigating.

**The custom-iconography sweep is CLOSED** after two glyphs: the Return
arrowhead (a filled triangle, because two thick strokes cannot form a point) and
the text cursor I-beam (notched serifs plus a baseline crossbar, both taken from
real cursors rather than invented). Redrawing the battery and the charging bolt
was measured, costed and **declined** — the faults are real (the bolt is eight
literal scanlines with two flare rows at double the stem weight and the halves
offset by 1 px) but the owner ruled the sweep closed. Full note in
`docs/ui-conventions.md`. `CoverIcon` also stays: verified dead three ways —
no includer, no reference in either repo, and absent from `firmware.elf` — and
therefore costing nothing.

**The page palette was a fourteen-row dial when this was written (2026-08-15/16)
— stale, confirmed against `crosspoint-simulator/CLAUDE.md` 2026-08-30.** It has
since grown to a **42-phosphor + 10-paper, 52-preset** registry, and then most
of it froze: the shipped page (India stock / Sanguine ink light, the owner's
four-gun dark mix) no longer reads from the preset store at all as of
2026-08-24, and the Settings.app row that once exposed it is gone. Nothing here
reaches device firmware either way — it is a simulator/iOS presentation choice,
and the device has no Settings.app to expose it. The presets, their measured
contrast and the phosphor research behind the CRT rows live in
`crosspoint-simulator/ios/README.md` and
`crosspoint-simulator/docs/crt-phosphor-presets.md`; the freeze itself is
`crosspoint-simulator/CLAUDE.md`'s "The color dials, and the one rule they all
share" table — read that file for current behavior, not this paragraph.

The trap that keeps recurring there, recorded here because it is a *fork*
discipline issue and not a palette one: **a preset persists as an integer**, so
rows append and never insert, and the test's "unknown preset" sentinel has to
stay ahead of the enum. It was walked four times (7 → 11 → 13 → 14) as of this
paragraph's writing and has been walked **ten** times as of 2026-08-30 (up to
56) — each time it caught the collision instead of shipping one.
