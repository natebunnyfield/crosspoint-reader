# Known bugs and open defects

Running list for this fork. Newest first within each section. A bug leaves
OPEN only when there is evidence it is fixed — a passing build is not evidence
for anything you cannot observe headlessly (see the device-feel rule in the
project guide).

Format: `**[id] Title** — severity · where · status`, then what breaks, how it
was found, and what closing it requires.

---

## TODO — deferred by ruling

Not defects. Work the owner has asked for and explicitly parked; kept here
because this is the running register and there is nowhere else it would be
found. Newest first.

### [T-001] Withdraw three more Settings rows
**scope: device Settings UI · ruled 2026-08-07 · not started**

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

---

## OPEN

### [B-017] Viewing a file could write emptiness back over it
**severity: high · scope: data loss · FIXED 2026-08-07 · `e9fd4cce`**

Reported as "some notes and bmp are being rewritten and emptied out sometimes
when viewed". Two unrelated causes, which is why it looked intermittent.

**BMP.** `BmpViewerActivity::doSetSleepCover()` opens the viewed file for read,
then opens `/sleep.bmp` for write — and `openFileForWrite` is `O_TRUNC`. When
the file being viewed **is** `/sleep.bmp`, the second open truncates the very
file the first is reading. The next `read()` returns 0, the copy loop never
runs, and `success` had already been set `true` *above* the loop — so it
reported Done over a zero-byte sleep screen.

Demonstrated at the syscall level with the same open sequence: 53,918 bytes → 0,
first read after the truncate returns 0. `fs_/sleep.bmp` on the sim card was
already sitting at 0 bytes when this was investigated.

**NOTES.** `NoteEditorActivity::onEnter()` sets `loadRefused` only when the file
exceeds the buffer. If `openFileForRead` **fails**, the load block is skipped
entirely, the buffer stays empty, `loadRefused` stays false, and `onExit()`'s
`save()` writes that emptiness back. B-013 fixed refused-to-load and left
failed-to-open exposed.

A file that does not exist is a different case — that is Create Note minting a
new note, which must still save — so the guard is "exists but will not open",
not "failed to open".

**Plausible trigger for the notes half:** enough leaked directory handles reach
`EMFILE` and opens start failing. That is S-006 in the simulator, fixed the same
day; the device HAL is separate code and has not been audited for the same leak.
Worth checking `lib/hal/HalStorage.cpp` before assuming this is fully closed.

**Verified:** 215/215 host tests, device `gh_release` and desktop canary build.
The BMP mechanism is proven; the notes half is a reasoned fix to a path that is
hard to trigger on demand, so it is **not** reproduced end to end.

### [B-006] X4 running firmware carries an empty version stamp
**severity: low · scope: device provisioning · found 2026-08-02**

The X4 runs a build stamped `1.5.0-BNY-rc+` — empty suffix. `gh_release_rc`
composes its version as `1.5.0-BNY-rc+${sysenv.CROSSPOINT_RC_HASH}`
(`platformio.ini:186`), and the flash was run without that variable set. The
code is identical to `crosspoint-880ba0f9.bin`; only the stamp is wrong. It
feeds the OTA version comparison, and it makes the running build
unidentifiable after the fact.

**Now staged:** both cards carry `20260807T0709Z-crosspoint-e194ab7b.bin`, a
`gh_release` build stamped `1.5.0-BNY` with no empty `+` suffix (confirmed by
`strings` on the binary), so SD Firmware Update from the card will replace the
badly-stamped firmware. Still OPEN because that is an on-device action nobody
has performed yet.

**Close by:** reflashing with the variable set, or SD Firmware Update from the
card (`SdFirmwareUpdateActivity` is a plain file picker with no version gate,
so a same-code reflash is accepted):
```bash
CROSSPOINT_RC_HASH=880ba0f9 pio run -e gh_release_rc -t upload --upload-port /dev/cu.usbmodem2401
```

### [B-003] Exploded `.epub` directories are probably unreadable on device
**severity: low · scope: content · found 2026-08-03**

The X3 card carried 10 entries named `*.epub` that are DIRECTORIES
(`META-INF/`, `mimetype`, `OEBPS/`) rather than zip containers. Miniz is the
firmware's only container library and `lib/Epub/Epub/Section.cpp` unzips at
runtime, so these almost certainly do not open — but this was inferred from
the container path, **not** confirmed by opening one on device or in the
simulator.

