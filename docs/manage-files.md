# Manage Files

A home-screen file manager: see every file on the SD card (including dotfiles,
each name shown as one whole string), rename, normalize book names, move, and
delete — with reading progress surviving rename/move. Designed 2026-08-04 in a
plan-first session; every decision below is an explicit user ruling, not a
default.

Mockups considered during design (options A/B/C with button maps):
https://claude.ai/code/artifact/aff4819b-af2d-4d09-8d62-bad253f433b8

## Decisions (user rulings, 2026-08-04)

| Question | Ruling |
|---|---|
| Interface model | **A — single pane + per-item context menu** (`OptionPopup`), one-slot clipboard for move. Two-pane commander rejected (half-height lists on e-ink, Left/Right stolen for pane switching). Mark-mode multi-select deferred to v2. |
| v1 operations | **Rename, Move, Delete** (original ruling), plus **Normalize name** (added later same day, see below). Copy, New folder, Details popup are v2. |
| Book progress on rename/move | **Migrate** the `.crosspoint/<prefix>_<hash>/` cache dir so reading progress survives. Amended same day: "yes to migrating, unless it is risky — then just delete cache." Risk assessed low (cache contents store no SD path; single FAT dir rename; same mechanism as the shipped read-folder move), so migrate — and on any rename failure the old cache dir is **deleted**, never left orphaned. |
| Protected paths | **No special-casing.** `.crosspoint/`, `.fonts/`, the open book — all get the same single confirmation as anything else. |
| Hidden files | **Always visible, no special styling.** (`SETTINGS.showHiddenFiles` still governs Browse Files only; it remains without a UI toggle.) |
| Code shape | **New `FileManagerActivity`** in `src/activities/home/`. `FileBrowserActivity` untouched except for extracting its recursive delete into a shared helper. |
| Home menu position | After File Transfer, before Settings. |
| List display (added later 2026-08-04) | Filename is **one string** — no split name/extension columns ("do not do the bullshit split of filename and extension"). Browse Files keeps its split; this ruling covers the manager. |
| Normalize name (added later 2026-08-04) | Action-menu option on epub/xtc files: propose a rename to the card's naming format **`Title - Author.ext`** (the format decision embodied by every book on the real card, e.g. "A Little Princess - Frances Hodgson Burnett.epub"). Title/author come from embedded metadata; empty author degrades to `Title.ext`; FAT-reserved characters sanitized to spaces, UTF-8 preserved. The proposal opens in the rename keyboard pre-seeded, so one Confirm applies it and it remains editable — commit is the ordinary rename path (cache migration included). |

## Interaction spec

List chrome matches every other CrossPoint list screen: header, `GUI.drawList`
rows with icons and the filename as one whole string (no extension column —
ruling above), path bar at the bottom, four button hints.

| Button | List state | Menu open |
|---|---|---|
| Confirm | Directory: descend. File: open action menu. Long-press on directory: action menu. | Run selected option |
| Back | Up one directory; at root: Home. Long-press: jump to root. | Close menu |
| Left/Right + side Up/Down | Move selection (`ButtonNavigator`; hold = page) | Move selection |

