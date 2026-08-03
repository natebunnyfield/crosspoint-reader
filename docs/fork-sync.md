# Keeping this fork in sync

`natebunnyfield/crosspoint-reader` is a **selectively divergent** fork, not a
tracking mirror. As of 2026-08-03 `develop` is 91 commits ahead of
`upstream/develop` and 2 behind. The gap is permanent and intentional, so the
usual advice — "just merge upstream regularly" — is actively wrong here.

Run `scripts/repo-status.sh` to see where everything stands. It only reports;
it never merges.

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
