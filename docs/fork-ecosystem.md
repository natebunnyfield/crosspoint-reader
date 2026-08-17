# The fork ecosystem, and what is worth taking from it

Surveyed **2026-08-15**, from `main` at `ade9dac91`, against
`crosspoint-reader/crosspoint-reader@develop`.

CrossPoint has **1,443 reader forks** and **27 simulator forks**. Most are
parked copies. A handful are real projects with hundreds of commits of their
own, and some of that work is directly useful here. This file records the
survey so it is not re-run from scratch every time someone wonders whether
another fork already solved something.

Companion to [fork-sync.md](fork-sync.md), which covers taking changes from
*upstream*. This file covers taking ideas from *siblings*.

## How the list was narrowed

1,443 is past reading. The ranking used two cheap signals:

- **Stars**, and
- **whether the fork was renamed.** A rename (`CrossInk`, `matcha-reader`,
  `folio`) is the strongest cheap indicator that someone is building rather
  than parking a copy.

Then `ahead_by`/`behind_by` via the GitHub compare API for the leaders, and
their commit subjects read in bulk (~1,700 across eight forks).

```bash
# the fork list (API stops paging at ~1,100 of the 1,443)
for p in $(seq 1 15); do
  gh api "repos/crosspoint-reader/crosspoint-reader/forks?per_page=100&page=$p" \
    --jq '.[] | [.full_name,(.stargazers_count|tostring),.name] | @tsv'
done | sort -t$'\t' -k2 -rn

# divergence + subjects for one fork
gh api "repos/crosspoint-reader/crosspoint-reader/compare/develop...OWNER:REPO:BRANCH" \
  --jq '(.ahead_by|tostring)+" ahead / "+(.behind_by|tostring)+" behind"'
gh api "repos/crosspoint-reader/crosspoint-reader/compare/develop...OWNER:REPO:BRANCH" \
  --jq '.commits[].commit.message | split("\n")[0]'    # capped at 250
```

Everything below is filtered through this fork's rules: a dedicated reader,
portrait only, no touch, no X4 Pro, 380 KB of RAM.

## The serious forks

| Fork | Stars | Ahead / behind | What it is |
|---|---|---|---|
| `uxjulia/CrossInk` | 1228 | 615 / 199 | The ecosystem's centre of gravity, with 149 forks of its own. Upstream carries a `crossink-controls-port` branch and this fork has backported from it before (`cb995809b`). Tables, file-browser actions, themes, tilt. |
| `eszter007/matcha-reader` | 20 | **659 / 31** | The one to watch. Japanese reading + learning tools, but the interesting half is a long disciplined run of heap/OOM/decoder fixes. Nearly current with upstream. |
| `franssjz/cpr-vcodex` | 393 | 453 / 370 | "Improving the reading experience and consistency." Progressive EPUB indexing, hyphenation, StarDict, highlights, stats, RTC. |
| `0x1abin/crossmux` | 111 | **402 / 28** | Apps hub, mini-games, standby faces, Simplified Chinese. Mostly out of scope; carries real low-heap crash fixes and CJK pagination perf. |
| `zrn-ns/crosspoint-jp` | 41 | 547 / 442 | Japanese-specialised: vertical writing, ruby, Aozora Bunko. Has a genuine `abort()` fix for very long CJK paragraphs. |
| `folio-etc/folio` | 30 | 215 / 183 | Quietly the most aligned with this fork's instincts: font budgets, 1-bit `.cpfont` packing, power-button claiming, button-hint rendering. |
| `yattsu/biscuit` | 442 | 52 / 517 | High stars, low divergence, last pushed April. Dormant. |
| `ztrawhcs/crosspoint-enhanced-reading-mod` | 24 | 199 / 692 | Button chords, long-press cycling, help boxes, page highlights. Diverged early, never resynced. |

## Worth taking

Ordered by value against our constraints. **Only the first was verified against
our source.** The rest are read from commit subjects — good evidence a problem
exists, weak evidence about the quality of the fix. Read each diff before
acting; fork-sync rules apply (per commit, live hunks only).

### VERIFIED GAP — table borders and `colSpan`