Action menu options (title = the item's name):

- **View** (files only, all types; first option — added 2026-08-04) →
  `TextViewerActivity` (`src/activities/util/`): paged plain-text viewer, one
  4 KB sliding window (never loads the whole file), word-wrap with UTF-8
  sequences kept atomic, `\t`→2 spaces, control bytes stripped, no EOF
  wrap-around, page-start offset stack for back-paging.
- **Rename** → `KeyboardEntryActivity` seeded with the current full name.
  Rejects empty, `/`-containing, and already-existing names.
- **Normalize name** (epub/xtc files only) → loads embedded metadata and opens
  the same keyboard seeded with `Title - Author.ext` (or `Title.ext` when the
  author field is empty). "No metadata found" popup when the title is empty.
- **Move** → arms the one-slot clipboard (full source path). A status line above
  the path bar shows the armed item; the menu everywhere then leads with
  **Move here**, which `Storage.rename()`s the source into the current
  directory. Same-directory move is a no-op; moving a directory into its own
  subtree is rejected before starting.
- **Delete** → existing `ConfirmationActivity`, then recursive delete (the
  iterative-stack implementation extracted from `FileBrowserActivity`, which
  clears book caches as it goes).

Empty directory + armed clipboard: Confirm still opens the menu (Move here is
the only entry), so a move into an empty folder works.

## Cache/progress migration

`.crosspoint` cache dirs are keyed by `std::hash<std::string>` of the book's
full path (`epub_`/`xtc_`/`txt_` prefix — see `lib/Epub/Epub.h`, `lib/Xtc/Xtc.h`,
`lib/Txt/Txt.cpp`). Rename/move therefore re-keys the cache dir, following the
existing pattern in `moveFinishedBookToReadFolder`
(`src/activities/reader/EpubReaderActivity.cpp`): rename the book file, rename
the cache dir old-hash → new-hash, `RECENT_BOOKS.updatePath(...)`, and repoint
`APP_STATE.openEpubPath` when it referenced the old path. The manager
generalizes this to all three book types via `FsOps::migrateBookRefs`
(recursively via `migrateBookRefsRecursive` when a directory moves — every book
inside is migrated).

**Failure policy (amended ruling): never leave an orphan.** If the cache-dir
rename fails for any reason, the old cache dir is deleted on the spot
(`FsOps.cpp` — "migrate unless risky; then just delete cache"). The move/rename
of the book itself still completes; the book simply re-parses at its new path
and progress restarts. Sim-verified by pre-blocking the destination hash dir.

## Traps (read before touching this code)

- **`Storage.rename()` is a FAT rename** — moves across directories on the same
  volume for free. Never implement move as copy+delete.
- **Recursion is banned** for tree walks: ESP32-C3 task stacks are 2–4 KB. The
  recursive delete uses an explicit stack of `(path, postOrder)` pairs; folder
  copy (v2) must do the same.
- **Self-nesting move** (folder into its own subtree) must be rejected by path
  prefix check before calling rename; SdFat behavior for it is not trusted.
- **Held-release leak**: any activity entered while Confirm/Back is held must
  swallow that button's next release (`lockNextConfirmRelease` pattern from
  `FileBrowserActivity`) or the first list item self-activates.
- **i18n**: labels are `STR_*` keys in `lib/I18n/translations/english.yaml`;
  generated headers are gitignored and rebuilt by `scripts/gen_i18n.py`.
- **Deleting a book does not remove it from Recents** (pre-existing behavior);
  rename/move via the manager does repoint Recents.

## Verification (2026-08-04, simulator)

Implemented as `src/activities/home/FileManagerActivity.{h,cpp}` with shared
helpers in `src/util/FsOps.{h,cpp}` (`FileBrowserActivity`'s recursive delete
now calls the same helper). Verified headlessly with
`CROSSPOINT_SIM_INPUT_SCRIPT` / `CROSSPOINT_SIM_SCREENSHOTS` against the real
firmware; both `-e simulator` and the `-e default` C3 target build clean.

Confirmed on-screen **and on-disk**: dotfiles listed; action menu contents
(with and without an armed move, and `Move here` reachable in an empty dir);
move across directories; **cache migration** (a fake
`.crosspoint/txt_<hash>` dir was re-keyed to the destination path's hash with
its `progress.bin` intact); **migration failure path** (destination hash dir
pre-blocked → rename failed → old cache deleted per the amended ruling, move
still landed); delete after confirmation; rename keyboard opens and cancels
cleanly. Normalize verified: menu offers it only on book files, and on the test
epub it seeded the keyboard with the metadata-derived name (log:
`normalize 'efttest.epub' -> 'English Fairy Tales.epub'` — that epub's embedded
author field is genuinely empty, so the title-only fallback fired; commit path
is the shared rename path). Migration risk basis: `book.bin` stores only EPUB-internal metadata
(title/author/spine hrefs — `lib/Epub/Epub/BookMetadataCache.h`) and
`progress.bin` stores spine/page/anchor; neither embeds the SD path, so a
renamed cache dir is valid wherever the book lands. NOT yet exercised end-to-end: a rename that actually
commits a new name (scripting the on-screen keyboard is impractical headlessly;
the commit path is the same `Storage.rename` + migrate call the verified move
uses) — worth one manual check on device.

### Traps found while building/verifying

- **Popup close leaks a button release.** `OptionPopup` acts on button *press*;
  every list screen in this codebase acts on *release*. A popup living inside
  an activity (not wrapped in its own activity) closes on the press and the
  release then fires the list action underneath — the manager swallows it via
  `lockNextConfirmRelease` / `lockNextBackRelease` set right after
  `popup.handleInput()` reports the popup closed. Press-driven activities
  (SettingsActivity) never see this, which is why the pattern isn't visible in
  the existing popup call sites.
