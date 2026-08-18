# Known bugs and open defects

Running list for this fork. Newest first within each section. A bug leaves
OPEN only when there is evidence it is fixed — a passing build is not evidence
for anything you cannot observe headlessly (see the device-feel rule in the
project guide).

Format: `**[id] Title** — severity · where · status`, then what breaks, how it
was found, and what closing it requires.

**Defects only, and only `B-` ids.** Anything that is merely owed goes to
[TODO.md](TODO.md), even when it was found during bug work. A parked todo lived
here as `[T-001]` until 2026-08-15 because this file predated `TODO.md`; two
different items ended up sharing that id. `scripts/tracker-check.sh` now fails
on that.

## Where the rest of the work lives

Four trackers across two repos. `scripts/tracker-check.sh` prints all of them
with open counts and the next free id.

| Tracker | Ids | Holds |
|---|---|---|
| [TODO.md](TODO.md) | `T-` | Firmware work that is owed |
| **BUGS.md** (this file) | `B-` | Firmware defects |
| `../crosspoint-simulator/TODO.md` | `ST-` | Simulator work that is owed |
| `../crosspoint-simulator/BUGS.md` | `S-` | Simulator defects |

Not tracked as numbered items: the upstream backlog
([docs/fork-sync.md](docs/fork-sync.md)) and the sibling-fork candidates
([docs/fork-ecosystem.md](docs/fork-ecosystem.md)).

---

## OPEN

### [B-031] Thirteen more bare `new`, all on the EPUB reading path
**severity: high · scope: memory safety · found 2026-08-18 · owner ruling: sweep all 13**

What [B-030] left behind. Same defect — a bare `new` under `-fno-exceptions`
calls `abort()` on OOM — on a path that matters more than covers do:

| File | Sites |
|---|---|
| `parsers/ChapterHtmlSlimParser.cpp:233,319,1743,1753,1779` | `new Page()` per page while paginating, `new ParsedText` per block |
| `Epub.cpp:427,429` | metadata cache, CSS parser |
| `Epub/Page.cpp:79,172` | `PageImage`, `Page` |
| `converters/ImageDecoderFactory.cpp:28,33` | JPEG and PNG decoders |
| `Xtc.cpp:28` | Xtc parser |
| `EpubReaderActivity.cpp:997` | `new Section` |

The five in the parser are the ones with teeth: a `Page` is allocated **per page
while a chapter paginates**, so on a long chapter with a fragmented heap the
abort lands in the middle of reading a book, not in the middle of a thumbnail.

**Owner ruling 2026-08-17-18 walk: sweep all thirteen**, same treatment as
B-030 — `makeUniqueNoThrow`, null check, `LOG_ERR`, and a real degradation per
site rather than one blanket bail.

**The design question this opens, and it is the reason the parser half is not
mechanical:** several callers return pages and sections by value or by
`unique_ptr` with no failure case, so a failed allocation has to mean something.
Deciding what a half-paginated chapter does — stop the chapter early at the last
good page, or refuse the book — is part of the work, not a detail to be picked
silently while editing.

**Done looks like:** zero bare `new` outside vendored code, `-e default` and
`-e simulator` green, host tests green, and the pagination failure behavior
written down here rather than only in the diff.


### [B-006] X4 running firmware carries an empty version stamp
**severity: low · scope: device provisioning · found 2026-08-02**

The X4 runs a build stamped `1.5.0-BNY-rc+` — empty suffix. `gh_release_rc`
composes its version as `1.5.0-BNY-rc+${sysenv.CROSSPOINT_RC_HASH}`
(`platformio.ini:186`), and the flash was run without that variable set. The
code is identical to `crosspoint-880ba0f9.bin`; only the stamp is wrong. It
feeds the OTA version comparison, and it makes the running build
unidentifiable after the fact.

**Root cause fixed and verified 2026-08-08.** `platformio.ini` no longer
interpolates `${sysenv.CROSSPOINT_RC_HASH}`; `scripts/git_branch.py` owns the
version and, with the variable unset, warns loudly and stamps `-rc+unset`.
Confirmed by building `gh_release_rc` with the variable removed from the
environment: the binary contains `1.5.0-BNY-rc+unset`, so the empty suffix that
produced this entry cannot recur.

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

### [B-003] Exploded `.epub` directories present as folders, never as books
**severity: low · scope: content · found 2026-08-03 · mechanism corrected 2026-08-07**

The X3 card carried 10 entries named `*.epub` that are DIRECTORIES
(`META-INF/`, `mimetype`, `OEBPS/`) rather than zip containers.