They are preserved at `~/crosspoint-books/_exploded/`. Also note
`ls *.epub | wc -l` on such a card reports a wildly inflated count because it
recurses into the directories (reported 510 for a real 76).

**Close by:** opening one in the simulator to confirm the failure mode, then
either re-zipping them as proper EPUBs (mimetype stored first, uncompressed)
or discarding them.

### [B-002] Two upstream commits unmerged, and unmergeable as-is
**severity: low · scope: fork sync · by design, tracked not fixed**

`9c48609f` (bookmarks survive re-pagination) and `0f747b82` (content-based
EPUB sync positions) remain unmerged. `git merge upstream/develop` produces 18
conflicts, six `modify/delete`, because both commits straddle live Epub engine
code and subsystems this fork deleted on purpose.

Not a defect so much as a standing cost. See [docs/fork-sync.md](docs/fork-sync.md).

**Close by:** cherry-picking the live hunks only — the `Section`, `ParsedText`,
`ChapterHtmlSlimParser`, `EpubReaderUtils.h` changes — and bumping the cache
format version if layout output changes.

---

## FIXED

### [B-005] The two SD cards hold different bytes under the same bin filename
**severity: low · scope: device provisioning · FIXED 2026-08-07**

Both cards were mounted together and written in one `cpcards` pass, so they now
carry a single identically-named, identically-hashed bin and nothing else:

```
REDACTED-SSID  20260807T0709Z-crosspoint-e194ab7b.bin
OWEN_BNF     20260807T0709Z-crosspoint-e194ab7b.bin
both sha256  564cd3cdd530494dcc7d01adb1ed83ea15e15edccfb24b1f6ffd12990120f14f
```

Verified by hashing the two cards separately and comparing. `cpcards` deletes
superseded `*crosspoint*.bin` before copying, so the three older bins that had
accumulated across the two cards are gone — that divergence had no way to be
noticed while only one card was ever mounted at a time, which is the actual
reason this bug existed.

Root cause B-004 is untouched, so the condition can recur: hold
`CROSSPOINT_RC_HASH` constant across a session, and prefer writing every card in
one `cpcards` run rather than one card per run.

Original report below.

`crosspoint-880ba0f9.bin` is md5 `262f1d51…` on OWEN_BNF (X4) and `930747eb…`
on REDACTED-SSID (X3). Same size, same `1.5.0-BNY-rc+880ba0f9` version stamp;
they differ only in embedded `__TIME__`/`__DATE__` strings, because the build
was relinked between the two copies (root cause is B-004). Identical filenames
with different content defeats later verification.

**Close by:** mounting OWEN_BNF and re-copying from
`.pio/build/gh_release_rc/firmware.bin` so both cards match. Requires the X4
card mounted.

### [B-004] Toggling CROSSPOINT_RC_HASH silently wipes every build directory
**severity: medium · scope: build tooling · FIXED 2026-08-07 · `5dcaba15`**

The sysenv interpolation is gone. `scripts/git_branch.py` — which already owned
`CROSSPOINT_VERSION` for the dev env — now computes the RC stamp from the same
variable, so the ini text never changes and `project.checksum` is stable.

Doing it in Python also lets the value be **checked**, which an interpolation
could not: an unset variable used to stamp a bare trailing `+` (that is B-006).
It now warns with the exact command to re-run and stamps `-rc+unset`, which is
greppable and obviously wrong rather than subtly wrong.

**Verified — and the first test was wrong.** Checking the canary after the FIRST
rc build in a fresh worktree reads as a failure, because adding an env to the
build set legitimately re-checksums. Rebuilding the canary first, then toggling:
hash `aaaa1111` → `cccc3333` survived, and set → unset survived with the warning
firing. `gh_release` still stamps `1.5.0-BNY`; `default` still gets its git
string.

This also unblocks B-017: the NimBLE include paths were put in `spike-build.sh`
specifically to avoid editing the ini, which that script's own header states.

Original report below.

`[env:gh_release_rc]` interpolates `${sysenv.CROSSPOINT_RC_HASH}` into
`build_flags`, so setting or unsetting it changes the resolved config, which
changes `.pio/build/project.checksum`, which makes the next `pio run` clean
**all** env build dirs — not just the target env.

