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

### CANDIDATE — progressive JPEG decode

"Decode progressive JPEGs whose DC scan is non-interleaved" — a specific
decoder bug with a specific failure mode. We decode through
`lib/Epub/Epub/converters/JpegToFramebufferConverter.cpp`. CrossInk
independently has "improve progressive jpeg cover support", so two forks hit
this. (matcha + CrossInk)

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
