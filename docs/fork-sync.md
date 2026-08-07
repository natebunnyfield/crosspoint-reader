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
2. `REVIEW` means the commit straddles live and removed code. Read it before
   doing anything: `git show <sha> --stat`.
3. Take only the live hunks. `git cherry-pick -n <sha>` then unstage the
   removed paths, or apply the specific files with `git checkout <sha> -- <path>`.
4. Build and, where the change touches layout or caching, bump the cache
   format version — see `docs/file-formats.md`.

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

## One working copy per project

Multiple clones is what actually costs time here. The live pair is:

- `~/src/crosspoint-reader` — firmware. `origin` **and** `fork` are
  `natebunnyfield/crosspoint-reader`; `upstream` is the project.
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

## Submodule

`freeink-sdk` is pinned at a commit ahead of its own `main`
(`heads/main-25-ge514a86`). A fresh clone that skips it builds against the
wrong SDK with no obvious error:

```bash
git submodule update --init --recursive
```

`repo-status.sh` flags the submodule when it is uninitialised (`-`) or has
drifted off the pinned commit (`+`).
