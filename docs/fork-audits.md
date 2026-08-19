# Auditing the sibling forks against this fork

The companion to [matcha-heap-audit.md](matcha-heap-audit.md), which covered the
first fork named in [T-014](../TODO.md). This one covers the other five, and it
exists for the same reason: the negative results are the half a chat summary
drops, and without them the same commits get re-proposed every month.

Audited 2026-08-19 against `main` at `d0d6f758a`.

**The lesson the matcha audit taught, confirmed again here:** forks diverge in
what they CONTAIN, not merely in how far behind they are. A patch that looks
directly relevant is usually written against a subsystem this fork deleted, or
against one the other fork added. Check that the code exists here before reading
a commit as a candidate.

| Fork | Ahead / behind us | Named concern | Verdict |
|---|---|---|---|
| `zrn-ns/crosspoint-jp` | 935 / 1076 | CSS-parse heap exhaustion (`f59d0fa0f`) | **PORTED** |
| `zrn-ns/crosspoint-jp` | — | `abort()` on long CJK paragraphs (`d114dff4d`) | N/A — vertical writing |
| `0x1abin/crossmux` | 538 / 439 | EPUB layout OOM recovery (`8c85361ab`) | N/A as written; class already swept |
| `0x1abin/crossmux` | — | allocation failures recoverable (`6c3279a0d`) | mostly N/A; live parts already done |
| `franssjz/cpr-vcodex` | 863 / 453 | progressive EPUB indexing (`dd3ad0aa7`) | already present, by another route |
| `folio-etc/folio` | 676 / 215 | 1-bit `.cpfont` packing | ruled out by [T-013] |
| `uxjulia/CrossInk` | 692 / 616 | table rendering + colSpan | superseded by [T-012] / [T-021] |

## Ported: the CSS-parse heap floor

**The one real gap the audit found.** `CssParser` here caps the rule COUNT
(`MAX_RULES = 1500`) and refuses to *start* parsing below 64 KB free — but
nothing checked the heap between those two points. Under `-fno-exceptions` the
map insert aborts rather than failing, so a book whose heap tightens mid-parse
reboots the device. A count cap and a heap floor answer different questions, and
a book can trip the second nowhere near the first.

Both registration paths now carry a 32 KB floor (crosspoint-jp's measured
value): the live parse, and the cache load — where the guard returns `true`, not
`false`, because the rules that did load are usable and a `false` would delete
the cache. Partial styling beats a reboot; this is the same ruling as [B-030]
and [B-031], applied to a third path.

**Not observable in the simulator, and deliberately not claimed as tested:** the
host heap model is a constant, so it cannot fall past a threshold mid-parse. At
20 KB the run skips CSS at the existing entry guard, and at 70 KB it never
approaches the new one. Its value is on a device, where the heap actually moves.

## N/A: vertical writing (crosspoint-jp `d114dff4d`)

The `abort()` this fixes is in the vertical page-turn path. `lib/Epub/Epub/` here
has no vertical writing at all (zero files match), which the T-014 entry already
suspected — confirmed rather than assumed.

## N/A as written: crossmux's layout-OOM recovery (`8c85361ab`)

It allocates a `LineBreakState[]` workspace through `makeUniqueNoThrow` and
propagates a `false` up through `ParsedText`. Neither `LineBreakState` nor
`workspace` exists here — crossmux's line breaker diverged from ours — so the
patch does not apply.

**Its premise does apply, and is filed as [B-032] rather than smuggled in here.**
Our own `ParsedText` sizes several `std::vector::reserve()` calls from token
counts, and a failed `reserve` aborts exactly like a bare `new`. The B-030/B-031
sweeps covered explicit `new`; container growth is the same class and is not yet
covered.

`6c3279a0d` is mostly N/A for the usual reason — it patches OPDS and Calibre,
both deleted here — and the parts that do exist (`Xtc`, `HalStorage`, the web
server) were already hardened by B-030/B-031.

## Already present: progressive indexing (cpr-vcodex `dd3ad0aa7`)

A 908-line rewrite of `Section.cpp` to build a chapter's index incrementally.
This fork already does that by another route: `Section` here suspends and
resumes a build, persists a partial cache with a watermark, and finishes in the
background — 13 references to that machinery in `Section.cpp`, and a headless run
logs `Suspended build: 9 pages persisted`. Taking vcodex's version would be
replacing a working mechanism with a different working mechanism.

## Ruled out already

* **folio's 1-bit `.cpfont` packing** — [T-013] measured it at ~120 KB against a
  6.4 MB budget and ruled: keep 2-bit, keep the antialiasing.
* **CrossInk's table work** — [T-012] shipped columns with a header rule and
  [T-021] the rotated page, both against rendered proofs. CrossInk's `colspan`
  handling remains the reference if colspan is ever wanted; this fork's planner
  refuses ragged tables instead, which is the safe behaviour, not the same one.