They never reach the zip layer at all. `FileBrowserActivity::loadFiles` marks a
directory by appending `/` (`src/activities/home/FileBrowserActivity.cpp:46-47`),
and `activateSelected` branches on `entry.back() == '/'` (`:139`) to **navigate
into** it (`:194-197`) instead of calling `onSelectBook` (`:200`). So an exploded
`foo.epub/` is a browsable folder holding `META-INF/` and `OEBPS/`, with
`mimetype` hidden for having no matching extension in the filter. There is no
error and no failure mode to observe — the book simply cannot be opened.

An earlier version of this entry said they "almost certainly do not open"
because miniz is the only container library and `lib/Epub/Epub/Section.cpp`
unzips at runtime. Both halves were wrong: the zip layer is
`lib/ZipFile/ZipFile.cpp`, reached from `lib/Epub/Epub.cpp:218`, and
`Section.cpp` contains no zip call at all — only two comments mentioning
"unzipped". The conclusion held; the reasoning behind it did not.

They are preserved at `~/crosspoint-books/_exploded/`. Also note
`ls *.epub | wc -l` on such a card reports a wildly inflated count because it
recurses into the directories (reported 510 for a real 76).

**Close by:** re-zipping them as proper EPUBs (mimetype stored first,
uncompressed) or discarding them. Opening one to "confirm the failure mode" is
no longer a useful step — the branch above is unambiguous.

### [B-002] Upstream commits unmerged, and unmergeable as-is
**severity: low · scope: fork sync · by design, tracked not fixed**

Both named commits — `9c48609f` (bookmarks survive re-pagination) and
`0f747b82` (content-based EPUB sync positions) — are still unmerged, but the
"two" in the title has been stale for a while. Re-measured **2026-08-17**:
`git rev-list --left-right --count main...upstream/develop` reports **502 ahead
/ 58 behind**, up from 255 / 13 on 2026-08-07 — 45 more upstream commits in ten
days, so this count ages faster than any breakdown written against it.

Note before acting on that 58: `scripts/repo-status.sh` OVERSTATES the backlog,
because a commit already applied by hand still counts as unmerged. On the last
pass 6 of 45 were already present. And never probe with
`git cherry-pick -X ours` — it reports conflicts as clean.
`git merge upstream/develop` produces 18 conflicts, six `modify/delete`,
because the named commits straddle live Epub engine code and subsystems this
fork deleted on purpose.

Of the thirteen, roughly five apply cleanly to live fork code, three apply
partially, and three are genuinely N/A because the subsystems were deleted
(bookmarks, dictionary, translations). Re-measure before acting rather than
trusting this breakdown — it ages the same way the "two" did.

Not a defect so much as a standing cost. See [docs/fork-sync.md](docs/fork-sync.md).

**Close by:** cherry-picking the live hunks only — the `Section`, `ParsedText`,
`ChapterHtmlSlimParser`, `EpubReaderUtils.h` changes — and bumping the cache
format version if layout output changes.

---

## FIXED

### [B-030] Thirteen allocations on the image and Wi-Fi paths abort instead of failing — FIXED 2026-08-18
**severity: high · scope: memory safety · found + fixed 2026-08-18**

With `-fno-exceptions`, a bare `new` that fails calls `abort()` — it does not
return `nullptr`. CLAUDE.md says so in two places and says never to write one.
Thirteen of them were live, and every one sat on a path where OOM is the
expected failure rather than an impossibility:

| Where | Allocation | At worst |
|---|---|---|
| `BitmapHelpers.h` Atkinson x2, Floyd-Steinberg | 8 error-row arrays | 3 x 4,104 B / 2 x 4,100 B |
| `PngToBmpConverter.cpp:671-672` | scaling accumulators | 8,192 B + 4,096 B |
| `Bitmap.cpp:171-173`, `PngToBmpConverter.cpp:655-660` | the ditherers themselves | — |
| `CrossPointWebServer.cpp:159,230,237` | HTTP server, WebDAV handler, WebSocket server | — |
| `CrossPointWebServerActivity.cpp:227,250` | captive-portal DNS, the server instance | — |

`MAX_IMAGE_WIDTH` is 2048, so the dither path asks for ~12 KB in separate 4 KB
blocks **while a cover is being decoded**, which is the tightest the heap gets.
The failure mode was a reboot where a missing cover was the right answer.

**What made it clearly wrong rather than arguable:** `PngToBmpConverter.cpp`
null-checks its `malloc` for the row buffer and logs `LOG_ERR` — then twenty
lines later allocated with bare `new` and checked nothing. Same function, same
path, two disciplines. Likewise the Wi-Fi path: `startWebServer()` already
releases the SD font caches *for* the server allocation, then allocated in a way
that could only abort.

This is NOT the unbounded-allocation bug [B-024] fixed. Dimensions are validated
first (`Bitmap.cpp:122-127` rejects bad and oversized images). This is the
graceful-failure half.