Observed: a stamped `pio run -e gh_release_rc` deleted
`.pio/build/simulator/program`, and a later headless simulator run died with
`no such file or directory`, exit 127. It also caused B-005.

**Close by:** either documenting it in the project guide next to the existing
version-override section, or removing the sysenv interpolation in favour of a
mechanism that does not perturb the checksum. Currently recorded only in
agent memory, not in the repo. Workaround: hold the variable constant across
every `pio run` in a session, including simulator builds.

### [B-015] Create Note displayed no text on iOS, while saving correctly
**severity: high · scope: notes / iOS · FIXED 2026-08-07 · `bb614f73`**

Reported as "Create Note (and possibly Claude) is not displaying text — it
shows one pixel in the upper left instead. It saves fine though."

The iOS target compiles `crosspoint_core` with `OMIT_FONTS`
(`crosspoint-simulator/ios/CMakeLists.txt:178`), and `src/main.cpp:371-372`
registers Space Mono and IBM Plex Mono inside `#ifndef OMIT_FONTS`. So on that
build neither editor face is ever handed to the renderer.

`editorfonts::builtinFontIdFor()` reads a **compile-time table**
(`src/notes/EditorFonts.h:39-45`) and returns `SPACEMONO_12_FONT_ID` regardless
— the constant lives in `fontIds.h` and is unaffected by `OMIT_FONTS`. The old
`resolveEditorFont()` returned that at its FIRST branch without asking whether
the renderer had it, so `drawText` was handed an id with no glyphs behind it.
`fallbackFontId()` had the same flaw: it also picks Space Mono. **Every row of
the Editor Font setting**, not just the shipped default, resolved to a face
absent from the binary. The text buffer was never involved, which is exactly
why saving worked.

It existed twice: `resolveEditorFont()` was copy-pasted into
`NoteEditorActivity.cpp` and `ClaudeChatActivity.cpp`, identical but for the
final UI constant. (Claude chat is separately excluded from iOS by B-014, so
on that target only Create Note was reachable — but the defect was in both.)

**How it was found.** It does not reproduce on the desktop simulator, where the
built-ins ARE registered. Ruled out first, each by running it: the default font
path, an editor family present on the card, render scale 2, `editorFont = 3`
(Space Mono, which is what the card was already set to), on-screen typing, and
the text viewer. The `OMIT_FONTS` difference is visible in the iOS build's own
`GCC_PREPROCESSOR_DEFINITIONS`.

Fixed by consolidating the two copies into `editorfonts::resolve()`, which asks
whether a font is registered before returning it and falls through to the UI
face when the binary contains no editor face at all. Chrome is the wrong
texture for a writing surface, but it is text on screen instead of a blank page.

**Verified:** five new tests in `test/editor_fonts` covering the reported case,
the same for every row, and the three orderings that must not regress; 215/215
host tests; device `gh_release` and desktop canary both build; desktop
rendering unchanged (it still resolves to Space Mono, because there it is
really registered). **Not yet confirmed on the phone** — that needs build-35.

### [B-016] Daisywheel Select typed uppercase while the rotation button was held
**severity: medium · scope: text entry · FIXED 2026-08-07 · `8aad57ec`**

Reported as "Select is not selecting the middle character", in the Mac
simulator's Device owner field. Reproduced exactly: hold Right to rotate, press
Select during the hold, and the field takes `H` instead of `h` — `longPick()`
ran instead of `tapPick()`.

`MappedInputManager::getHeldTime()` **takes no button argument**
(`src/MappedInputManager.h:74`). It reports the longest-held button on the
device, so `DaisyEntryActivity`'s long-press check was asking "has anything been
held past `LONG_PRESS_MS`", not "has THIS pick been held past it". Rotation
auto-repeats — holding Left/Right is how the wheel is meant to be driven — so
the threshold was already satisfied before the pick button went down, and the
first frame fired the uppercase branch. Every pick made while rotating was
uppercase, not only Select.

Fixed by timing each pick locally with `millis()` at its press. Kept out of the
HAL deliberately: the HAL surface mirrors the firmware's and this needs no new
hardware concept.

**Verified** in the simulator, three cases: a plain tap gives the lowercase
middle char, a long press still gives uppercase (`bB` from tap-then-hold), and
Select during a rotation hold now gives lowercase where it gave `H`. 215/215
host tests; device `gh_release` builds.