- **Headless QA scripts: `.crosspoint/` materializes and shifts every row.**
  The first sim run creates `.crosspoint/` on the test card; in a
  show-everything list it then occupies row 0 at the root, so a DOWN-count
  script written against the first run's listing silently acts on the wrong
  entries the next run. Recount rows against the *current* card contents (or
  grep the activity's own log) before trusting a script.
- **Host `std::hash` matches the sim binary,** so a fake cache dir for
  migration tests can be pre-computed with a 5-line host program using
  `std::hash<std::string>` — same libc++ as the sim build. (Device hashes
  differ; only relative consistency matters.)
- One early sim run listed no dotfiles with identical code and card
  (`qa/01-manager-root.bmp`); never reproduced across five later runs.
  If hidden files ever appear missing, re-run before digging.
- **Text-wrap measurement**: use `getTextAdvanceX`, not `getTextWidth`, for
  line-breaking — `getTextWidth` measures ink extents (a lone space is 0 px)
  and under-counts by ~15%, overflowing lines off-screen.
- **`pgrep -f "pio run"` self-matches its own shell** in a wait loop — the zsh
  `-c` command line contains the pattern, so the loop never exits. The bracket
  variant `pgrep -f "[p]latformio"` fixes self-matching but STILL deadlocks
  while an interactive `pio run -t run_simulator` session is open (it keeps a
  platformio scons alive the whole time). For parallel work, build into an
  isolated dir instead: `PLATFORMIO_BUILD_DIR=<scratch> pio run -e simulator`
  (the shared `.cache` keeps rebuilds fast).
- **2x screenshots**: `CROSSPOINT_SIM_SCREENSHOTS` downsamples to logical size
  unless `CROSSPOINT_SIM_WINDOW_SCALE=2` is set — a hi-res rendering bug is
  invisible in the default capture.
- **Headless boot + home-menu row shifts**: stale resume state boots the sim
  straight into the reader (`HOME` does NOT rescue from there — force Home with
  `readerActivityLoadCount: 1` in `fs_/.crosspoint/state.json`), and once any
  book has been opened, Recents rows sit above the home menu and shift every
  DOWN-count. Both documented in the simulator repo's CLAUDE.md ("Navigating to
  a screen from a headless script").

## v2 plan (rulings 2026-08-05)

Planned in a second plan-first session. Every decision below is an explicit
user ruling.

| Question | Ruling |
|---|---|
| File short-press | **View directly** (was: action menu). Hold 1s = menu, files and folders alike. View stays in the menu too. Bottom nav updates: file → `View/Menu`, folder → `Open/Menu`. |
| Back at root | **Return to home with the Manage Files row selected**, parity with the other home items. Bug: `ActivityManager::goHome()`'s name-match block lacks `"FileManager"`, so it falls to `NONE` = fresh home, first book selected. |
| Normalize name | Renamed **"Rename using metadata"**, and the item is only added when the metadata probe succeeds (light epub/xtc load at menu-open, book files only). No more showing it then erroring. |
| File info | **Inline under the menu title** — size, created, modified (FAT's three timestamps; there is no separate "updated"). Folders: entry count. Needs mutex-wrapped date getters on `HalFile`. |
| Duplicate | New action **above Move**, **files only**. Streamed chunk copy, collision names ` copy`, ` copy 2`, …. **TODO: folder duplicate** (recursive streamed copy + progress popup + interrupted-state story) — deferred by ruling, tracked here. |
| Viewer scrollbar | Persistent right-side scrollbar in the viewer, both modes: thumb position = byte offset / file size, thumb height = page/file ratio. Doubles as a size indicator. |
| Viewer pretty mode | **Select toggles raw ↔ pretty** per type: json (streaming token re-indenter, no DOM), html (tag-strip state machine), bmp (existing BmpViewer path), png (PNGdec; sim needs the native-decoder opt-in), epub (metadata card: title/author/language/spine count/cover presence). Any parse trouble → fall back to raw, never crash. |
| Binary formats | **Structured summaries only** for the known `.crosspoint` formats (book.bin version + chapters, section.bin version + pages, progress.bin decoded fields per docs/file-formats.md). No universal hex-dump view (ruled out). Unknown binaries keep the raw-text view. |
| File icons | png (and jpg) get the Image icon — `UITheme::getFileIcon` currently maps only bmp. Audit the rest of the extension → icon table while in there. |
| Editor + BT keyboard | **Deferred to its own plan-first session.** Vim-flavored + intuitive input modes both wanted; BLE HID host RAM on the C3 must be measured before any commitment. |

Build order: Phase 1 = interaction rework (short-press View, nav labels,
go-home selection fix, icon fix, metadata-gated rename); Phase 2 = inline info
+ Duplicate; Phase 3 = viewer scrollbar + pretty modes.

## Summarize tag (ruling 2026-08-05, second session)

A per-book handshake with the host-side summarization pipeline
(`scripts/summarize_books.py`): the action menu on an **EPUB** (cheap
extension gate, no metadata probe) shows **Summarize** / **Don't summarize**,
toggling a marker file `/.crosspoint/epub_<hash>/summarize.tag` whose content
is the book's full card path plus `\n`. The book itself never moves. The host
pipeline scans markers (over WebDAV — dot-paths are deliberately unprotected —
or a local card tree), generates `<Title> — Summary.epub` into `/books/`
(3/10/30-minute chapters), and deletes the marker on success.

Marker lifecycle:

| Event | Marker outcome |
|---|---|
| Toggle on | Created (cache dir mkdir'd first if the book was never opened) |
| Toggle off / host success | Deleted |
| Rename/Move (file or folder) | Survives — cache dir is re-keyed by `migrateBookRefs`, which now also rewrites the marker's content to the new path |
| Duplicate | Not copied (Duplicate never touches `/.crosspoint`) — correct |
| Delete | Dies with the cache dir — correct |
| Settings → Clear Cache | **Lost by design** — the cache-clear loop stays dumb; re-toggle Summarize after clearing |

With Summarize the action menu's worst case is 8 entries (armed move + all
file actions on an epub); `openActionMenu`'s `options[]` was bumped to 9 so
the unchecked `options[count++]` keeps headroom.

Host side (`scripts/summarize_books.py`, stdlib only + the `claude` CLI):

```bash
python3 scripts/summarize_books.py --source fs_ --list   # what's tagged
python3 scripts/summarize_books.py --source fs_          # local card tree
python3 scripts/summarize_books.py --webdav http://crosspoint.local
```

It extracts the book's text, has `claude -p` research it (WebSearch) and write
three self-contained summaries plus a colophon, validates word budgets
(±15% of 690 / 2300 / 6900 words at 230 wpm) and XML well-formedness, retries
once, then packages `<Title> - Summary.epub`. Two traps worth keeping:

- **The child CLI's environment is a whitelist, and `USER` must not be in it.**
  Run from inside a Claude Code session with the ambient environment and the
  child fails with "OAuth session expired and could not be refreshed". Bisected
  2026-08-05 against every variable: forwarding **`USER`** alone reproduces it
  100%, dropping it fixes it 100% (`LOGNAME` carries the same value and is
  harmless). Do not "restore" `USER` to the whitelist in `child_env()`.
- **`--model` is explicit (default `opus`)** rather than inheriting the CLI's
  configured model, which can be one the account has no credits for; the
  failure then surfaces as a bare exit 1 with the real reason on *stdout*.

Two output rules exist because of how this device paginates, and the script
both prompts for them and repairs them before packaging: fragments must not
open with a heading (section.bin v35 breaks the page before h1-h3, and the
builder already emits the chapter title, so a leading heading costs a whole
page), and named HTML entities (`&ldquo;`, `&mdash;`) are invalid — an EPUB is
parsed as XML and the firmware builds with `XML_GE=0`, so they are rewritten
to literal characters.

Verification status: local `--source` mode verified end-to-end; `--webdav`
verified against a mock DAV server (PROPFIND/GET/PUT/DELETE, spaces in
filenames) but **not yet against real hardware**.

## TODO — deferred, explicitly wanted (ruling 2026-08-05)

- **Folder duplicate** — recursive streamed copy + progress popup +
  interrupted-state story (files-only Duplicate shipped first).
- **Hex-dump viewer mode** — offset/hex/ASCII columns for unknown binaries
  (v2 ships structured `.crosspoint` summaries only).
- **New folder** action.
- **Mark-mode multi-select** — batch move/delete.
- **Sort order** — name/date/size toggle for the list.
- **Free space** readout (path bar or list footer).
- **"Set as sleep screen"** on bmp files from the action menu —
  `BmpViewerActivity` already has the machinery.

## v2 candidates (explicitly deferred)

New folder, Details popup (size/path/has-cache), mark-mode multi-select batch
operations.

## Editor + BT keyboard — SHIPPED (2026-08-06)

No longer deferred. The BLE HID host was measured, the editor was built, and
both landed as **Create Note** and **Claude** on the home screen, plus an
**Edit** action here for `.md`/`.txt`.

- Feature and behaviour: [notes-and-claude.md](notes-and-claude.md)
- The measurements that justified it, and what is still unverified:
  [ble-editor-spike.md](ble-editor-spike.md)

Headline: a resident BLE host does NOT fit (~72 KB against the reader's
budget), so the on-screen keyboard is the default and BLE starts only when a
keyboard is already bonded.