**Fixed:** every site is `new (std::nothrow)` or `makeUniqueNoThrow`, checked,
with `LOG_ERR` naming the size. The three ditherer classes gained `ok()` —
their constructors allocate several rows, so a partial allocation has to be
detectable; the destructors already handled it, since `delete[] nullptr` is a
no-op. Degradation is per-site and deliberate: no cover instead of a reboot, a
new `BmpReaderError::OomDitherer` (appended, never inserted — `-Werror=switch`
makes every consumer handle it), HTTP-only transfer without WebDAV, HTTP uploads
without the WebSocket fast path, an AP without the captive-portal redirect, and
`onGoHome()` rather than a state nothing dismisses if the server itself cannot
be allocated.

**Proof, and its limit.** `-e default` and `-e simulator` both build; 353/353
host tests pass; zero bare `new` remain in the five files, by grep. **The OOM
branch itself is not covered by a test, and I could not make one that is honest
on a host:** a nothrow array of 2^40 `int16_t` does not return null on macOS, it
gets the process OOM-killed (measured — exit 137). Forcing a failed allocation
needs either an allocator seam or the device. So this is a defect removed by
construction, not a behavior demonstrated.

**Owner ruling 2026-08-18: that is accepted, and no test seam is being built.**
A fault-injecting allocator would be new test-only machinery in the memory
layer — the layer where a mistake is worst — to exercise branches that log and
return false, and it would only ever fail the simulator's allocator, not the
device's. The claim this entry makes is therefore deliberately the weaker one:
the code can no longer abort where it used to, verifiable by reading and by
grep. Do not re-raise this as missing coverage; it is a decision.


### [B-028] A note does not repaint while a host keyboard types into it — FIXED 2026-08-17
**severity: high · scope: NoteEditorActivity · reported and FIXED 2026-08-17**

Reported against TestFlight build 85: *"create note with iOS keyboard: not
updating while typing at all."* Taken literally, and literally is what it was —
the characters reached the buffer and were saved correctly on exit; the screen
simply never changed once while they were being typed.

**Death point: `src/activities/util/NoteEditorActivity.cpp:620`,
`if (panelHidden) return;`.** The host-typed text is drained into `buf` at
line 587 and sets `dirty`, but the only code that acts on `dirty` — the
debounced `relayout()` / `ensureCursorVisible()` / `requestUpdate()` block —
sat at the BOTTOM of `loop()`, below that early return. `panelHidden` is
`mappedInput.isHostKeyboardVisible()`, which is a constant false on device and
true exactly when a host software keyboard is up. So on hardware the block was
always reached, and on an iPhone with the keyboard raised it was never reached,
from the first keystroke to the last. The guard was added for the panel's
BUTTON input (a hidden panel must not take a Confirm), and it took the repaint
with it.

`ClaudeChatActivity` — the sibling editor, same channel, same debounce — never
had the bug because it has no early return in `loop()` at all: it gates the
panel's input inside an `if` and hides the panel in `render()`
(`ClaudeChatActivity.cpp:622`).

**Fix:** the BLE drain and the debounced repaint move ABOVE the `panelHidden`
guard, which is the shape ClaudeChat already has. Nothing else changed; the
panel-visible path executes in exactly the order it did.

**Evidence, headless, both directions.** Two frames captured either side of a
`TYPE` in a fresh note, under `CROSSPOINT_SIM_HOST_KEYBOARD=1` (the flag that
makes a desktop build answer `isHostKeyboardVisible()` the way a phone does):

| | before fix | after fix |
|---|---|---|
| panel visible (`=0`) | frames differ | frames differ, **byte-identical to the pre-fix pair** |
| host keyboard up (`=1`) | frames **identical** (`a04d4495…` twice) | frames differ; "hello world" and the count `11` on screen |

Pinned by `crosspoint-simulator/tests/test_note_editor_repaint.sh`, verified
failing-first: with the fix stashed the control arm passes and the
`host_keyboard` arm fails at the frame comparison.

### [B-027] Curves are still logical-resolution on a supersampled build — FIXED
**severity: low · scope: GfxRenderer · found 2026-08-15 · lines, polygons and arcs all FIXED by 2026-08-16**

**The arc half landed 2026-08-16 (`21371f79`).** It was recorded below as
needing a ruling — "a device-resolution arc has to decide what a dither cell
means before it can be written" — and it turned out not to. The BOUNDARY is
geometry and belongs at device resolution; the DITHER is texture and stays
logical. `fillArc` walks device rows while the ink decision still asks the
logical coordinate, so a solid arc gets a smooth edge and a dithered arc gets a
smooth edge around an unchanged texture. Nothing about the dither moved, so
there was nothing to rule on.