`ChapterHtmlSlimParser.cpp:481-519` tracks `tableDepth` and handles
`thead`/`tr`/`td`/`th`, but there is **no `colspan` handling and nothing draws a
border** — grepped for both, no hits. We already invested here once, labelling
flattened cells by header (PR #10), so this is the adjacent next step.

Source: `uxjulia/CrossInk` — "improve table rendering (#89)", "draw borders for
simple tables", "handle colSpan in tables when they act as header or footer
rows (#90)".

### CANDIDATE — matcha's heap and OOM series

The richest vein, and written against effectively the same rules as our
`CLAUDE.md`: avoid EPUB indexing heap pressure; avoid reader-settings OOM; free
the font cache before a settings save; drop the per-call allocation from
`HalStorage::hasContent`; bound history by memory rather than by a corruption
guard; generate cover thumbnails without holding HAL power locks. At 31 behind
upstream these should apply with little friction.

### DONE 2026-08-17 — progressive JPEG decode

Real, reproduced on real material, and fixed — but only half of this entry was
ever about the same thing. Full account: [progressive-jpeg.md](progressive-jpeg.md).

* **matcha `669d2ac01`** — the same bug and the same reading of the spec. Taken
  in spirit, not verbatim: matcha's patch fixes 4:4:4 and REFUSES the
  subsampled case, which is MozJPEG's default and the only variant that turned
  up here in ~1,900 real images. Ours decodes it, by re-deriving the traversal
  from the luma component's own block grid
  (`scripts/jpegdec_patches/0003-...`). Its `JpegDecodeError.h` idea was taken;
  the text is ours. Worth offering back.
* **CrossInk `886a2ae68` / `f39a11fee`** — **already present, nothing to take.**
  SOF2 detection, forced `JPEG_SCALE_EIGHTH`, 1/8 geometry through the scaler:
  all three exist here, via `getJPEGType()` rather than re-parsing the file
  (`JpegToFramebufferConverter.cpp:426,455`, `JpegToBmpConverter.cpp:541-549`,
  which also smooths the upscaled preview — CrossInk does not). Do not
  re-propose.

Adjacent and NOT taken: CrossInk `c3d04a9dd` raises the max JPEG width 2048 →
4096. Ours is a RAM guard in `validateImageDimensions` on a 380 KB device; that
is a separate decision with a cost, not part of a decoder fix.

### CANDIDATE — 1-bit packing for bundled `.cpfont`s

The same dial the 2-bit chrome font work turned the other way: we traded flash
for antialiasable UI, folio packed bundled theme fonts down to 1 bit. Worth
reading as the counterweight regardless of which way we keep it. (folio)

### CANDIDATE — power-button claiming and button-hint rendering

folio lets an activity claim the short power press, and extends button hints so
a *power* button hint can be drawn. Directly relevant to **T-009** in
[../TODO.md](../TODO.md) — prior art for which affordances get drawn and who
owns them. (folio)

### CANDIDATE — font budgets during heavy activities

"Constrain font budgets during heavy activities", plus SD-theme memory logging.
We load ~80 global font objects and have already been bitten by the font cache
colliding with other work (the `clearSdCardFonts` / hi-res companion bug). A
budget mechanism is the structural version of that fix. (folio)

### CANDIDATE — low-heap crash fixes

"Avoid EPUB footnote allocation crash", "avoid web settings heap exhaustion",
"avoid reading stats crash on low heap". The footnote one matters here because
we declined upstream's `FOOTNOTE_HREF_LEN` 96→256 bump — this is a different
attack on the same area, without the +160 bytes per entry. (crossmux)

### CANDIDATE — long-CJK-paragraph `abort()`

An `abort()` triggered by page-turning inside EPUBs with extremely long CJK
paragraphs. A hard crash on real content, worth understanding even though the
vertical-writing context around it is not wanted. (crosspoint-jp)

### CANDIDATE — progressive EPUB indexing

Indexes large EPUBs progressively rather than in one pass. We already window
section builds and had a real bug there (the `O_WRONLY` read-back-during-build
fault), so the problem shape is familiar. (cpr-vcodex)

## Checked — we already have it

Recorded so these are not re-proposed. This is the half a chat summary drops.

| Candidate | Reality |
|---|---|
| "Add missing HTML 4.01 named entities" (cpr-vcodex) | `lib/Epub/Epub/htmlEntities.cpp` has **all 252**. Counting source *lines* gives 51 and looks like a shortfall — count entries. Spot-checked 29 obscure ones (`&permil;` `&oline;` `&lceil;` `&notin;` …), all present. |
| "Display `<hr>` instead of ignoring it" (CrossInk) | Handled at `ChapterHtmlSlimParser.cpp:601` (inside tables) and `:992` (block, with its own margin/padding). |
| "Toggle hidden files in the file browser" (CrossInk) | `showsHiddenEntries()` — `FileBrowserActivity.h:31`, used at `.cpp:41`. Whether it is user-facing is worth a look; the mechanism exists. |
| "Shift modifier for long-press injection" (jimrhoskins, simulator) | Covered by `queueButtonTap(buttonIndex, holdMs)` and the `QTAP` script action. |

## Ruled out by this fork's own rules

Most of what the ecosystem builds by volume is what `SCOPE.md` exists to
refuse. Listed so the refusal is on record rather than rediscovered.

- **Apps hubs, mini-games, virtual pets.** crossmux's Apps hub;
  `trilwu/crosspet` (214 stars, virtual pet); `ma-r-s/crossplay`;
  `rtens/crosspoint-reader-chess`. "A dedicated e-reader, not a Swiss Army
  knife."
- **Tilt page turn and touch surfaces.** Excluded by the standing no-touch
  ruling and by X4 Pro being out of scope.
- **Themes, carousels, stats dashboards, KOReader profiles.** CrossInk's
  minimal theme and Lyra carousel, vcodex's dashboards, flashcards and
  multi-profile KOReader sync all land in subsystems this fork deliberately
  deleted. Taking them would undo decisions, not add features.

## The simulator side is empty

All 27 simulator forks were compared. **Twenty-two are strictly behind with
nothing of their own.** The four with any commits carry three or fewer, and
every one is a rebrand or the same recurring maintenance — "add missing HAL
stubs that broke the simulator build", "add missing vSemaphoreDelete FreeRTOS
shim", "update simulator shims for current firmware".

Our simulator fork is **299 ahead of upstream** and is by a wide margin the most
developed in the ecosystem. There is nothing to salvage here; the traffic goes
outward.

## Limits of this survey

- It is a survey of **commit subjects**, not of code. One item was verified
  against our source.
- GitHub's fork listing stops paging at roughly 1,100 of the reported 1,443,
  and sorts by neither stars nor divergence. **A fork with real work, no stars
  and no rename would not have surfaced.**
- The compare API caps `.commits[]` at 250, so forks further ahead than that
  had only their most recent 250 subjects read.

## Salvage log — what was actually taken

Updated as PRs land. Records the skips too, with the reason, so a later pass
does not re-evaluate the same commit from scratch.

### matcha-reader — heap/OOM series (T-014, first pass 2026-08-15)

Probed all ten heap-related commits by cherry-pick. **One applied cleanly**;
the rest conflict, which is expected at 659 commits of divergence and is not a
verdict on their quality.

| Commit | Outcome |
|---|---|
| `9011c91fd` emergency font release also frees kern/lig tables | **TAKEN.** Applied clean. |
| `07b35f966` free font cache before settings save | **SKIPPED — needs infrastructure we lack.** Calls `FontCacheManager::releaseAllFontMemory()`, which does not exist here; our manager exposes only `clearCache()`. Porting it means porting matcha's emergency-release machinery first. Its guard (`finishOnBack`) also has no analogue — this fork's `SettingsActivity` has nine `saveToFile()` sites and no reader-entered mode flag. Revisit if that machinery is ever ported. |
| `f6df3e35c` drop per-call allocation from `HalStorage::hasContent` | **N/A — checked 2026-08-17, `hasContent` does not exist here.** `grep -rn hasContent lib/ src/` returns one hit and it is `http.hasContentLength()` in `HttpDownloader.cpp:75`. The commit optimizes a method matcha ADDED (a guard against 0-byte husks left by an interrupted conversion being read back as finished artifacts). So this is not a hand-portable perf fix; taking it means importing the API and converting its call sites — a feature decision, not a cleanup. The first pass read "generic HAL" off the diff without checking that the method was ours. |
| `d8b220990` avoid EPUB indexing heap pressure | Conflicts (3). Worth a hand-port. |
| `2ea69f77a` avoid reader settings OOM | Conflicts (2). Same area as `07b35f966`. |
| `37265177f` report a power lock that is never released | Conflicts (2). Diagnostic; generic. |
| `41d36878a` use-after-free crashed XTC on the second page turn | Conflicts (1). XTC exists here — worth reading. |
| `5bca482f1` throttle mid-build read-back retries to real heap changes | Conflicts (1). Touches the same read-during-build path as our `O_RDWR` fix. |
| `f07d737d0` bound the history by memory | Conflicts (6). "History" is matcha's; likely N/A. |
| `16d4dd225` cover thumbnails without HAL power locks | Conflicts (3). Their `LibraryActivity`; likely N/A. |

Not probed, and deliberately: everything vertical-writing, manga, Babylon
dictionary or word-lookup specific. Those are features this fork does not have.

**On the one taken.** Two halves, and only the first is unambiguously ours:

- `freeStyleKernLigatureData()` now clears the `stubData` / `miniData` ligature
  mirrors alongside the array. Both alias `ligaturePairs` directly, so freeing
  the array without clearing them leaves dangling pointers. That is a
  correctness fix here regardless of call site.
- `clearPersistentCache()` now frees the kern/lig class tables too. Matcha's
  measured rationale is about an *emergency* mid-read release, which this fork
  does not have — our `clearPersistentCache()` is reached from `freeAll()`
  (`SdCardFont.cpp:200`), i.e. font unload. The anti-fragmentation argument
  still holds at that point, but the dramatic 61428 -> 102388 `maxAlloc` figure
  in their commit message is theirs, measured on their build. Do not repeat it
  as ours.