**Related, untouched:** `KeyboardEntryActivity.cpp:653,727,759,766` compare the
same global `getHeldTime()` against per-button holds. Not reported and not
reproduced — the grid keyboard's nav buttons may not repeat the same way — but
it is the same shape and worth a look before trusting long-press there.

### [B-014] The iOS Home menu listed Claude, which cannot work on a phone
**severity: medium · scope: iOS app · FIXED 2026-08-07 · `641e463a` · SUPERSEDED same day**

> **Superseded by S-010 / `f1459353`.** Claude is BACK on iOS and that is correct.
> The premise here — that `WifiCredentialStore` is not compiled for the phone —
> stopped being true when `CROSSPOINT_NO_NETWORK` was split: the credential store
> is in the iOS build again, so the link failure this entry describes cannot
> recur. Do not re-apply the guard. What was genuinely right about this entry is
> the rule, not the remedy: a row that opens a screen which cannot work is a
> defect. Claude can work now.

The iOS build defines `CROSSPOINT_NO_NETWORK`, and `HomeActivity.cpp:36` still
counted Claude in the menu — so the row rendered, was selectable, and opened a
screen that could never do anything. `claudechat` needs a saved Wi-Fi
credential (`ClaudeChat.cpp:118` calls `WIFI_STORE.findCredential`) and an API
key read off the SD card; `src/WifiCredentialStore.cpp` is not compiled for iOS
at all.

This is the same lying-control class as B-008, and it also had teeth: once the
notes TUs entered the generated iOS source set, `ClaudeChat.cpp` failed to link
against the excluded credential store and **took the build-30 archive down**
with `ld: symbol(s) not found for architecture arm64`.

Fixed by guarding the row under `CROSSPOINT_NO_NETWORK` across all four sources
of truth the header warns about — `getMenuItemCount`, both index maps, and the
label/icon vectors — plus the dispatch arm, `onClaudeOpen`, `goToClaudeChat`
and the `ClaudeChatActivity` include; and by adding `src/notes/ClaudeChat.cpp`
and `src/activities/util/ClaudeChatActivity.cpp` to
`CROSSPOINT_IOS_EXCLUDED_FW_SOURCES`.

**Verified:** device `gh_release` still builds (the network path is unchanged),
the desktop canary builds and boots, and the iOS configure reports
`20 iOS exclusions all resolve`. Device-side behaviour of the network build is
unchanged by construction — nothing outside `#ifdef CROSSPOINT_NO_NETWORK` moved.

### [B-013] Opening an oversized `.txt` in the note editor destroyed it
**severity: high · scope: data loss · FIXED 2026-08-07 · `641e463a`**