Geometry is pinned to the logical shape (outer edge at `maxRadius + 0.5`,
centres at `k*S + S/2`) so no corner shifted, and the path is guarded on
`renderScale() > 1` so the device build is untouched. `test/hires_shapes` gains
three cases at scale 3, all verified failing-first against a forced recompile.

Only the 1-px `drawLine` diagonal remains logical, and that is the deliberate
exclusion below, not an omission.

**Original entry follows.**

`drawPixel()` paints a `RENDER_SCALE` x `RENDER_SCALE` block
(`lib/GfxRenderer/GfxRenderer.cpp:557-565`), which is exactly right for an
axis-aligned edge and exactly wrong for anything slanted or curved: the
staircase scales with the shape while text, on the hi-res glyph path, does not.
The shipped iOS app pins `RENDER_SCALE=3`
(`crosspoint-simulator/ios/CMakeLists.txt:89,208`), so on a phone every such
edge sits at a third of the resolution of the characters beside it.

Slanted **thick lines** were fixed on 2026-08-15 (the keyboard's Return arrow;
`test/hires_shapes` pins it). Still logical-resolution:

| Primitive | Where it shows |
|---|---|
| `fillArc` / `fillRoundedRect` (`:1423-1500`) | Every rounded corner — key backgrounds, popups, list pills, and the Return arrow's own elbow, which is now the coarsest thing left in that glyph |
| `drawArc` / `drawRoundedRect` | Stroked rounded borders |
| ~~`fillPolygon`~~ | **FIXED 2026-08-15/16** — see below |
| 1-px `drawLine` diagonals | Left deliberately: a device-resolution hairline would be 1/3 the weight, not smoother |

**The polygon half is closed.** `fillPolygon` rasterizes at device resolution
on a supersampled build, and triangles additionally go through exact integer
half-space tests rather than the scanline sweep — the sweep had two faults that
made symmetric input render asymmetric (division truncation following the
edge-walk order, and a parity rule that dropped the row at each edge's minimum
y). Sampling vertices at the centre of their logical pixel keeps the shape where
the logical form put it, so nothing shifts by half a pixel. `test/return_arrow`
pins it at RENDER_SCALE 1 and 3.

`fillPolygon` was tractable precisely where the arcs are not: it writes a solid
state with no dithering, so it has no "what is a dither cell in device space"
question to answer.

The arc/rounded paths are **not** a copy of the line fix. They dither, and a
dither cell is defined in LOGICAL pixels on purpose (`ios/README.md:357-361`) —
so a device-resolution arc has to decide what a dither cell means before it can
be written. That is a ruling, not a refactor. **That is all that remains of
B-027.**

Nothing here is visible on an X3 or an X4: `RENDER_SCALE` is 1 on device and the
whole path preprocesses away.


### [B-019] `clockFormat` is a visible setting that nothing reads
**severity: medium · scope: lying control · found 2026-08-06 · filed 2026-08-07** · FIXED 2026-08-07


`src/SettingsList.h:463-464` registers a 24H/12H enum bound to
`CrossPointSettings::clockFormat`, under `StrId::STR_CAT_SYSTEM` — the only
category the device UI renders. `src/CrossPointSettings.h:194` declares the
field. Those three lines are the *complete* set of references in `src/` and
`lib/`: nothing consumes it. The owner can toggle a live row that changes
nothing, which is the same defect class as B-001, rated high.

**Close by:** implementing it in the clock rendering paths, or hiding the row.
Do **not** silently delete the field — it round-trips through `toJson`/`fromJson`
and the web settings API, so dropping it would strip the key from `settings.json`
on every card already in the field.


**Fixed — by giving it a reader, not by hiding the row.** Checking upstream
first is what decided it: upstream reads `clockFormat` in `ClockOffsetActivity`,
`ClockSyncActivity` and a whole `StatusBarSettingsActivity`, via
`halClock.formatTime(..., clock12h)`. This fork still HAS that function, with the
`use12Hour` parameter, and had zero callers.

The cause was a capability this fork dropped, not a setting it never wired.
Upstream's offset screen carries a live wall-clock preview — its comment says
*"so users can verify against a watch"* — and this fork's rewrite of that screen
as a timezone list left the preview behind. That is what stranded `clockFormat`.

Restored on `ClockOffsetActivity`, tracking the HIGHLIGHTED row rather than the
saved setting, which is what makes it a preview: you can see what a timezone
would give you before selecting it. Verified on the panel — UTC-5 shows
"Current time: 8:40 PM", moving two rows to UTC+0 shows "1:41 AM", and both
render 12-hour with the setting on 12-hour. The line is reserved only when
`halClock.isAvailable()`, so devices without an RTC lose no list space.

The field was never touched, per the close condition: it round-trips through
`toJson`/`fromJson` and the web settings API, so deleting it would have stripped
the key from `settings.json` on cards already in the field.

### [B-020] `BleHidHost` says the hang is unexplained twenty lines above naming its cause
**severity: low · scope: stale comment · found 2026-08-06 · verified 2026-08-07** · FIXED 2026-08-07


`src/notes/BleHidHost.cpp:687-695` describes the `nimble_port_init()` hang as
nondeterministic, correlates it with free heap, and closes *"This is a spike
workaround, NOT a fix — the nondeterminism itself is unexplained and is the
biggest open risk in this work."*

`:708-716` in the same function then says *"THE cause of the 'nondeterministic'
hang"* — `HalPowerManager` drops the CPU to 10 MHz after 3 s idle and the BT
controller cannot start its radio at that clock — explicitly retracts the heap
theory, and `:717` takes `HalPowerManager::Lock powerLock` to hold full clock
across init. That is the fix, and it is in place.

Only the first comment is wrong. An audit read it as a live unfixed hang, which
is what a stale comment costs.

**Close by:** rewrite `:687-695` to describe the watchdog as belt-and-braces
behind the clock lock, and drop the "unexplained" claim.


**Fixed.** The comment at `:687-695` now says the cause is known and points at
the `HalPowerManager::Lock` twenty lines below that fixes it, and describes the
watchdog as belt-and-braces rather than as the workaround. The heap-range
correlation it used to assert is gone — the file's own later comment had already
retracted it.

### [B-021] A null check that cannot fire, in the tightest-memory path
**severity: medium · scope: graceful degradation · found 2026-08-06 · verified 2026-08-07** · FIXED 2026-08-07


`lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp:759-762` does
`self->currentPage.reset(new Page())` and then tests `if (!self->currentPage)`,
logging "Failed to create new page". The build sets `-fno-exceptions`
(`platformio.ini:65`), so a failed `new` aborts the process — the pointer is
never null and the branch is dead. The log line has never been emitted and
cannot be.

This is the clearest instance because the comment shows the misunderstanding
outright, but bare `new` is used broadly in the layout path, which is exactly
where a 380 KB device runs out.

**Close by:** either route these through a nothrow allocator and handle null, or
delete the dead guards and state plainly that OOM aborts by design. The current
code claims a degradation path it does not have, which is worse than either.


**Fixed.** Six sites, two shapes. `ChapterHtmlSlimParser.cpp:759` and `:766`
were true dead guards — `new Page()` with an `if (!currentPage)` that cannot
fire — and three other `Page()` allocations in the same file already used
`new (std::nothrow)`, so this was an inconsistency inside one file rather than a
missing idea. `Epub.cpp:448`/`:560` and the two image converters had no null
check at all: they dereferenced the result immediately, so a failed `new` aborted
before anything could look. All six now use `new (std::nothrow)` via the
`makeUniqueNoThrow` idiom already in `lib/Memory/Memory.h`, with a real check and
the failure path that already existed (`return false` / `return nullptr`).

Deliberately not a sweep of every bare `new` in the tree. These are the ones
where a caller already had somewhere sensible to fail to.

### [B-026] Browse Files Back exits to Home instead of the file listing; Manage Files double-navigates on Back
**severity: medium · scope: navigation · reported 2026-08-11 · FIXED 2026-08-11**

Two related navigation regressions, fixed together.

**Browse Files (FileBrowserActivity) — Back exits to Home.**
Opening a file from Browse Files called `onSelectBook()` → `activityManager.goToReader()` → `replaceActivity(ReaderActivity)`. `replaceActivity` destroys FileBrowserActivity and clears the stack, so when the reader's short-press Back called the `goHome` callback (or `activityManager.popActivity()` on an empty stack), the user was sent Home rather than back to the listing. Selection at the opened file was also lost.

**Manage Files (FileManagerActivity) — Back from a viewed file navigates twice.**
B-018 filed this symptom (2026-08-07). It was fixed by `backPressSeen`, then that guard was removed by commit `ce652c05` ("centralize child-exit input swallow"), which assumed `swallowUntilIdle()` would cover the release. It does — except that `FileManagerActivity::loop()` called `wasReleased(Back)` twice in one frame. The first call (in the `lockLongPressBack &&` guard) consumed the swallow latch and returned false; the second call (the short-press handler) saw the latch already cleared and returned the real edge value — true, because Back had just been released. At the SD root that second `wasReleased` arm calls `onGoHome()`, so the user saw: TextViewerActivity disappears, then Manage Files flashes, then Home.