`NoteEditorActivity::onEnter` refuses a file at or over the 8 KB cap
(`:115-118`), logs it, and sets `bufferFull` — but leaves `buf` allocated and
**empty**. `onExit` (`:136`) then calls `save()` unconditionally, and `save()`
never consulted the flag. `openFileForWrite` is `O_TRUNC`, and an empty buffer
is a legitimate save (the comment in `save()` says so: it is how "the owner
deleted this text" is recorded), so nothing downstream could tell the two apart.

Manage Files offers Edit for `.md` **and `.txt`**
(`FileManagerActivity.cpp:172-174`), and a `.txt` book is routinely far larger
than 8 KB. Open one, read "refusing to open" on screen, press Back — the file
is now zero bytes. Unrecoverable, and it is the owner's own content.

The OOM sibling path was safe only by accident: there `buf` is null, so
`save()` returns at its first line.

`bufferFull` could not be the guard, because `:260` sets it again when typing
hits the cap — that buffer holds real edits and must still be written. Fixed
with a separate `loadRefused` flag, set only on the refuse-to-load path and
checked at the top of `save()`.

**Close-out note:** verified by reading the path end to end and by the device +
desktop builds; not yet exercised on hardware. The failing sequence is
Manage Files → a `.txt` book → Edit → Back, and the file should be untouched.

### [B-012] Home draws a line of content below the bottom of the screen, every paint
**severity: medium · scope: Home / theme layout · FIXED 2026-08-07 · `fc76342a`**

**Missing precondition: Recents must be EMPTY.** With books present this does not
reproduce at all — `splitPages` is `homeMenuOnSecondPage && bookCount > 0`, so a
populated Home takes the split branch and a bare one does not. With the list
emptied the report reproduces verbatim: 1756 escapes, x 40-175, y 837-862.

**The suspect in the original report was wrong**, and so was the first fix built
on it: correcting the non-split `menuRect` height changed the escape count by
exactly zero. `drawButtonMenu` (both `LyraTheme` and `BaseTheme`) lays rows out at
a fixed pitch from `rect.y` and never reads `rect.height`, so no rect correction
could have helped. Instrumenting `drawText` to log any origin below y=760 named
the culprit in one run: `Settings` at y=833.

Empty Recents still reserves a full 312px cover tile — `drawEmptyRecents` paints
the "No open book" panel there — leaving ~450px for 7 rows at a 72px pitch.

Fixed by making `drawButtonMenu` fit the rect it is handed, compressing the gap
first and then the tiles, so rows compress rather than vanish (Settings was the
row being lost). The `menuRect` height is made consistent too.

**Verified:** 1756 -> 0 with Recents empty, 0 -> 0 with nine books, all seven rows
on-panel above the button hints in a screenshot, 213/213 host tests.

Original report below.

Home paints ink 37-62 pixels below the panel. It is dropped, so nothing is
corrupted and the screen looks fine — but whatever that line is, the owner
never sees it, and each lost pixel costs an ERR log line. A 1.5-second boot
produced **1,756** of them.

Reproduce, no interaction needed (X4 profile, 480x800 logical in portrait):

```bash
SDL_VIDEODRIVER=dummy CROSSPOINT_SIM_INPUT_SCRIPT='2000:HOME;3500:QUIT' \
  .pio/build/simulator/program 2>&1 | grep -c 'Outside range'
```

The pixels form one band: x 40-175, y **837-862**, against a last valid row of
799. It repeats on every Home repaint (69, 571, 238, 466, 412 … per paint in a
20-second run), and it happens both with a real `state.json` and with a
minimal one, so it is not an artifact of missing reader state. Only
`drawText` / `drawLine` / `drawIcon` / glyph ink can log this — `fillRect` and
friends clip in logical space — and a 136x26 sparse box is the shape of a text
line, not a rule or a box.

**Culprit not identified.** The leading suspect is the non-split `menuRect` in
`HomeActivity.cpp:389-394`: its `y` starts at
`homeTopPadding + coverAreaHeight + homeMenuTopOffset` while its `height`
subtracts `headerHeight` instead of `coverAreaHeight`, so the rect's bottom
lands at `pageHeight + coverAreaHeight - headerHeight - verticalSpacing -
buttonHintsHeight` — past the screen whenever the cover area is taller than
that sum, which on Lyra Six it is. `drawButtonMenu` then has room it does not
have for the last row. This arithmetic has NOT been confirmed against the
observed band; it is where to look first, not the answer.

**Close by:** instrumenting `drawText` to print the string when the origin is
out of range (or bisecting the Home render), then fixing the geometry — and
adding a headless assertion, since this is exactly the class of defect a
screenshot hides and the log announces 1,756 times.

### [B-011] drawRect's lineWidth overload draws one pixel outside its rectangle
**severity: low · scope: rendering primitives · FIXED 2026-08-07 · `6d415094`**

Fixed with `x + width - 1 - i` / `y + height - 1 - i`. Both call sites were
checked and neither had been nudged to compensate, so both move toward their
intent: the popup outline now matches the `fillRect` drawn at the same
geometry one line below it, and adjacent daisy-keyboard cells stop
overlapping by a pixel.

**Verified RED first.** Two tests in `test/renderer_bounds` fail against the
old arithmetic — 1324 escaped pixels for a full-screen bordered rect, 24 for
one flush to the corner — and pass after. The second pins the two overloads to
each other, so clamping instead of fixing the extent would not satisfy it.
Full suite: 213/213.

Original report below.

The two overloads disagree about what the rectangle's extent means. The
5-argument one is correct — `drawLine(x, y, x + width - 1, y, ...)`
(`GfxRenderer.cpp:834-839`). The 6-argument one, which takes a `lineWidth`,
uses `x + width` and `y + height` (`GfxRenderer.cpp:842-849`), so its border
lands one pixel right of and one pixel below the rect it was handed — despite
the comment above it reading "Border is inside the rectangle".

Two callers: the popup progress-bar outline (`BaseTheme.cpp:781`) and the
daisy keyboard's selected-cell box (`KeyboardPanel.cpp:267`). Neither sits at
a screen edge today, so the symptom is a border 1px larger than intended
rather than lost pixels; a caller that ever draws flush right or bottom would
have that edge silently dropped by `drawPixel`'s bounds check and would log a
line per pixel (the B-010 mechanism).

**Close by:** using `x + width - 1 - i` / `y + height - 1 - i` in the loop, then
checking both call sites still look right — they may have been nudged to
compensate.

### [B-010] The Claude prompt hint ran off the right edge of the panel
**severity: low · scope: Claude chat / text rendering · FIXED 2026-08-06 · `c512eef1`**

Found twice the same evening, from opposite directions: by driving the daisy
layout and looking at the screen, and in the log of the session that fixed the
OK-key crash, whose 28-minute run carried 23 of these:

```
[1451261] [ERR] [GFX] !! Outside range (480, 120) -> (120, -1)
…
[1451261] [ERR] [GFX] !! Outside range (494, 131) -> (131, -14)
```

Reading them: the first pair is the logical coordinate, the second the
post-rotation framebuffer one (`GfxRenderer.cpp:582`). Portrait maps
`phyY = panelHeight - 1 - x` (`:224-225`), so on the X4's 480-wide logical
screen a negative `phyY` means x ran past column 479 — here by 1 to 15 pixels,
across rows 120-132, which is exactly one line of Space Mono 12 ink at
`contentTop`. `drawPixel` drops the write before touching the framebuffer
(`:574-583`), so nothing was corrupted; the glyph tails were simply cut off,
and each lost pixel cost a log line.

The string was `"Type a question, then press Ask."` — 32 characters drawn raw
at `contentSidePadding`, with no wrap and no truncation. It fit while the
editor borrowed the narrow 10 pt UI face and stopped fitting the moment the
editor font became a real monospace face. The timestamp is 5 s after the
answer arrived, i.e. Back to an emptied prompt, which is when the hint shows.

Fixed by wrapping it to the `maxWidth` the prompt already computes, and by
making it (plus NoteEditor's OOM message, same shape, two sites)
`tr()`-translated instead of a hardcoded English literal.

Verified independently of the fixing session: a headless run on `c512eef1`
that walks Home to Claude and stops on the empty prompt logs **0**
`Outside range` lines from `Entering activity: ClaudeChat` onward. (The same
run logs 7,902 before it, all on Home — that is B-012, a different defect.)

### [B-009] An unrepresentable codepoint vanished and took its width with it
**severity: low · scope: Claude chat / text rendering · FIXED 2026-08-07**

Done in two steps. First the log was demoted `LOG_ERR` -> `LOG_DBG`
(`GfxRenderer.cpp:425`), removing the per-character, per-paint spam that fed the
`RTC_NOINIT` crash ring and pushed real panic history out of a 16-entry buffer.

Then the character itself, which was left as an owner call between three
options. The pick is **the fallback chain**, because it is the only one that
fixes the metrics half as a side effect and needs no font rebuild:
`EpdFont::getGlyph` now tries U+FFFD and then `'?'` (`FALLBACK_GLYPH`,
`Utf8.h`). U+FFFD alone was not enough — only 52 of the 84 built-in faces carry
one, and the four that do not are exactly the editor and UI-chrome faces.

That also closes the zero-advance shift the entry flagged as unmeasured: it was
real. `drawText` reads `glyph ? glyph->advanceX : 0` (`GfxRenderer.cpp:726`), so
before the fix an unrepresentable character contributed **zero width** and the
rest of the line slid left into its place — a string measured with the emoji
present no longer matched what was drawn. A resolved `'?'` restores the advance.

Both substitutes are excluded from recursing, not just the one being asked for:
U+FFFD -> `'?'` -> U+FFFD is a cycle, and a face missing both overflowed the
stack. Found by the existing `EpdFont` cases in `test/differential_rounding`
segfaulting on the first attempt; their synthetic font carries neither.

**Verified RED first**, two new cases in `test/renderer_bounds`: the unit one
(`getGlyph(0x1F60A)` resolves to the same glyph as `'?'`, with a non-zero
advance) and the metric one (`getTextWidth("a😊b") > getTextWidth("ab")`). Both
fail against the old chain. Full suite 215/215, desktop canary green.

**Verified on screen too**, since a substitution nobody can see is not a fix: a
file named `emoji 😊 test.md` in the SD root, listed by Browse Files, renders as
`emoji ? test` — one glyph wide, spacing intact, in the UI face that has no
U+FFFD either.

A visible `▯` would be nicer than `?` and is one `#define` away
(`FALLBACK_GLYPH` in `Utf8.h`) — but it needs a glyph in every face first, which
is the font-rebuild option this deliberately avoided.

Original report below.

The API answers with emoji unprompted. No font in this firmware can represent
one, so the character disappears and the render logs an error every time the
text is painted:

```
[1446176] [ERR] [GFX] No glyph for codepoint 128522     (U+1F60A 😊)
```

Confirmed chain. `renderCharImpl` looks the glyph up and bails
(`GfxRenderer.cpp:417-420`); `EpdFont::getGlyph` had already fallen back to
U+FFFD and returned nullptr (`EpdFont.cpp:181-189`), which only happens when
the face carries neither the codepoint nor the replacement character. The
answer is painted in Space Mono 12 — `SETTINGS.editorFont` defaults to the
card-only iA Writer row, so `resolveEditorFont` falls through to the built-in
mono — and `grep -c 0xFFFD spacemono_12_regular.h` is **0**. Same for
ibmplexmono, librefranklin and ubuntu, i.e. both editor faces and the UI
chrome faces. The built-in converter never requests a codepoint above U+FFFD
(`lib/EpdFont/scripts/fontconvert.py`), and no SD interval preset includes an
emoji block (`fontconvert_sdcard.py`), so this cannot be fixed by installing a
family.

Two consequences beyond the missing character. `prevAdvanceFP = glyph ? ... : 0`
(`GfxRenderer.cpp:726`) advances the cursor by zero on a miss, so the rest of
the line shifts left into the gap rather than leaving a space — wrap widths
were computed with the emoji present, so the line ends short. And LOG_ERR
feeds the RTC_NOINIT crash ring, so a long answer full of emoji can push real
history out of a 16-entry buffer.

Nothing sanitises the response: `ClaudeChat.cpp` stores the model's bytes
verbatim, `layoutAnswer` only splits and soft-wraps, and the request carries no
system prompt that would ask for plain text.

Not Claude-specific — an EPUB or a BLE-typed note with emoji or CJK takes the
same path. Claude is just the surface that produces them daily.

**Close by:** deciding where to intervene. Adding U+FFFD to the four faces
turns silence into a visible ▯ and costs one glyph each; stripping
non-representable codepoints before layout keeps the line metrics honest;
a system prompt would reduce but not eliminate them. Demoting the log to DEBUG
is worth doing regardless — the firmware cannot control what a remote server
sends, so this is not an error condition.

### [B-008] iOS app offers WiFi and web-server menus that cannot work
**severity: medium · scope: iOS app · FIXED + VERIFIED 2026-08-03 · SUPERSEDED 2026-08-07**

> **Superseded by S-010 / `f1459353`.** Wi-Fi Networks and File Transfer are BACK
> on iOS and that is correct. This entry's diagnosis was exact for its moment —
> `WiFi.scanNetworks()` returned a synthetic list and `localIP()` was hardcoded to
> `127.0.0.1`, so the screen drew a QR code pointing at loopback. Simulator
> `4a98ba8` then gave the target a real radio (NetworkExtension, in-process HTTP,
> Bonjour, servers bound to all interfaces), which removed the premise. Keeping
> the guard after that suppressed features that work.
>
> Still true and still enforced: SD Firmware Update and OTA remain hidden on iOS,
> now under `CROSSPOINT_NO_DEVICE_FLASH`. Those write an ESP32 partition.

Fixed by `CROSSPOINT_NO_NETWORK` guards (firmware `5bce63bf`) plus iOS TU
exclusions (simulator `ac8cdef`).

**Verified by driving the iOS Simulator, not by a clean compile.** Fresh
install on crosspoint-x3-air, iOS 26.5:
- Home menu shows exactly Browse Files / Recent Books / Settings. **File
  Transfer is gone**, nothing dangles.
- Settings > System shows Time to Sleep, Quick Resume, the three Sleep Screen
  rows, Keep Screen Awake, the three Clock rows, Clear Reading Cache,
  Language, Device owner. **Wi-Fi Networks and SD Card Firmware Update are
  gone**, nothing dangles.
- App launches, a book opens, pages turn, images render.
- Font picker lists exactly the four S-tier families with live previews.

Also verified inert on DEVICE firmware: `CROSSPOINT_NO_NETWORK` is undefined
in platformio.ini and gh_release_rc builds identically at 3,658,031 bytes
flash before and after the guards.

Original report below.

The iOS build compiles and ships the whole firmware network stack, and exposes
it in the UI, but none of it can function on a phone. `WiFi.scanNetworks()`
returns a synthetic list (`crosspoint-simulator/src/WiFi.h:244`), and
`CrossPointWebServerActivity` shows the user `WiFi.localIP()`, which is
hardcoded to **127.0.0.1** (`WiFi.h:196`) — so the app renders a URL and QR
code pointing at loopback that nothing can reach. OTA is stubbed to always
report NO_UPDATE and to fail install with `INTERNAL_UPDATE_ERROR`
(`simulator_ota.cpp:19`). SD Firmware Update offers to flash a `.bin` from an
SD card the device does not have.

This is the lying-control class of defect: the control exists, is reachable,
and silently does nothing useful. `Info.plist.in` also carries no
`NSLocalNetworkUsageDescription`.

**Close by:** hiding these entries on the iOS target (menu surgery in
`SettingsActivity` / `NetworkModeSelectionActivity`), ideally alongside
compiling the ~16 dead TUs out. Note this is capability *removal* from a
surface where the capability never worked — flag it as such when doing it.

### [B-007] iOS seed fonts are stored twice on device
**severity: low · scope: iOS app · FIXED + VERIFIED 2026-08-03**

Fixed by symlinking rather than copying (simulator `ac8cdef`). Verified from
the app's own launch log on a fresh install: `[harness] symlinked
fonts/TeXGyreSchola -> bundle SeedFonts` and the same for Rosarivo,
Coelacanth and Edgar. The font picker then listed all four and text rendered,
so the symlinks resolve for reading. Saves ~54.8 MB of duplicated storage.

Original report below.

`seedOneFontDirectory` hard-copies every bundled `.cpfont` into
`Documents/fonts/`, including the `2x/` subdirectory
(`crosspoint-simulator/ios/CrossPointFsPrep.cpp:193,245`). The 54.8 MB seed
set therefore exists in both the app bundle and Documents, so a 19.8 MB
download presents as roughly **113 MB** in iOS Storage settings — the number
users actually see.

**Close by:** symlinking rather than copying, provided the installer and prune
paths never write through the link. Would halve the visible footprint to
~58 MB with no capability change.

### [B-001] Quick Resume pin made the sleep-screen setting a lying control
**severity: high · fixed 2026-08-03 · `6bb7efc8`, `780982ed`**

`normalizeRetiredSettings()` pinned `quickResumeSleepScreen` to ON on every
load (`CrossPointSettings.cpp:136`), and the whole sleep group lived in the
Display settings category, which the device UI drops
(`SettingsActivity.cpp:48`). Net effect: an owner could set a custom sleep
image and never see it, because while Quick Resume is ON `SleepActivity::onEnter`
returns before it ever reads `sleepScreen` — and the inactivity timeout is the
common way a reader sleeps. The only control was the web UI, and a reload
reverted even that.

Found by tracing the sleep path from the report "sleep.bmp never shows"
rather than trusting the settings file, which already read `0`.

Fixed by moving the whole sleep group (Sleep Screen, both cover options, Quick
Resume on Timeout) to System and dropping the pin. Verified on the simulator:
the row loads OFF from disk where it would previously have read ON, and a 60s
idle produces `Auto-sleep triggered` then `Loading: /sleep.bmp`.

### [B-000] install-sim-fonts.py silently reinstalled all 15 font families
**severity: medium · fixed 2026-08-03 · `4c0571aa`**

The installer defaulted to "every curated family `sd-fonts.yaml` can build".
Safe while several families lacked sources; once all 15 became buildable
(2026-08-01) that default became "install all 15", so a routine re-run of the
documented command broke the four-family S-tier parity with both SD cards and
the iOS seed bundle. The ruling was written in three places and enforced in
none.

Fixed with `installed_families:` in `sd-fonts.yaml` as the single source of
truth; `--all-curated` opts back in and warns. See
[docs/sd-card-fonts.md](docs/sd-card-fonts.md).