**How fixed.**
- `FileBrowserActivity`: opening a file now uses `activityManager.pushActivity(new ReaderActivity(...))` instead of `onSelectBook()`, so FileBrowserActivity stays on the stack. Selection (`selectorIndex`) was already pointing at the opened file, so it is restored automatically on pop.
- `ReaderActivity`: all four sub-reader dispatches (`onGoToEpubReader`, `onGoToXtcReader`, `onGoToTxtReader`, `onGoToBmpViewer`) now call `activityManager.replaceCurrentActivity()` instead of `replaceActivity()`, so the stack entry for FileBrowserActivity survives the ReaderActivity → concrete-reader swap.
- `handleBackNavigation` (ReaderUtils.h): the short-press default now calls `activityManager.popActivity()` instead of the `goHome` callback. `popActivity` on an empty stack already calls `goHome()`, so the path from Home or any non-Browser launcher is unchanged.
- `FileManagerActivity`: the `lockLongPressBack` clear uses `!isPressed(Back)` (level read) instead of `wasReleased(Back)` (edge read). The level read has no swallow-clearing side effect, so the short-press handler sees the swallow still active and correctly returns false.
- New `ActivityManager::replaceCurrentActivity()` method added (replaces current without clearing the stack). Covered by `ActivityInput.ReplaceCurrentActivityPreservesStack`.

**End-of-book back is unchanged.** EpubReaderActivity still calls `onGoHome()` at the end of a book, which uses `replaceActivity(HomeActivity)` and clears the stack. A user who finishes a book lands on Home, not Browse Files; that is the intended behavior.

**Audit note.** RecentBooksActivity also uses `onSelectBook()` (replaceActivity). Books opened from Recents replace the stack intentionally — the owner never asked for Recents to be a browse-and-return context — so no change was made there.

### [B-025] The editor caret barely moves when you type a space
**severity: medium · scope: text entry · reported 2026-08-07 · FIXED 2026-08-07**

Reported as: cursor does not move on space in the text editor.

Measured, because the first diagnosis was wrong. `getTextWidth` does not *drop*
a trailing space, as the comment at
`src/activities/util/NoteEditorActivity.cpp:25-28` and the first version of this
entry both said — it under-counts it. With LibreFranklin 12: `"ab"` is 28,
`"ab "` is 29, `"ab  "` is 34. An interior space advances 5px; a trailing one
contributes 1.

So the caret did move on space — by a single pixel, then jumped the remaining
4 when the next visible character arrived. That is indistinguishable from "space
does nothing" at reading distance, and the report was exactly right.

`advanceOf()` (`:29`) exists to work around this and every drawn span goes
through it (`:509`). The caret at `:519` was the one measurement that called
`getTextWidth` directly.

**Fixed** by measuring the caret through `advanceOf()` like everything else.
Two tests in `test/renderer_bounds/` pin it: that the raw call under-counts a
trailing space, and that the sentinel recovers exactly the interior-space
advance. The second is the one that matters — it asserts the caret lands where
the next glyph will actually be drawn — and it is written so that if a future
font change makes `getTextWidth` count trailing spaces properly, the first test
fails and the workaround gets retired deliberately rather than by accident.

### [B-024] Unbounded allocations from untrusted cache and zip data
**severity: high · scope: memory safety · found 2026-08-06 · verified 2026-08-07** · FIXED 2026-08-07


Three sites size an allocation from a number read straight out of a file, with
no bound:

- `lib/Serialization/Serialization.h:46-51` — `readPod(file, len)` return value
  is discarded, then `s.resize(len)`. On a short read `len` is uninitialized.
- `src/activities/reader/TxtReaderActivity.cpp:511-516` — `readPod(f, numPages)`
  then `pageOffsets.reserve(numPages)`, unvalidated.
- `lib/ZipFile/ZipFile.cpp:389-392` — `inflatedDataSize = fileStat.uncompressedSize`
  uncapped, and `:449` writes `data[inflatedDataSize] = '\0'`; at `0xFFFFFFFF`
  the `+1` wraps to zero and the terminator lands wild.

On a 380 KB device any of these aborts the process. The input is a `.crosspoint`
cache or a zip header — both attacker-influenced in the sense that matters here,
which is a corrupted file on an SD card the owner did not author.

`lib/Xtc/Xtc/XtcParser.cpp` had the same shape and is already fixed: `:302-308`
clamps `maxOffset` to the file bounds and `:316` derives the chapter count from
the bounded remainder. That is the pattern to copy.

**Close by:** bound each length against the actual bytes remaining before
allocating, as XtcParser now does.


**Fixed.** All three sites bound the length against the bytes actually left:
`readString` (both overloads) checks the remaining stream/file and yields an
empty string, which every caller already handles because these are cache reads
and an empty field fails the cache's own validation. `numPages` is checked
against `numPages * sizeof(uint32_t) <= bytes remaining`. `ZipFile` caps a member
at 16 MB, which also closes the `0xFFFFFFFF + 1` wrap that put the terminator
write at `data[0xFFFFFFFF]`. `len` is now initialized too — `readPod` returns
void, so a short read left it holding stack garbage. Five tests in
`test/untrusted_input/`, three of which fail against the old code.

### [B-023] Two out-of-bounds reads on untrusted image data
**severity: high · scope: memory safety · found 2026-08-06 · verified 2026-08-07** · FIXED 2026-08-07


**XTC plane indexing.** `lib/Xtc/Xtc.cpp:203` sizes each plane
`(width * height + 7) / 8`, but `:206` computes `colBytes = (height + 7) / 8` and
`:224-226` indexes `plane1[colIndex * colBytes + byteInCol]`. When `height % 8`
is non-zero, `colBytes * width` exceeds `planeSize` and the last columns read
past the buffer. The thumbnail path guards this; the main render path does not.

**PNG bit depth.** `lib/PngToBmpConverter/PngToBmpConverter.cpp:339` and `:363`
both compute `const int ppb = 8 / ctx.bitDepth` with no prior validation. A
`bitDepth` of 0 divides by zero; 3, 5, 6 and 7 give a wrong packing and read
crooked. The sibling decoder already has the guard —
`lib/Epub/Epub/converters/PngToFramebufferConverter.cpp:113` defines
`isSupportedBitDepth` and `:396` calls it — so this is a missing call, not a
missing idea.

Both are reachable from opening a book or loading a cover.

**Close by:** bounds-check the XTC offset against `planeSize` at `:225`; call the
existing `isSupportedBitDepth` before `:339`.


**Fixed.** The XTC cover path now carries the same `byteOffset >= planeSize`
guard the thumbnail path in the same file already had — the two disagreed, which
is why only one of them was safe. The PNG bit depth is validated before anything
divides by it, using `pngbitdepth::isValid`, extracted to
`lib/PngToBmpConverter/PngBitDepth.h` so the rule is testable without a HalFile
or a real PNG. It is the same rule `isSupportedBitDepth` has always applied on
the in-book path. Seven tests in `test/untrusted_input/`, including three that
pin the plane arithmetic so the guard is not later removed as redundant by
someone who checks only the height-divisible-by-8 case.

### [B-022] Paging mutates `lines` outside the render lock while the render task walks it
**severity: medium · scope: threading · found 2026-08-06 · verified 2026-08-07** · FIXED 2026-08-07


`ActivityManager::loop()` deliberately runs without the render lock — its own
comment at `src/activities/ActivityManager.cpp:77` reads *"do not hold a lock
here, the loop() method must be responsible for acquire one if needed"*. That
makes the lock opt-in per activity, and `TextViewerActivity` does not opt in:
`loop()` (`:248`) reaches `pageForward()`/`pageBack()` (`:231`, `:239`) →
`layoutCurrentPage()` (`:209`) → `layoutPage()`, which calls `lines.clear()` at
`:107` and refills it. `render(RenderLock&&)` (`:348`) reads `lines[i].c_str()`
at `:363` on the render task.

So holding Down to page quickly can free the strings the render task is reading.
`ClaudeChatActivity` has the same shape around `answer` — cleared in `loop()` and
in the BLE drain, with the lock taken only afterwards.

The window is narrow, which is why this has not obviously bitten yet. It is still
a use-after-free.

**Close by:** take a `RenderLock` across the mutation, or defer relayout until
the render guard is held. Audit the other activities for the same pattern rather
than fixing only these two.


**Fixed.** `TextViewerActivity::layoutPage` now lays out into a local vector and
publishes it with `lines.swap()` under a `RenderLock`, so the lock is held for an
O(1) swap rather than across the file reads and text measurement. The committer
is RAII because the function returns from five places. `ClaudeChatActivity`'s two
unguarded clears — Back from an answer, and typing over an answer — take the lock
the same way the relayout path 150 lines above already did; the rule was written
down in that file and these two sites had simply been missed.

Not covered by a test: this is a race between two FreeRTOS tasks, and a
deterministic host test for it would be testing the scheduler rather than the
fix. Verified by reading every mutation path against `render()`.

### [B-018] One Back tap could be consumed twice, landing you on Home
**severity: medium · scope: navigation · FIXED 2026-08-07 · `416e7f42`; re-introduced by `ce652c05` (2026-08-08); re-fixed 2026-08-11 — see B-026**

Reported as: viewing an `.md` in Manage Files kicks you to Home instead of back
to the listing.

`TextViewerActivity` exits on the Back **release**. `FileManagerActivity` also
acts on the Back release, and at the SD root that arm calls `onGoHome()`. So one
physical tap could be read by both: the viewer dismisses on it, and the screen
underneath treats the same edge as a fresh Back. It is the invariant
`test/activity_input/ActivityInputTest.cpp` already pins for the
FontSelection/EpubReader pair, seen from the parent's side.

`viewFile`'s handler looked like it covered this but could not: it arms
`lockLongPressBack` from `isPressed(Back)`, and a child that exits on the
release has already let the button go, so the lock never armed.

Fixed by gating the release on `backPressSeen`, mirroring the `confirmPressSeen`
directly above it — a release with no matching press belongs to whatever ran
before this screen.

**A rejected first attempt is worth recording.** Arming `lockNextBackRelease`
unconditionally in the handler also stops the double-consume, but swallows the
next release whether or not it is genuine: measured, view → Back → Back left the
listing on screen where the second tap should have reached Home. Gating on the
press costs nothing.

**NOT REPRODUCED on the desktop simulator** — tested at the root and one folder
deep; both return correctly, because the SDL path clears the edge between
frames. The leak depends on input timing, which differs on the iOS pad and the
device. This is a fix to a mechanism that demonstrably exists in the code, not
to an observed desktop failure, so whether it resolves the reported symptom is
unconfirmed until it runs where it was seen.

**Audit done alongside it.** All 28 activities that handle Back were mapped for
press-vs-release. The pairing rule is: a child that finishes on the PRESS,
launched from a parent that acts on the RELEASE, leaves the release for the
parent. `FileManagerActivity` → rename/normalize → `DaisyEntry`/`KeyboardEntry`
is that shape, but it IS guarded — those children exit on the press, so Back is
still held when the handler runs and `lockLongPressBack` arms correctly. The
view path was the one where the guard could not fire.

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

### [B-005] The two SD cards hold different bytes under the same bin filename
**severity: low · scope: device provisioning · FIXED 2026-08-07**

Both cards were mounted together and written in one `cpcards` pass, so they now
carry a single identically-named, identically-hashed bin and nothing else:

```
CARD-X3      20260807T0709Z-crosspoint-e194ab7b.bin
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
on CARD-X3. Same size, same `1.5.0-BNY-rc+880ba0f9` version stamp;
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
version-override section, or removing the sysenv interpolation in favor of a
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
`20 iOS exclusions all resolve`. Device-side behavior of the network build is
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
### [B-029] A release built from a fresh clone silently drops the commercial editor faces — FOUND 2026-08-18
**severity: high · scope: build / release · found 2026-08-18**

Building `gh_release` in a fresh `git worktree` produced a binary **430,674
bytes smaller** than the same commit built in the working checkout. The
difference is entirely `nittitypewriter_*` and `pragmatapro_*` glyph tables:
those headers are **gitignored** (`.gitignore:35`, commercial faces, correctly
not committed), and `convert-builtin-fonts.sh` guards their absence
(`NITTI_MISSING`), so the build **succeeds without them and says nothing**.

The result would have been a published firmware that quietly removes two editor
typefaces from the device. Caught only because the flash figure did not match
the working tree's — 68.8% against the expected 75.4%.

**Do not publish a release from a clean clone or worktree** unless the untracked
headers are copied in first:

```
cp lib/EpdFont/builtinFonts/nitti*.h lib/EpdFont/builtinFonts/pragmatapro*.h <worktree>/lib/EpdFont/builtinFonts/
```

Verify before publishing: `nm firmware.elf | grep -c "nittitypewriter\|pragmatapro"`
should report 100, and the flash figure should match a working-tree build.

**FIXED 2026-08-18 — the build refuses instead of shrinking.**
`scripts/check_editor_faces.py` runs as a `pre:` script and checks the sixteen
1x headers the two commercial families need. A **release** environment
(`gh_release`, `gh_release_rc`) FAILS; every other environment prints the same
report and carries on, because a clone with no licensed TTFs building fine is
the behavior `__has_include` exists to provide and has to keep working.
`CROSSPOINT_ALLOW_MISSING_EDITOR_FACES=1` downgrades the failure for a
deliberate no-faces release, and says so in the output.

It calls out the PARTIAL case separately, which is the one that actually bit
before: `main.cpp` gates on the largest size, so a tree regenerated before 14 pt
existed drops or breaks a face whose siblings are sitting right there.

Registered in **both** `[base]` and `[env:simulator]` `extra_scripts` —
`[env:simulator]` does not `extends = base`, so a single entry would have missed
the env where the stale-tree break was first seen.

Verified two ways rather than by reading it: eight logic cases (present, empty
and partial trees against release, dev and override) all pass, and then for
real — moving `pragmatapro_14_regular.h` aside made `pio run -e gh_release`
**FAIL in 5 seconds, before compiling anything**, and the header was restored
afterwards. With the faces present, `gh_release` and `simulator` both build
unchanged.

