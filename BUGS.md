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

### [B-045] A crafted card font reads ~16 KB past a 1-byte glyph buffer (heap overflow)
**severity: high (memory safety, remote-reachable) · scope: SD font loading + glyph decode · found 2026-09-04 by a crafted-input hunt (ASan), reproduced and FIXED the same day**

A `.cpfont` on the card is untrusted input: a Wi-Fi peer can PUT one through
WebDAV (`crosspoint-simulator/docs/network-surface-hunt-2026-09-04.md`). The
loader validated the header, the style TOC and the interval table
exhaustively but trusted every glyph's `width`/`height`/`dataLength`, and the
renderer's 2-bit decode (`GfxRenderer.cpp`, `bitmap[pixelPosition >> 2]` for
`pixelPosition` in `[0, width*height)`) reads `ceil(width*height/4)` bytes
without consulting `dataLength`. A 93-byte font whose single glyph declares
255x255 in a 1-byte bitmap made `onGlyphMiss` allocate 1 byte and the decode
read 16257 bytes past it -- an unmapped-page abort on device, an ASan
heap-buffer-overflow on the host. The prewarm/mini path
(`SdCardFont.cpp`, sizes `miniBitmap` to the sum of `dataLength`) has the
same shape. The only geometry-vs-data check in the tree was build-time
(`scripts/verify_compression.py`), which never sees a card-side file.

**FIXED 2026-09-04.** `glyphGeometryFitsData()` compares `dataLength` against
the bytes `width*height` needs at the font's bit depth, at BOTH load sites:
`onGlyphMiss` refuses the glyph (returns notdef), the prewarm loop blanks it
(`width=height=dataLength=0`, so the decode is a no-op). Reproduced under
ASan with the reviewer's harness before the fix and confirmed refused after
(`Overflow: U+0041 ... declares 255x255 px in 1 bytes -- refused`, `getGlyph`
null, no ASan report). Pinned by `test/malformed_font/` (2 cases: the crafted
glyph refused, a well-proportioned one still loads); the assertion
discriminates against the pre-fix tree, which returned a non-null glyph
carrying the bad geometry. ctest 627/627, X3 canary, pio check PASSED.


### [B-044] WebSocket `START` accepts an absurd size and writes until the peer disconnects
**severity: low (latent) · scope: `src/network/CrossPointWebServer.cpp` WS upload · found 2026-09-04 by the simulator's network-surface hunt (`crosspoint-simulator/docs/network-surface-hunt-2026-09-04.md`, finding 10)**

`START:name:<size>:/books` parses the size with `toInt()`, which saturates at
`LONG_MAX`, so `START:hugesize.txt:99999999999999999999999:/books` answers
`READY` and the upload can never complete: the handler writes every BIN
frame to the card until the peer disconnects, at which point the partial is
removed. No data loss, no crash — a peer can fill the card for as long as it
stays connected, which the 256 MB overflow guard on a single frame does not
prevent across frames. Fix shape: refuse a size above the card's free space
(or a fixed ceiling) at `START`. Found alongside the case-only MOVE data
loss, which WAS fixed the same day (`handleMove`, `strcasecmp` → rename
through a temporary name).


**Relocated here 2026-08-30, content otherwise unchanged except for heading
level.** `[B-041]` and `[B-040]` used to sit far down this file, past the point
where `scripts/tracker-check.sh` stops counting open items — its `open_ids()`
awk one-liner starts counting at `## OPEN` and stops for good at the FIRST
subsequent `## `-level line, and several entries in this file use
`## Follow-up ...` / `## Closed ...` as in-entry narrative sub-headings at that
same heading level (B-033's own "## Closed 2026-08-28..." follow-up, further
down, was the first one and silently ended the scanned range right after the
four items that used to look like the whole open list). Both B-041 and B-040
are genuinely unresolved (B-041's `Compile Release` job is still red; B-040 is
mitigated but device-unconfirmed), so the script's count was wrong by two.

Fixed two ways, together: **moved** both entries up here, inside the region the
script actually scans, and **demoted** the four `##`-level narrative
sub-headings inside B-041 and B-040 themselves (its own "Follow-up" and "What
was changed" continuations) to `#### `, since leaving those at `##` would have
re-broken the scan the moment it reached them. `scripts/tracker-check.sh`
itself was not touched (`scripts/` is not a file this pass may edit) — its
open-counting is still a naive single-boundary scan, so ANY future entry that
keeps a `##`-level narrative heading anywhere between here and the true end of
the open list will silently shrink the count again. Whoever owns
`scripts/tracker-check.sh` should consider making it heading-level-proof (e.g.
scan by `^### \[` markers directly rather than bracketing by `##`), since the
long-standing convention in this file's older entries — B-033's "## Closed",
"## FIXED 2026-08-28", and others further down, all left as they were rather
than demoted, since they sit safely inside what is genuinely archival material
now — makes a recurrence likely.

### [B-041] The original ask — get Actions running — is DONE. CI now runs, and every workflow that ran on 2026-08-29 is RED — FOUND 2026-08-28, RECONFIRMED AND REWRITTEN 2026-08-29, NARROWED 2026-08-30, GREEN-ABLE 2026-08-31, RED AGAIN 2026-09-01→03 on three jobs, FIXED and GREEN ON GITHUB 2026-09-04 (run 33847931384)
**severity: medium (no automated verification is currently trustworthy) · scope: build / release · found 2026-08-28, CI enabled and red 2026-08-29, green-able 2026-08-31, red again on every push 2026-09-01→03, green on GitHub 2026-09-04**

**The premise this entry used to carry — "CI has never run on this fork, every
workflow reports zero runs" — is FALSE as of 2026-08-29 and must not be quoted
as current.** Actions were enabled sometime between the 2026-08-28 finding and
today. Confirmed live:

```
gh api 'repos/natebunnyfield/crosspoint-reader/actions/runs?per_page=20' \
  --jq '.workflow_runs[] | "\(.id) \(.name) \(.status) \(.conclusion) \(.created_at)"'

33235711071  CI (build)                  completed  failure  2026-08-29T05:14:52Z
33232852213  Compile Release             completed  failure  2026-08-29T04:02:32Z
33232779945  CI (build)                  completed  failure  2026-08-29T04:00:50Z
33232779110  CI (build)                  completed  failure  2026-08-29T04:00:49Z
33232573504  Compile Release Candidate   completed  skipped  2026-08-29T03:55:26Z
30941258592  Graph Update: pip …         completed  success  2026-08-04T19:00:21Z
```

Six runs total, five from today. So the fix for the original bug (enable
Actions) already shipped and needs no further action. **The bug this entry now
tracks is different: every workflow that actually ran today FAILED or was
correctly SKIPPED, and none of the three failures is the same cause.**

**`CI (build)` (`.github/workflows/ci.yml`, triggers on push to `main` and on
PRs) — FAILED, three times.** Its jobs, from run `33235711071`:

| job | result |
|---|---|
| `build` (`pio run -e default -e sticky`) | **success** |
| `clang-format` | failure |
| `cppcheck` | failure |
| `unit-tests` | failure |
| `test-status` ("Test Status", the PR-required gate) | failure |

`test-status` is exactly the "bare `exit 1`" the earlier spot-check described —
confirmed against the checked-in YAML, `.github/workflows/ci.yml`:

```yaml
  test-status:
    name: Test Status
    needs: [build, clang-format, cppcheck, unit-tests]
    if: always()
    runs-on: ubuntu-latest
    steps:
      - name: Fail because needed jobs failed
        if: ${{ contains(needs.*.result, 'failure') || contains(needs.*.result, 'cancelled') }}
        run: exit 1
```

That reading holds, but "something is currently failing" undersold it — three
of the four jobs it gates on are failing, for three unrelated reasons, verified
from each job's own log:

1. **`clang-format` — a real formatting drift**, not a CI defect. The diff shown
   in the log reflows a `std::fprintf` call across two lines in what the log
   context shows is the render-harness code; `bin/clang-format-fix` closes it.
2. **`cppcheck` — 32 pre-existing low-severity findings**, mostly
   `useStlAlgorithm` and `knownConditionTrueFalse` style notes across
   `src/activities/*`, `src/notes/BleHidHost.cpp`, `src/components/**`. The step
   is `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect
   high` — `low` is in that list, so any style note fails the job. Zero HIGH or
   MEDIUM findings; the table it printed was `HIGH 0 / MEDIUM 0 / LOW 32`.
3. **`unit-tests` — never actually runs the tests.** It fails at CMake configure,
   not at `ctest`:
   ```
   CMake Error at sleep_screen/CMakeLists.txt:18 (message):
     ArduinoJson not found under .pio/libdeps/*/ArduinoJson/src (nor the primary
     checkout's).
     Run any PlatformIO build first, e.g.: pio run -e simulator
   ```
   This job never runs `pio run` — only the separate `build` job does, in a
   different container — so `.pio/libdeps` is empty and `test/CMakeLists.txt`
   can't find the vendored ArduinoJson dependency. The 353+ host tests this repo
   otherwise runs clean (see `tests/run_all.sh` in `../crosspoint-simulator` and
   `CLAUDE.md`) are not what failed; the CI job simply never got far enough to
   try them.

**`Compile Release` (`.github/workflows/release.yml`, triggers on tag push,
ran here for tag `1.5.21-BD`) — FAILED, on the seed-font integrity gate, not on
a code or config defect.** The build log:

```
lib/EpdFont/scripts/convert-builtin-fonts.sh   (needs lib/EpdFont/local_fonts/)

REFUSING TO BUILD. This is a release environment, and a release
built without these faces loses PragmataPro and NittiTypewriter
from the device with no error and a smaller binary (B-029).
Override with CROSSPOINT_ALLOW_MISSING_EDITOR_FACES=1 if that is
genuinely what you want.
```

This is the [B-029] gate working as designed: the commercial editor faces
(PragmataPro, NittiTypewriter) are not and should not be checked into the repo,
so a fresh GitHub-hosted runner has no `lib/EpdFont/local_fonts/` and the
release build correctly refuses rather than silently shipping a smaller binary.
Whether that means `release.yml` should carry those fonts as a repo secret /
protected artifact, or should stay hand-run via `scripts/release.sh` (which
already does the same work locally, with the same checks) is a design
decision, not a bug fix — recorded here rather than answered.

**`Compile Release Candidate` (`.github/workflows/release_candidate.yml`) —
correctly SKIPPED.** Its job guard is `if:
startsWith(github.ref_name, 'release/')`; the run in question was on `main`, so
the skip is the workflow behaving exactly as written, not a CI problem.

**Not this entry's job to fix.** A later agent owns the actual repair
(`bin/clang-format-fix`, a decision on the 32 cppcheck low findings, and making
`unit-tests` run `pio run` — or otherwise populate `.pio/libdeps` — before its
CMake configure step). This entry's job was to make the record match what `gh
api` and the workflow YAML actually show, which it now does.

**Next step for whoever picks this up:** run `bin/clang-format-fix` and commit;
decide fix-vs-suppress for the 32 cppcheck low findings (`pio check
--fail-on-defect low ...` is what turns style notes into build failures); and
give the `unit-tests` job in `ci.yml` a `pio run -e simulator` (or equivalent)
step before its `cmake -S test -B build/test` step so `ArduinoJson` is present
under `.pio/libdeps` and the job reaches `ctest` at all. The `Compile Release`
failure needs an owner ruling on where CI gets the commercial fonts from, not a
code fix.

#### Follow-up 2026-08-29: (1) and (3) fixed and proved locally; (2) enumerated; a NEW,
#### separate `unit-tests` blocker found underneath the one this entry already named

Re-confirmed all four problems against the live API and the checked-in YAML
before touching anything, per standing instruction. No push is possible from
this environment, so nothing here is a green CI run — every claim below is a
local reproduction of the exact command the workflow runs, shown going from red
to green (or, for the one that is still red, shown red with the reason).

**1. `clang-format` — fixed, and the earlier description of scope was too
narrow.** The prior pass read the failing job's log tail, which only showed a
`std::fprintf` reflow in the render-harness code, and inferred the drift was
small. Reproducing the job's own command
(`bin/clang-format-fix`, which resolves to `clang-format -style=file -i` over
every tracked `.c/.cpp/.h/.hpp` file except the three excluded trees) against
`clang-format-21.1.8` — the exact version `ci.yml` installs, built locally via
`brew install llvm@21` since Homebrew's default `clang-format` is 22.1.8 and
does sort `#include`s differently between the two majors — found **91 files**
with real drift, not one. All of it is mechanical: trailing-comment alignment,
collapsed blank lines, `using`-declaration alphabetical order, macro
continuation-backslash alignment, and re-wrapped long lines. No logic changed
in any of them (spot-checked several full diffs before applying). Applied with
the same binary and flags the workflow uses; a second pass now reports 0 files
needing changes — verified by running the same 91-file check again post-fix.

**2. `cppcheck` — reproduced exactly, 32 LOW findings, 0 MEDIUM/HIGH, matching
the job's own printed table.** `pio check --fail-on-defect low --fail-on-defect
medium --fail-on-defect high` locally: `[FAILED] ... HIGH 0 / MEDIUM 0 / LOW
32`, 83 s. Not fixed or suppressed, per instruction — enumerated and graded
below. Grouped by rule (`[low:style] [rule]` tag), with a genuine/noise call
made by reading the flagged code, not just the message text:

| rule | count | file:line |
|---|---:|---|
| `useStlAlgorithm` | 12 | `src/FontDisplayNames.h:507`, `src/activities/boot_sleep/CalendarSleepScreen.cpp:143`, `lib/EpdFont/MissingGlyphLedger.h:76` (×3, one per TU that includes it), `src/activities/reader/EpubReaderActivity.cpp:1605`, `src/activities/settings/FontSelectionActivity.cpp:669,706,711`, `src/activities/util/ClaudeChatActivity.cpp:175`, `src/activities/util/DaisyEntryActivity.cpp:124`, `src/activities/util/PrettyView.cpp:129` |
| `knownConditionTrueFalse` | 10 | `src/activities/home/FileBrowserActivity.cpp:41`, `src/activities/reader/EpubReaderActivity.cpp:277` (×2 conditions), `:1558`, `src/activities/settings/ColophonActivity.cpp:216`, `src/activities/util/NoteEditorActivity.cpp:787` (×2 conditions), `src/components/UITheme.cpp:62,63`, `src/notes/BleHidHost.cpp:493` |
| `constParameterReference` | 3 | `src/activities/boot_sleep/CalendarSleepScreen.cpp:348,392`, `src/notes/MarkdownRender.cpp:37` |
| `unusedStructMember` | 2 | `src/activities/reader/EpubReaderActivity.cpp:54` (`ProgressRange::start`), `src/notes/BleHidHost.cpp:170` (`ReportInfo::endHandle`) |
| `constVariable` / `constVariableReference` | 2 | `src/components/themes/BaseTheme.cpp:828,861` |
| `duplicateValueTernary` | 1 | `src/activities/network/WifiSelectionActivity.cpp:547` |
| `variableScope` | 1 | `src/activities/settings/OnlineFirmwareUpdateActivity.cpp:73` |
| `shadowFunction` | 1 | `src/notes/BleHidHost.cpp:336` |

*Noise (12 + 3 + 2 + 1 = 18, `useStlAlgorithm` / `constParameterReference` /
`constVariable(Reference)` / `variableScope`): pure style opinions, zero
behavior implication, cheapest bucket to either fix in bulk or suppress
wholesale.*

*`knownConditionTrueFalse` (10) is NOT one bucket — read individually, it
splits three ways:*
- *Deliberately defensive, cppcheck correctly reading a guard as always-true
  given ONLY this TU's single-threaded view: `EpubReaderActivity.cpp:277`'s
  re-check under `RenderLock` exists because another THREAD can replace
  `section` between the outer peek and the lock (the comment two lines above it
  says so explicitly) — cppcheck cannot model that race, so it flags the
  correct, load-bearing re-check as redundant. Do not "simplify" this one.*
- *Provably true by design and commented as such: `FileBrowserActivity.cpp:41`
  (`showsHiddenEntries()` is a switch that always returns false today,
  deliberately kept as a switch so a future enum case fails to compile silently
  — `FileBrowserActivity.h:31-37`); `UITheme.cpp:62-63` (`getOrientation()` is
  `static constexpr`, and this codebase is portrait-only, so the landscape
  branches are unreachable by design, not by accident); `EpubReaderActivity.cpp:1558`
  (`gpio.readAloudCaptureWanted()` is a hard `return false` in the device HAL —
  `lib/hal/HalGPIO.h:262` — read-aloud capture is a simulator/iOS-only feature by
  architecture, so this is always-true on every device build and that is
  correct).*
- *Genuinely worth a look: `ColophonActivity.cpp:216`'s `span > 0 ? span : 1` —
  the function returns early at line 200 whenever `total <= linesPerPage`, so
  past that point `span = total - linesPerPage` is provably `> 0` and the `: 1`
  fallback is dead; harmless, cheap to delete. `NoteEditorActivity.cpp:787` is
  more interesting: it is a byte-for-byte SECOND copy of the `if (oomFailed ||
  !buf) { ... return; }` block at line 772 — 13 lines of genuinely dead,
  duplicated code, not a defensive re-check (no lock, no thread boundary, no
  comment explaining a second check is needed). Worth removing; not removed
  here, since item scope was "enumerate," not "fix."*

*`unusedStructMember` (2): `ProgressRange::start` (`EpubReaderActivity.cpp:54`)
traces to `getPageProgressRange()` having **zero call sites anywhere in the
tree** (checked repo-wide) — the whole function looks like leftover dead code
from a removed progress-percentage feature; `unusedFunction` is suppressed
repo-wide so only the member finding surfaces. `ReportInfo::endHandle`
(`BleHidHost.cpp:170`) is declared, never assigned, never read — only
mentioned in a comment at line 273 describing a `[valHandle+1, endHandle]`
subscription window that was apparently never wired up. Both look like real,
harmless leftovers.*

*`duplicateValueTernary` (1): `AUTO_CONNECTION_TIMEOUT_MS` and
`CONNECTION_TIMEOUT_MS` (`WifiSelectionActivity.h:99-100`) are both literally
`15000` — two distinctly-named constants for auto-connect vs. manual-connect
timeouts that currently resolve to the same value, making the ternary at
`:547` a no-op either way. The separate names strongly suggest these were
meant to diverge (auto-connect on boot arguably shouldn't wait as long as a
user-initiated connect); this may be the actual, mildly interesting finding in
the whole set. Not changed — a tuning decision, not a mechanical fix.*

*`shadowFunction` (1): `BleHidHost.cpp:336`'s local `uint16_t end = gSvcEnd;`
shadows a function named `end` elsewhere in scope. No call to that function
happens inside the shadowing block, so it is inert today but a legitimate
hygiene flag — a future edit inside that block that tries to call `end(...)`
would silently resolve to the local variable instead of erroring.*

**Owner decision, not made here:** fix the ~18 style items in bulk (safe,
mechanical), triage the 10 `knownConditionTrueFalse` individually per the
three-way split above (2 are load-bearing and must not be "fixed", 2 are
provably-correct-by-design and should probably get an `// cppcheck-suppress`
with the reason inline rather than a code change, 2 are genuinely dead code
worth deleting), decide the `WifiSelectionActivity` timeout question, and
decide whether to loosen `--fail-on-defect low` to `medium` in the meantime so
the gate stops blocking on style while these get worked through. All are
options with counts now, not a diff.

**3. `unit-tests` — the described CONFIGURE failure is fixed and verified from
a clean checkout, but a SECOND, independent BUILD-time failure sits directly
underneath it and was not visible until the first one was cleared.**

Mechanism reconfirmed exactly as described: `test/CMakeLists.txt:94` globs
`.pio/libdeps/*/ArduinoJson/src`, and `test/activity_input/CMakeLists.txt:48-53`
plus `test/progressive_jpeg/CMakeLists.txt:8-19` `FATAL_ERROR` when it's empty
(the latter also needs JPEGDEC PATCHED in place by
`scripts/patch_jpegdec.py`, which only runs as a `pio run` pre-build script —
`pio pkg install` alone would not do it). `ci.yml`'s `unit-tests` job never ran
`pio run` before `cmake -S test -B build/test`, so on a fresh checkout the glob
is empty and configure aborts — reproduced by cloning this repo into a scratch
dir with no `.pio/`:
```
CMake Error at sleep_screen/CMakeLists.txt:18 (message):
  ArduinoJson not found under .pio/libdeps/*/ArduinoJson/src (nor the primary
  checkout's).
```
Fixed in `.github/workflows/ci.yml`: the `unit-tests` job now installs
PlatformIO Core and runs `pio run -e default` (matching
`test/progressive_jpeg/CMakeLists.txt`'s own recommended command, so both
ArduinoJson and a patched JPEGDEC land under `.pio/libdeps` in one step) before
`cmake -S test -B build/test`. Verified end to end in the same clean clone:
`pio run -e default` succeeds (5:22 on a cold toolchain download), then
`cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release` configures
clean with no FATAL_ERROR — the exact failure above is gone.

**But `cmake --build build/test` then fails on ONE target, `ActivityInputTest`
— link error, not a configure or ArduinoJson problem, and pre-existing in the
primary checkout too (not a clone artifact):**
```
Undefined symbols for architecture arm64:
  "PersistableStoreBase::writeDocToFile(char const*, ArduinoJson::V742HB42::JsonDocument const&)"
  "CrossPointSettings::toJson(ArduinoJson::V742HB42::JsonDocument&) const"
    referenced from: PersistableStore<CrossPointSettings>::saveToFile() const in FontSelectionActivity.cpp.o
ld: symbol(s) not found for architecture arm64
```
Traced to `SETTINGS.saveToFile();` at `src/activities/settings/FontSelectionActivity.cpp:406`,
added by `00ae42356` ("feat: switch a font off with a long hold in Reader
Font"), the commit immediately below current HEAD — landed 2026-08-29, the
same day as this follow-up, and not yet flagged anywhere because CI has never
gotten far enough to reach this target before today. `test/activity_input/CMakeLists.txt`'s
own header comment says the exclusion is deliberate: *"CrossPointSettings —
real class and real field defaults, but the .cpp is not linked (JsonSettingsIO
/ ArduinoJson / file I/O)."* The new call needs exactly the two symbols that
exclusion leaves undefined. Every OTHER test target in the suite links and its
tests pass — confirmed with `cmake --build build/test -- -k 0` (keep going past
the first failure) and `ctest --test-dir build/test --output-on-failure -j`:
560/560 runnable tests pass, and the ONE failure is a
`gtest_discover_tests`-generated placeholder, `ActivityInputTest_NOT_BUILT`,
which reports "Unable to find executable" because the real binary never linked.

This is not the workflow bug item (3) named, and not something to fix
unilaterally — the CMakeLists exclusion was a deliberate choice (avoiding real
file I/O in a host test target) that a new feature commit has now outgrown, so
the actual fix is a design call: link `CrossPointSettings.cpp` +
`PersistableStore.cpp` (+ whatever they pull in) into `ActivityInputTest` for
real, or give the target a targeted double for
`PersistableStore<CrossPointSettings>::saveToFile()` the way `CrossPointSettings`
itself is already doubled for I/O reasons, or route the new long-hold-to-deactivate
feature's persistence through a path this test target doesn't touch. **Net: the
`unit-tests` job is fixed for the failure this entry named, but will still be
red today for an unrelated, newer reason — a fully green run needs both.**

**`Compile Release` / `Compile Release Candidate` — reconfirmed, unchanged.**
The B-029 seed-font gate is still refusing correctly (no
`lib/EpdFont/local_fonts/` on a hosted runner); not disabled, not touched. The
three options from the prior pass stand and are restated here rather than
decided: (a) skip that workflow on CI entirely, (b) provide the commercial
fonts to CI via a secret / protected artifact, or (c) accept a permanently red
release workflow and keep releasing via `scripts/release.sh` by hand, which
already runs the same gate locally. `Compile Release Candidate`'s
`startsWith(github.ref_name, 'release/')` guard is confirmed correct by
reading; the skip on a `main` push is the workflow behaving exactly as
written.

#### Follow-up 2026-08-29 (later the same day): `ActivityInputTest`'s link failure closed — the double option, not the real-.cpp option

The decision the prior follow-up left open ("link the real .cpp for real, or
double `saveToFile()`, or reroute the feature's persistence") was made and
implemented: **doubled**, matching the existing `SdCardFontSystem` /
`CrossPointSettings::getReaderFontId()` pattern in this same target rather than
pulling `CrossPointSettings.cpp` / `PersistableStore.cpp` (and their
`JsonSettingsIO` / real SD file I/O) into a suite that is deliberately
hermetic. `FontSelectionActivity` was not dropped from the target either — both
alternatives were considered and rejected, per instruction.

Two out-of-line no-op bodies added to `test/activity_input/HostHarness.cpp`
(next to the existing `CrossPointSettings` double section, `HostHarness.cpp`
~144-156):
```cpp
bool PersistableStoreBase::writeDocToFile(const char*, const JsonDocument&) { return true; }
void CrossPointSettings::toJson(JsonDocument&) const {}
```
This satisfies both symbols the linker named (`PersistableStoreBase::
writeDocToFile`, `CrossPointSettings::toJson`) without linking
`PersistableStore.cpp` or `CrossPointSettings.cpp`. `test/activity_input/
CMakeLists.txt`'s header comment was extended (not rewritten) to say so.

**Checked whether the no-op is honest before writing it, per instruction:**
`find test -type f \( -name '*.cpp' -o -name '*.h' \) -print0 | xargs -0 grep -l
'saveToFile\|writeDocToFile'` across the whole `test/` tree returns only the
two new call sites — **no test anywhere in this suite asserts on persisted
(file or JSON) content**; the only settings-related assertions
(`ActivityInputTest.cpp:612,622,631`) read `SETTINGS.fontSizeSlot` directly,
the in-memory field the input handling wrote, not anything written to disk. So
a silent no-op is the correct double, not a shortcut past a real assertion.

**Reproduced red, then green, exactly as CI runs it:**
- `pio run -e default` — success (92 s).
- `cmake -S test -B build/test` (clean `build/test`) then `cmake --build
  build/test` — **before the fix**: every other target built; `ActivityInputTest`
  failed at link with the exact two undefined symbols quoted in the follow-up
  above. **After the fix**: full `cmake --build build/test` (all targets)
  completed with no errors, `ActivityInputTest` present and linked
  (`build/test/activity_input/ActivityInputTest`, 2,092,592 bytes).
- `ctest --test-dir build/test --output-on-failure`: **100% tests passed out of
  601** (2 disabled by design: `LineBreakQuality.Sweep`,
  `LineBreakQuality.RaggedGateSweep`). All 37 cases that live in the
  `ActivityInputTest` binary (`ActivityInput.*`, `TypedTextEntry.*`,
  `ClaudeChatSend.*`, `ListPaging.*` — ctest test IDs 258-294) are in that
  passing count, including the pre-existing `ActivityInput.
  SideButtonChangesFontSizeUnderTheRenderLock` case that reads
  `SETTINGS.fontSizeSlot`. Nothing newly-running failed.
- `pio run -e simulator` — success (7 s), confirming the firmware (which links
  the *real* `CrossPointSettings.cpp` / `PersistableStore.cpp`, unaffected by
  this test-only double) still builds.

**Not this pass's job, left alone:** the `clang-format` / `cppcheck` /
`Compile Release` items above are unchanged by this pass. `ci.yml` itself was
not touched here — the prior follow-up already added the `pio run -e default`
step ahead of the CMake configure; this pass only closes the build-time gap it
found underneath. **Net: with both follow-ups applied, `unit-tests`
configures, builds every target including `ActivityInputTest`, and 100% of the
601 runnable tests pass locally** — the remaining red CI jobs are
`clang-format` (fixed, needs commit+push), `cppcheck` (32 LOW findings,
enumerated, awaiting a fix/suppress ruling), and `Compile Release` (awaiting a
CI-fonts ruling), none of which this pass touched.

#### Follow-up 2026-08-30: cppcheck cleared, clang-format re-confirmed clean, and the `Compile Release` blocker got an owner ruling — none of it pushed yet

Reconfirmed against the live tree before writing this, per standing instruction.

**cppcheck — the 32 LOW findings the two follow-ups above enumerated are
fixed, not just enumerated.** Commit `225c8e5c6` ("style: clear all 32
cppcheck findings, and close two live questions") landed 2026-08-30. Re-ran
the exact CI command locally: `pio check --fail-on-defect low --fail-on-defect
medium --fail-on-defect high` -> `No defects found`, 31.4 s (previously `HIGH
0 / MEDIUM 0 / LOW 32`). Per the commit message: two of the ten
`knownConditionTrueFalse` findings the prior follow-up flagged as load-bearing
(`EpubReaderActivity.cpp`'s race re-check, `FileBrowserActivity.cpp`'s
intentional always-true switch) took a narrow inline suppression carrying the
reason rather than a restructure — consistent with what the enumeration above
said must NOT be "fixed" by simplifying. The `WifiSelectionActivity`
duplicate-timeout question (flagged above as "the actual, mildly interesting
finding") resolved as already-intentional: `AUTO_CONNECTION_TIMEOUT_MS` was
deliberately unified with `CONNECTION_TIMEOUT_MS` on 2026-08-02 (`1e48ced`),
and the fix only adds a comment recording that rather than un-merging them.
The `NoteEditorActivity.cpp` dead duplicate block (flagged above as "worth
removing") was removed.

**clang-format — still clean, checked with the CI binary, not the machine
default.** `PATH="<llvm@21 prefix>/bin:$PATH" bin/clang-format-fix` against
`clang-format-21.1.8` (the version `ci.yml` installs; this machine's default
`clang-format` resolves to Homebrew's 22.1.8, which the earlier follow-up
already noted sorts `#include`s differently) makes zero changes to the tree —
the 91-file sweep from `129c9566d` (2026-08-29) is not re-dirtied by the
cppcheck fixes that landed after it.

**`unit-tests` — the fix described in the two follow-ups above is committed,
not only proved once locally.** `.github/workflows/ci.yml`'s `unit-tests` job
carries the "Build firmware (populates .pio/libdeps for the CMake test
suite)" step (`pio run -e default`) ahead of the CMake configure, from
`7979677b2`; `test/activity_input/HostHarness.cpp:156-157` carries the two
doubled symbols (`PersistableStoreBase::writeDocToFile`,
`CrossPointSettings::toJson`) from `414135091`. Not re-run end to end in this
pass — the prior follow-up already proved it locally at 601/601 — this pass
only confirms the fix is still in the tree, unreverted.

**Still red, and now the only remaining blocker: `Compile Release`'s
seed-font gate ([B-029]).** Unchanged mechanically — no
`lib/EpdFont/local_fonts/` on a hosted runner, correctly refused. **Owner
ruling 2026-08-30**, resolving the three options the 2026-08-29 follow-up left
open (skip the workflow on CI / provide the fonts via a secret or protected
artifact / stay permanently red and release by hand): the commercial faces
reach CI via a **private repository, fetched with a personal-access token held
as an Actions secret** — not GitHub's own secret storage for the payload
itself. Measured why that distinction matters: `lib/EpdFont/local_fonts/` is
**3.8 MB** (`du -sh lib/EpdFont/local_fonts` on a machine that has the faces),
and a single GitHub Actions secret caps at **48 KB** — roughly **81x** over,
so the faces cannot be a secret directly, only the token that fetches them
from somewhere else can be. This fork is public, which is why the faces
themselves need a private home rather than living in-repo. **Not yet
implemented** — this entry records the ruling and the reasoning behind it, not
the wiring; the private repo, the fetch step in `release.yml`, and the secret
itself are still to be built.

**None of the above three fixes have reached GitHub's own CI yet.** `git log
origin/main..HEAD` shows this checkout 3 commits ahead of `origin/main`
(`225c8e5c6`, `414135091`, `be43e4846`) — unpushed as of this pass. Every claim
above is a local reproduction of the exact command the workflow runs, same as
the two follow-ups before it; a genuinely green `CI (build)` run still needs a
push, and the `Compile Release` job stays red until the private-repo fetch is
built regardless.


#### Resolved 2026-08-31: Compile Release no longer runs on CI

Owner ruling, asked directly against writing the private-repo + token machinery
or leaving the check red: **"don't run Compile Release on CI at all."**

`.github/workflows/release.yml` fired on every tag push and failed every time --
not from a code fault, but because the B-029 seed-font gate correctly refuses a
build with the commercial editor faces absent, and `lib/EpdFont/local_fonts/` is
gitignored and cannot be in a fresh checkout. It is now `workflow_dispatch`
only, matching `release_candidate.yml`, with the whole argument written into the
file so nobody re-adds the trigger and rediscovers the red badge.

The rejected alternative, measured rather than assumed: the fonts are 3.8 MB
across 25 files, about **81x GitHub's 48 KB Actions-secret cap**, so they cannot
be a secret; and this fork is PUBLIC, so an encrypted archive committed here
would be publicly downloadable ciphertext of licensed fonts. A private fonts
repo with only a token as the secret would have worked and was offered.

**What this costs, stated plainly:** no CI coverage of the release path, so a
release-only break is found at release time. Accepted.

With this, every CI check that CAN pass does: cppcheck clean, clang-format
clean, the unit-tests configure fixed and ActivityInputTest linking again at
601/601.

#### Red again 2026-09-01→03, on three jobs — all fixed 2026-09-04

The paragraph above was true for one day. Every push from 2026-09-01 through
2026-09-03 (`gh run list --repo natebunnyfield/crosspoint-reader`, eight runs,
all `failure`; latest `33779976692`) failed three of the four jobs, none of
them the blocker this entry was narrowed to. Nobody looked, because the badge
is not in anyone's path — the same mechanism as the original "zero runs, ever".

| job | what was red | fix |
|---|---|---|
| `unit-tests` (Build step) | `lib/FsHelpers/FsHelpers.cpp:14: error: 'uint8_t' does not name a type` — the file uses `uint8_t` five times and never includes `<cstdint>`. Apple libc++ pulls it in transitively; Ubuntu's libstdc++ does not, which is why every local run reported 601/601 green while CI was red. Blocked THREE test targets (`plain_text_files`, `definition_list`, `table_keep_together`). | `#include <cstdint>` |
| `clang-format` | four files drifted in `5dcd2ba11`, `682514416`, `7125a6d5e`: `FontSelectionActivity.cpp:223` (121-col line), `LibraryUpdateActivity.cpp:1,234` (include order, a needless wrap), `test/activity_input/ActivityInputTest.cpp:52,649`, `test/home_landing/HomeLandingTest.cpp:27-30,60-61`. | formatted; the local binary is clang-format 22 against CI's 21, and the diff matched CI's printout line for line |
| `cppcheck` | three new LOW findings: `BleHidHost.cpp:491` (the loop whose COMMENT already says cppcheck misreads it — the comment was written, the suppression never added), `EpubReaderActivity.cpp:403` (`consumeFontFamilyStep()` is a constant 0 on the device HAL, the `readAloudCaptureWanted()` shape this entry already documents), `BleHidHost.cpp:161` (`ChrInfo::defHandle` written by the aggregate init and read by nothing — a real unused member). | two `// cppcheck-suppress knownConditionTrueFalse`; `defHandle` removed from the struct and its one initializer |

Verified locally 2026-09-04: `pio check --fail-on-defect low --fail-on-defect
medium --fail-on-defect high` PASSED; `ctest` 622/622 in `build/test`
(macOS — the `<cstdint>` fix is the one thing this machine cannot prove, and it
is a one-line include). What is NOT yet true: a green run on GitHub. The next
push is the evidence; if it is red, this entry reopens on that run's log.

The push of those fixes turned up a FOURTH red, hidden behind the
`<cstdint>` one: `ProgressiveJpegTest` failed with `JPEGDEC.h:22: fatal
error: Arduino.h: No such file or directory`. JPEGDEC's host branch keys on
`__MACH__ || __LINUX__ || …`, and GCC defines `__linux__`, never `__LINUX__`
(JPEGDEC's own spelling), so Ubuntu fell through to the Arduino include while
every Mac took the host branch. `e484edb75` defines it for the target on
non-Apple hosts. **Run `33847931384` on that commit: unit-tests, build,
clang-format, cppcheck, Test Status — all green.** The first green run since
Actions were enabled on 2026-08-28.

**The recurring lesson**, since this is the third time this entry has recorded
it: a check nobody watches is a check that goes red silently. `gh run list
--repo natebunnyfield/crosspoint-reader --limit 3` after every push to `main`
is the whole discipline, and it is not automated.

### [B-040] The reader aborts on a 16 KB allocation while building a font's advance table — MITIGATED 2026-08-28, unconfirmed on device
**severity: high (hard crash) · scope: SD font loading · found 2026-08-28 in `/Volumes/BUNNYFIELDS/crash_report.txt`**

Found while reading the card's crash reports for the OTA work, not reported —
so nobody has said how often it happens, and that is the first thing to
establish.

```
CrossPoint version: 1.5.9-BD
Panic reason: abort() was called at PC 0x421c764d on core 0
[336467] [ERR] [SDCF] buildAdvanceTable: failed to allocate codepoint buffer (16384 bytes)
   ... the same line 13 times, ~350 ms apart ...
[276]  [INF] [HW] Using cached device type: X3
```

**What the shape says.** Thirteen consecutive failures to get 16 KB, then an
`abort()`. So this is not one unlucky allocation — the heap was exhausted and
stayed exhausted while the reader retried, which means the retry itself is part
of the story: something asked, failed, and asked again without releasing
whatever had filled the heap.

The last two lines are from the REBOOT (`[276]`, a fresh millis), so the abort
is the end of that boot's log, not a recovery.

**Where to look, in order.** `SdCardFont::buildAdvanceTable` and what holds
memory across its retries; whether the failure path frees the partial table
before the next attempt; and what else was live at the time — 16 KB is not a
large ask, so the interesting question is what had already taken the heap. The
X3 has ~400 KB and the surrounding code is written for that, so a single
runaway consumer is more likely than genuine pressure.

**Not reproduced.** No card state was captured beyond the report, and the log
tail does not say which family or which book was open.

#### What was changed, 2026-08-28

`buildAdvanceTableRange` asked for the WORST CASE on every call: 4096
codepoints is 16 KB, `new[]`-ed and freed per invocation. On a device with
~400 KB the number that matters is not free heap but the largest CONTIGUOUS
block, and a 16 KB request stops being satisfiable long after churn has broken
the heap up — which is exactly the shape of thirteen consecutive failures with
the device otherwise running.

It now starts at **256 codepoints (1 KB)** and quadruples only when a scan
actually fills the buffer, so the common call — a page of text needs a couple of
hundred distinct codepoints — never asks for more than 1 KB. The scan restarts
after a growth rather than resuming, because `collectUniqueCodepoints` dedupes
and a rescan is therefore idempotent; at most two growths separate 256 from the
cap.

**A failed growth is no longer fatal.** The buffer already holds a full set of
codepoints at that point, and `hitCap` already means "layout may be
approximate" — an outcome this function has always been able to return and the
reader has always survived. Only the first 1 KB allocation can still fail the
call outright, and it is the one most likely to succeed.

**This is a mitigation, not a diagnosis.** It removes the largest recurring
contiguous request on the font path, which is the thing most likely to fail on
a fragmented heap and the thing the log actually recorded. It does NOT explain
what had already consumed the heap, and the `abort()` itself came from some
OTHER allocation — a plain `new` failing under `-fno-exceptions` aborts, and
the nothrow site here returns instead. So if the crash recurs, the next step is
to find that allocation, not to shrink this one further.

577 tests pass, desktop canary green. Device-confirm only: nothing host-side
reproduces a fragmented ESP32 heap.

### [B-033] The release binary carries a stale provenance stamp — REOPENED 2026-08-28, FIXED the same day (scripts/stamp_app_desc.py), first-OTA confirm owed
**severity: MEDIUM (raised 2026-08-28 — the descriptor is live, not dead data) · scope: build / release · handed over 2026-08-19 · wrongly closed and reopened the same day**

Reported by the session that cut the 1.5.2-BD release: the binary contains
`1.5.1-BNY-2-g78be6b97f` while `git describe` returns `1.5.1-B2-34-…`. **The
DISPLAYED version is correct** (1.5.2-BD on the boot screen); this is provenance
metadata, so the likeliest visible symptom is the web UI reporting an old build.

What that session established, and it is the useful half:

* it survives `pio run -t clean`, so it is not a stale object;
* plain `grep` cannot find the string in the tree, which points at a
  **committed compressed asset** — most probably the gzipped web UI that
  `scripts/build_html.py` embeds.

Deliberately filed without a mechanism rather than guessed at. **Close by**
finding which committed artifact carries it (start by decompressing the
generated HTML headers and grepping those), then regenerating it — or, if it is
baked into a committed `.gz`, making the generator stamp it at build time so it
cannot go stale again.

## Closed 2026-08-28. Both halves of the lead were wrong.

**The web UI does not carry it.** The suggested first step was to decompress the
generated HTML headers and grep them. Done: `src/network/html/*.generated.h`
are gzip blobs as expected, they decompress cleanly, and **none contains a
version-like string at all**. They are also NOT COMMITTED — `git ls-files` lists
only the `.html` sources and `js/`, so they are regenerated every build and
could not have gone stale even if they had carried it.

**The string is not in the tree.** A full `grep -a` across the working tree,
excluding `.git`, `.pio` and `fs_`, finds `1.5.1-BNY` in exactly one file:
`BUGS.md`, this entry. No committed artifact carries it, compressed or
otherwise.

**And the current build cannot produce that FORMAT.** `scripts/git_branch.py`
emits `{base}-dev-{branch}-{sha}` for the default env and `{base}-rc+{hash}` for
`gh_release_rc`; every other env takes a literal from the ini. The reported
string `1.5.1-BNY-2-g78be6b97f` is `git describe --tags` output — base, commits
since the tag, `g` plus the hash — a shape none of those three paths emits. The
script also caches nothing: `inject_version` recomputes on every invocation and
appends the define, so there is no store for a value to go stale in.

So whatever produced that stamp is not in this tree, and the closest thing to a
mechanism is that the inspected binary predated the current `git_branch.py`.
Closed rather than left open with a disproved lead. If a stale stamp is ever
seen again, the useful first fact is the FORMAT: `-dev-` means the default env,
`-rc+` the RC env, a bare triple a literal in the ini, and `-N-g<sha>` means
something outside this repo's build path generated it.

## That closure was WRONG, and the next build proved it within the hour

I closed this saying the `-N-g<sha>` format was no longer producible. Then a
fresh `gh_release` build, made for B-006, came out containing
**`1.5.1-B2-43-g7211621a3`** — that exact shape, alongside the correct
`1.5.16-BD`. The closing reasoning was right about `git_branch.py` and wrong
about the conclusion: I checked the paths I knew about and treated that as
checking all of them.

**The mechanism, found by following it rather than reasoning about it:**

* `git describe` in this repo today returns `1.5.16-BD-49-g093a4b129` — current.
  So the binary's string is not this build's describe.
* `7211621a3` IS a commit in this repo, just an older one than HEAD. So it is a
  describe of THIS project, taken at some earlier moment and kept.
* Grepping the build tree finds it only in `firmware.bin` and `firmware.elf` —
  no object file in `.pio` carries it, which is why a rebuild never clears it.
* Grepping wider finds the carrier:
  **`~/.platformio/packages/framework-arduinoespressif32-libs/esp32c3/lib/libesp_app_format.a`**.

It is baked into a **prebuilt library in the PlatformIO package cache**, outside
the repository. That accounts for every clue the original report left and that I
mis-read:

| the 2026-08-19 evidence | why |
|---|---|
| survives `pio run -t clean` | clean empties `.pio/build`, not `~/.platformio/packages` |
| `grep` cannot find it in the tree | it is not in the tree |
| the shape is `git describe` | ESP-IDF stamps the app descriptor's version from `git describe` |

The DISPLAYED version was always right because `CROSSPOINT_VERSION` is a
compile-time define this repo controls. What is stale is the **ESP-IDF app
descriptor**, which is what OTA metadata and the web UI report — exactly the
"likeliest visible symptom" the original entry predicted.

## The library member, and what it proves

`esp_app_desc.c.o` inside that archive carries three strings together:

```
1.5.1-B2-43-g7211621a3
crosspoint-reader
01:15:06   Aug 21 2026
```

**This project's own name and an August 21 build timestamp are inside a shared
PlatformIO framework package.** `esp_app_desc.c` is ESP-IDF's; the values in it
are this repo's. So on 2026-08-21 a build compiled that translation unit and the
result was written into `~/.platformio/packages/framework-arduinoespressif32-libs/`,
where it has been linked into every binary since — and would be linked into any
OTHER project built with that package on this machine.

**So `PROJECT_VER` is NOT the fix**, and the previous "close by" was wrong for a
second time. `PROJECT_VER` is consumed when `esp_app_desc.c` is COMPILED, and
that object is never recompiled: it arrives prebuilt. Nothing in this repository
can change what is already baked into it.

**Close by** restoring the framework package so the descriptor is generated
rather than inherited — the package is polluted with a build artifact and should
be reinstalled — and then finding what wrote into `~/.platformio/packages` on
2026-08-21, because a toolchain package that a project build can mutate will do
it again.

### The descriptor is LIVE, so this is not cosmetic

Read straight out of the shipped image rather than inferred. `esp_app_desc_t`
sits at offset `0x20` (24-byte image header + 8-byte segment header) and its
magic checks out as `0xABCD5432`:

```
version     : 1.5.1-B2-43-g7211621a3
project_name: crosspoint-reader
date/time   : Aug 21 2026 01:15:06
idf_ver     : 5.5.2.260206
```

So the stale string is not a leftover sitting unused in a library — it IS the
descriptor the image carries. **Every release built on this machine reports an
August 21 version in its OTA metadata and its web UI build line**, whatever the
boot screen says.

Severity raised from low to **medium** on that. The original entry called the
web UI "the likeliest visible symptom" and hedged it; this confirms it, and adds
OTA metadata alongside. `project_name: crosspoint-reader` in a supposedly
generic ESP-IDF library is also the proof that the object was compiled for this
project and frozen, rather than shipped that way by Espressif.

### Two more facts, and they make the remedy safe rather than risky

Checked before recommending a reinstall, because "restore the toolchain" is a
bad thing to be wrong about:

* **The package is STOCK, not a custom build.** `package.json` reports
  `framework-arduinoespressif32-libs` version **5.5.0+sha.87912cd291**, from
  `espressif/esp32-arduino-lib-builder`. So a reinstall re-fetches a pinned,
  published artifact — it does not discard a bespoke toolchain someone built on
  purpose. That was the risk worth ruling out: if these libs HAD been custom-built
  for this firmware's sdkconfig, replacing them would have been destructive.
* **Only esp32c3 is affected** — this project's target. `strings` finds
  `crosspoint-reader` in the esp32c3 `libesp_app_format.a` and **zero** times in
  the esp32 and esp32s3 copies of the same library.

Every `.a` in `esp32c3/lib/` shares one mtime, `Aug 21 01:16`, one minute after
the descriptor's own compile stamp of `01:15:06`. So the whole esp32c3 lib set
was written at once, by something that had just compiled against this project.
**What did it is still unknown** — nothing in this repo references
`~/.platformio/packages` except one comment in `tools/stack_budget/`, and the
other chips are untouched, so it was not a broad package operation.

### A repo-side fix EXISTS: the symbol is weak

`nm --defined-only` on the archive reports:

```
esp_app_desc.c.o:
00000000 V esp_app_desc
```

`V` is a **weak object symbol**. ESP-IDF marks it weak precisely so an
application can override it: a strong definition in this project would win at
link time with no duplicate-symbol error, and the archive member would simply
never be pulled. So the descriptor can be corrected **without touching the
toolchain at all** — which makes this fixable in-repo, and reinstalling the
package the fallback rather than the remedy.

**Why it is not written yet, and this is a real blocker rather than caution.**
Overriding means supplying the WHOLE `esp_app_desc_t`, not just the version
field, and the struct's tail is not all inert padding on current IDF:
`min_efuse_blk_rev_full`, `max_efuse_blk_rev_full` and `mmu_page_size` are read
by the BOOTLOADER. The values currently in the image are correct because
Espressif's build computed them; hand-writing a replacement means reproducing
them, and guessing wrong there does not produce a wrong version string — it
produces an image the bootloader may refuse.

That is the one failure this repository's own OTA design exists to prevent (see
the five-step no-brick chain in `OnlineFirmwareUpdateActivity.h`), and it is not
verifiable off-device: reading the fields back proves the struct is well-formed,
not that the bootloader accepts it.

## FIXED 2026-08-28 — and not by the override, because that would have been the
## same bug one field over

The weak-symbol override was the obvious fix and it is the wrong one.
Overriding means supplying the WHOLE struct, so the bootloader-read fields
would have to be hardcoded here — and the next framework bump would silently
invalidate them. That is B-033 again, one field across: a value frozen in a
place nobody looks.

**`scripts/stamp_app_desc.py` patches thirty-two bytes after the link instead.**
It finds the descriptor at `0x20`, verifies the magic is `0xABCD5432`, writes
the `CROSSPOINT_VERSION` this build is already using — read from the build
flags, so it cannot disagree with the boot screen — and recomputes the appended
SHA256, because the descriptor sits inside a hashed segment and a patched image
with a stale hash is one the bootloader rejects. Every field Espressif's build
computed is left exactly as it was. If the magic is ever absent it does
nothing and says so, rather than corrupting a byte range.

Verified on a real `gh_release` build:

```
stamp_app_desc: app descriptor version '1.5.1-B2-43-g7211621a3' -> '1.5.16-BD', sha256 recomputed

version        : 1.5.16-BD          <- was the August string
project_name   : crosspoint-reader
idf_ver        : 5.5.2.260206
secure_version : 0   (was 0)        <- bootloader-read, unchanged
min_efuse_blk  : 0   (was 0)
max_efuse_blk  : 199 (was 199)
mmu_page_size  : 16  (was 16)
sha256 VALID   : True
magic byte     : 0xE9
```

**Device-confirm still owed.** Byte-level verification proves the image is
well-formed and self-consistent; it cannot prove the bootloader accepts it. The
no-brick chain applies as always -- a rejected image fails
`esp_ota_end()` verification or reverts on the next boot -- but the first OTA
onto real hardware is the confirmation.

### CORRECTION 2026-08-28: "polluted" was too strong, and I had said it loudly

Having called this a polluted toolchain and recommended a reinstall, I looked at
how `crosspoint-reader` appears in the OTHER esp32c3 libraries. It is a BUILD
PATH:

```
/Users/natebunnyfield/src/wt-ship/crosspoint-reader     (libfatfs.a, libespressif__mdns.a,
                                                         libesp_bootloader_format.a, ...)
```

That is ordinary debug/assert path leakage from compilation, present in any
locally built object and harmless. So the accurate description is not "a build
leaked into a shared package" — it is that **the esp32c3 lib set was built
locally on 2026-08-21 from the `wt-ship` worktree and cached**, which is what
this toolchain does. `esp_app_desc.c.o` froze that moment's version along with
everything else, and is the one object where the frozen value is not inert.

**So there is nothing to clean up, and the reinstall is withdrawn as a
recommendation.** Rebuilding 260 MB of esp32c3 libraries to correct one string
that `stamp_app_desc.py` already corrects would be effort spent on the wrong
layer. Another project on this machine linking these libs would inherit the
stale descriptor, which matters only if it ships OTA metadata and does not stamp
its own — worth knowing, not worth a rebuild.

The earlier alarm in this entry is left in place rather than deleted, because
the reasoning that produced it is the useful part: a project's own name inside a
framework library DOES look like contamination until you check what form the
string takes.

**Reinstalling the package remains the zero-risk alternative.** It rewrites a
shared toolchain outside this repository and forces a large re-download; that is
the owner's machine and his call. And this entry has now been closed wrongly
once and given a wrong remedy twice — each time by reasoning one step past the
evidence — so the third answer is the one that gets checked before it is acted
on.

**What is safe to rely on meanwhile:** the DISPLAYED version is unaffected.
`CROSSPOINT_VERSION` is a compile-time define this repo owns, it reads
`1.5.16-BD` correctly, and OTA's `isNewer()` parses the numeric triple from it.
Only the app descriptor — OTA metadata and the web UI's build line — is stale.

### [B-034] Fork and upstream will collide in the tag namespace at 1.5.3 — CLOSED 2026-08-28, the collision never happened and the reason is now written down
**severity: low · scope: release · found 2026-08-19 · closed 2026-08-28**

Tags `1.5.3`, `1.5.4`, `1.5.5` and `1.5.6` exist locally with no releases on this
fork — they are upstream's, arriving through the `upstream` remote. The fork is
at **1.5.2** and numbers upward, so its next minor lands on a tag that already
means something else.

Nothing is broken yet, and that is exactly why it is worth deciding now rather
than during a release: `git tag 1.5.3` will simply fail, in the middle of a
publish, on a machine where the fetch happened to have run.

**Close by** choosing a namespace and writing it down — a prefix the fork owns
(`bd/1.5.3`), or skipping to a range upstream will not reach. Either is fine;
discovering the clash mid-release is not.

## Closed 2026-08-28. The namespace was already chosen; only the writing-down
## was missing.

The predicted failure did not occur, and the fork is now at **1.5.16-BD** —
fourteen releases past the point this entry expected `git tag` to fail
mid-publish. Checked rather than assumed:

* every fork release tag carries a **suffix**: `1.5.1-BNY`, `1.5.1-B2`, then
  `1.5.2-BD` through `1.5.16-BD`, seventeen in all;
* every BARE `1.5.N` tag is upstream's, authored by `0x1abin` and `Uri Tauber`;
* `1.5.3` through `1.5.6` do exist locally, exactly as this entry warned — and
  the fork tagged `1.5.3-BD` … `1.5.6-BD` straight past them without a clash.

**So the suffix IS the namespace**, and it is the same suffix the version string
already carries for the Settings corner and the OTA screen. It was not adopted
as a tag policy; it just fell out of tagging with the full version string, which
happens to include the fork marker. That is why this entry could be written at
all — the practice was invisible because nobody had stated it.

**The rule, stated:** *a fork release tag is its full version string, suffix
included.* `1.5.17-BD`, never `1.5.17`. A bare `1.5.N` tag in this repo is
upstream's and must not be created here. Nothing needs changing to comply —
seventeen tags already do — and `[crosspoint] version` in `platformio.ini`
carries the suffix, so a tag taken from it is correct by construction.

No prefix scheme is needed. `bd/1.5.3` was one of the two options this entry
offered and it would be a second, redundant namespace on top of the one already
working.


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

> **THE STAGED IMAGE IS NOW SIXTEEN VERSIONS STALE — checked 2026-08-28.**
> The card mounted as `BUNNYFIELDS` carries
> `20260817T2333Z-crosspoint-9aae0b3f.bin`, and `strings` on it reports
> **`1.5.0-BNY`**. The fork is at **1.5.16-BD**.
>
> So following the "close by" instruction below TODAY would fix the stamp and
> **downgrade the device to August firmware** — losing every fix since,
> including the untrusted-input memory-safety work (B-023, B-024), the bare-`new`
> sweep (B-031, B-032) and everything shipped this week.
>
> **The close action has therefore changed**: build a CURRENT `gh_release`
> image with `CROSSPOINT_RC_HASH` set, stage that, and update from it. Do not
> flash the image currently on the card.
>
> **STAGED 2026-08-28.** `20260828T2010Z-crosspoint-fbd3129d.bin` is now on the
> card beside the old one, built from `gh_release` and verified in place:
> descriptor version **1.5.16-BD** (the B-033 stamper ran), appended SHA256
> valid, image magic `0xE9`.
>
> Copying it is not a flash — `SdFirmwareUpdateActivity` is a file picker, so
> nothing is written to the device until it is chosen. That is why staging was
> safe to do and the update itself is not mine to perform.
>
> **Pick the 2026-08-28 file, not the 2026-08-17 one.** The older image is still
> there and still stamped `1.5.0-BNY`; it is left rather than deleted because
> removing a firmware image from someone's card is a worse default than leaving
> two and saying which is which.

**Close by:** reflashing with the variable set, or SD Firmware Update from the
card (`SdFirmwareUpdateActivity` is a plain file picker with no version gate,
so a same-code reflash is accepted):
```bash
CROSSPOINT_RC_HASH=880ba0f9 pio run -e gh_release_rc -t upload --upload-port /dev/cu.usbmodem2401
```

---

## FIXED

### [B-042] A definition list rendered as one unbroken blob — FIXED 2026-08-28
**severity: high (every `<dl>` in every book) · scope: chapter parsing /
layout · found by owner report, fixed same day**

`dl`, `dt` and `dd` appeared **nowhere** in `lib/Epub/`. They were not in
`BLOCK_TAGS`, so `startElement` fell through to its INLINE branch and no block
ever opened: a term ran straight into its own definition mid-line. Owner's
screenshot, a quiz book — *"…opens which novel?**A Tale of Two Cities (Dickens,
1859)**The two cities are London and Paris…"*.

The bold in that screenshot is the tell that this is structural and not a
styling bug: the inline branch reads `font-weight` off the resolved CSS, so the
book's `dt { font-weight: bold }` was honored the whole time. Only the block
half was missing.

Fixed by adding the three tags to `BLOCK_TAGS`, giving `<dl>` the container
text-indent reset (and no step of its own — a step there would indent the term
too), and giving `<dd>` the same 1.5 em default step `<ul>`/`<ol>` take **only
when the publisher gave it no left inset of its own**. The reported book states
`dd { margin: 0.25em 0 0.25em 1em }` and `CssParser` already resolved it, so
the book's own 1 em decides the indent; hardcoding one would have misrendered
every book whose `dd` margin differs. A `<dt>` also keeps with **three lines**
of room for its definition to start, so a term is not stranded at a page foot —
the shape of B-038's answer for tables, with the group keep replaced by a room
requirement because the `<dl>` path is streamed where the table path is
buffered.

Three lines, not the classical one: this engine runs widow/orphan keep-2/2 on
the `<dd>` *after* the term's keep has declined, so a one-line rule let a
three-line definition move away wholesale and strand the term anyway — 4 of 41
page alignments, measured. Adversarial review caught that after the first
version was already written up as done; the trade-off table and the cost (zero
extra pages on the reported book) are in the doc.

`SECTION_FILE_VERSION` 54 → 55: three pagination changes ride it, and a section
served from a stale cache would go on showing the reported page.

Full account, including what the CSS layer does and does NOT honor
(`font-size`, `color` and `border-left` on `dd.why`: no, deliberately), the
keep-with-next decision and its one accepted false positive, and before/after
renders of the owner's own book at the page in his screenshot:
[docs/definition-lists-2026-08-28.md](docs/definition-lists-2026-08-28.md).
Host coverage: `test/definition_list/`, 16 cases, 9 of which fail against the
pre-fix tree and two more of which fail against the first, wrong keep rule.

**Not confirmed on a device** — verified headlessly and by render only.

Moved to FIXED 2026-09-04 under the 2026-09-02 ruling that silence closes a shipped fix (device confirm was the only thing owed).

### [B-039] Inknut's L slot drew half-size glyphs on a full-size grid — FIXED 2026-08-26
**severity: high (visible on every page of one shipping family) · scope: font
build tooling · found by owner report, fixed same day**

`build/seedfonts/InknutJunicode/2x/InknutJunicode_14.cpfont` was a **14 ppem**
render — the 2x cut of the 7 pt slot — sitting under the 14 pt slot's filename.
The renderer takes the pen from the base 1x font and the ink from the
companion, so the advance grid was right and the letters were half-size: ink
filled **0.45** of its advance instead of 0.89, and the line box was 90 device
px around 45 px glyphs. Owner: "L size inknut is missized."

Cause: `U+2E3B` rasterises 289x5 px at 32 ppem and cannot be written to
`EpdGlyph`'s uint8 width, so `build-sd-fonts.py --scale 2` aborted; the rename
to slot names runs only on success, so fontconvert's ppem names survived, and
`2 x 7 = 14` collides with a real slot. The drop table that knew this lived in
`install-sim-fonts.py`, which writes the card and never the bundle tree.

Fixed by moving `tier_drops:`/`hires_drops:` into `sd-fonts.yaml`, discarding a
failed build's own output, and gating the rename on a well-formed tier. Card
was never affected; no other family was. Full account and every measurement:
[docs/inknut-l-slot-2026-08-26.md](docs/inknut-l-slot-2026-08-26.md).

**Not confirmed on a phone** — no TestFlight build in that session.

Moved to FIXED 2026-09-04 under the 2026-09-02 ruling that silence closes a shipped fix (device confirm was the only thing owed).

### [B-036] A line with a missing glyph measures narrower than it draws — FIXED 2026-08-20
**severity: low · scope: text layout · found and fixed same day**

Measurement and rendering disagree about kerning after a missing glyph.
`getTextBounds` resets `prevCp = 0` when a codepoint has no glyph
(`lib/EpdFont/EpdFont.cpp:38`), so the pair spanning the gap takes no kern —
but `drawText` and `getTextAdvanceX` keep `prevCp = cp` unconditionally
(`lib/GfxRenderer/GfxRenderer.cpp:802`, `:2662`) and DO look the kern pair up.
A wrapped line measured as exactly fitting can therefore draw a kerned pixel
or two wider and clip at the margin — only on text containing a glyph the
active font lacks, which is also exactly the text the coverage gaps in
[docs/font-unicode-coverage.md](docs/font-unicode-coverage.md) produce.

**Fixed** by making every draw/measure loop sever the pair in BOTH directions:
the glyph is fetched before the kern, a missing one takes no kern in
(`kernFP = glyph ? getKerning(...) : 0`) and resets `prevCp` so none is taken
out. The into-the-hole half was found while writing the test — the original
filing only caught the out-of-the-hole half. `test/missing_glyph_kern/` pins
it with a synthetic font whose kern classes list a codepoint its intervals do
not carry (the exact shape SD-font pruning produces): red on the old code,
green on the fix. Note the two measurers report different metrics by design —
bounds is ink extent, advance is advance sum — so the invariant is a constant
sidebearing gap, not equality.

The trigger population also shrank in the same pass: the coverage fill below
removed the everyday holes.

Moved to FIXED 2026-09-04 under the 2026-09-02 ruling that silence closes a shipped fix (B-036 had no device caveat at all and was a filing error).

### [B-038] A table's header row, and its caption, stranded at the foot of a page — FIXED 2026-08-26
**severity: high · scope: reader, EPUB table layout (T-012 columns path) · fixed 2026-08-26, reproduced and re-rendered before/after**

Owner, with two consecutive pages of his own reading: *"don't split up table
header or caption from rest (when possible). intact is best"*. The first page
ended with the section heading, its intro paragraph, the caption line
**"Effort-to-payoff, best first"**, the header row `Addition | Effort | What it
buys`, the rule under it — and then roughly half a page of nothing. The second
page opened on the first body row. The reader turns the page carrying three
column names in their head, and the page they leave is half empty.

**The book is identified**: `claude-tools` `nutrition/04-add.xhtml`, and the
caption is a real `<caption>` element inside `<table>`, not a preceding
paragraph. That distinction is the whole shape of the fix (see the sibling case
at the foot).

**Mechanism.** `emitBufferedTableAsColumns` breaks pages ROW BY ROW
(`ChapterHtmlSlimParser.cpp`, the `needed` test in the row loop). Each row asks
only whether IT fits, so a header row that fits at the foot of a page is drawn
there, and the first body row — which does not fit — opens the next one. Nothing
in that loop knows a header is worthless without a row under it. The caption
compounds it: `retirePendingBlockBeforeTable()` lays the caption out in ordinary
flow BEFORE the row loop starts (B-037's fix, and correctly so), which spends it
on the page the header is about to leave.

**Fixed** with a keep-with-next, in the shape the prose widow/orphan control
already uses (keep-2/2, `flushPendingLines`). The rule is a pure function,
`tablecolumns::breakBeforeHeaderKeep` (`TableColumnLayout.cpp`, eight tests):
the group is the caption, the header with its rule and the first
`kKeepBodyRows` = 2 body rows, and the page is completed ahead of it when that
group does not fit where it stands AND does fit an empty page. The owner's own
"(when possible)" is the ladder: two rows, then one, then **yield** — a group
too tall for a whole empty page is placed where it stands exactly as before,
because breaking would blank this page and strand the header again on the next.
That pair of tests is also what makes it terminate: after a break the page is
empty, and an empty page is never broken.

The parser side is the MEASURING, and two things about it are load-bearing:

* `measureUnlaidLeadHeight()` has to answer how tall the caption is while it is
  still only potential — once retired it belongs to this page and cannot travel.
  `ParsedText::layoutAndExtractLines` ERASES the words it consumes, so the probe
  runs on a COPY of the block. A second, non-destructive path through the line
  breaker would be a second definition of where the lines go. A pending block
  over `kMaxLeadLines` = 3 lines is NOT a caption — plenty of tables follow a
  whole paragraph — and is measured as 0 so it cannot make the group unkeepable.
* A row costs more than its text. Every cell is an ordinary text block, so
  `makePages()` adds the paragraph's bottom spacing under it before the row gap
  — half a line whenever paragraph spacing is on, which is the shipped default.
  `measureRowHeight()` answers about the text only, which is right for its other
  caller ("is this row taller than a page") and wrong here. Measured: modelling
  a body row at 43 px instead of 60 said a group of three fitted in 695 px of a
  700 px page, and the render put the third row on the next one.

`SECTION_FILE_VERSION` 51 → 52. It moves page BOUNDARIES and grows no header
field, so it is the same kind of bump as v50; without it a section served from a
v51 cache is never parsed again and a reader who saw the reported page would go
on seeing it. Every book on every card repaginates once.

Flash +2,542 bytes (5,304,235 -> 5,306,777, `pio run -e default`, back-to-back
builds of the same tree).

**Two limits, stated rather than papered over.** First, the keep is measured
with `measureRowHeight`, which wraps a cell through `GfxRenderer::wrappedText`
(greedy, whole words) while the cell is actually SET by
`ParsedText::layoutAndExtractLines` (total fit, or greedy with hyphens). The two
can disagree about a cell's line count, so a group can be measured short, and
then the row loop's own per-row break splits it anyway after the page was
completed for nothing. That disagreement predates this change -- the row loop's
existing `needed` test uses the same measure -- and it costs white space rather
than a wrong render, so it is recorded here rather than fixed with a fudge
factor nobody measured. Second, a keep-with-next always MOVES white space
rather than removing it: the page before the table ends earlier than it used to.
That is the trade the ruling asks for ("intact is best") and it is visible in
the after render.

**Evidence.** `test/epubs/test_table_keep.epub` — the reported table, cells
verbatim, set out at fourteen different heights on the page so one rung always
strands whatever the reading size. Rendered in the desktop simulator (X3, 14 pt,
light, native 528x792) at rung 3: BEFORE, the page ends caption / header / rule
and 40% white, and the next page opens on `Canned beans and lentils`; AFTER, the
caption, the header, the rule and that first row are on one page together. The
four page images:
[claude.ai/code/artifact/4522bb50-3522-4c7f-9486-a18b9ea6f71c](https://claude.ai/code/artifact/4522bb50-3522-4c7f-9486-a18b9ea6f71c).

**Coverage.** `test/table_keep_together/` is the first host suite that links
`ChapterHtmlSlimParser` — it paginates real XHTML through the real layout engine
and asserts which page each cell lands on, sweeping the preceding prose from 0
to 14 paragraphs so the header arrives at every height on the page. Two of its
five tests fail against the pre-fix tree and pass after. That closes the gap
B-037 stated in its own entry ("no host test links ChapterHtmlSlimParser, so the
emitter half of this fix is covered by render only").

**The adversarial pass found three real defects in the first version of this
fix**, all of them on 2026-08-26, and every one of them is now a test:

1. **Two breaks in a row, and a paragraph alone on a page.**
   `measureUnlaidLeadHeight()` answers 0 both for "nothing is pending" and for
   "pending, but a whole paragraph rather than a caption" — and in the second
   case the page cursor before the retire is STALE, because that paragraph is
   about to be laid on this page. The first version decided against it, broke
   early, then broke again after the paragraph. Reachable from any
   `<div>text<table>`, which is the shape B-037's own adversarial pass found.
   There is now exactly ONE decision point, chosen by whether a caption is
   travelling: with one, decide before it is retired and never ask again (a
   later break could only leave it behind); without one, decide after, against
   the honest cursor. Pinned by `LooseProseAboveATableCostsAtMostOnePage`, whose
   bound is the honest statement of what a keep may cost — one page, measured
   against the same document with its header row demoted so the keep cannot
   fire.
2. **The row model under-measured by a whole line-height per row with Line Grid
   on.** The row loop snaps TWICE (`makePages` after the cell's bottom spacing,
   then the loop after the row gap); the model snapped once, over the sum.
   `snap(M + L + L/4)` is `M + 2L`, `snap(M + 0.75L)` is `M + L` — about 129 px
   short on a three-row group in a 700 px page, in the direction that completes
   a page and splits the group anyway. Line Grid defaults off, so no render
   showed it, and every test arm ran with it off. There is a `lineGridEnabled`
   arm now.
3. **A ruby line in a caption under-measured**, because the probe accumulated
   raw line advances while `placeLineOnPage` snaps after EVERY line. That is the
   one direction that can leave a caption alone on a page.

Two more of its findings were fixed as risks rather than bugs: the probe copied
an arbitrarily large `ParsedText` BEFORE finding out the block was too long to
be a caption (`ParsedText.h` records that a CJK paragraph runs to thousands of
tokens and that a contiguous allocation that size aborts on a fragmented C3
heap), so a word ceiling now runs before the copy; and the anchor at the break
is keyed on `pendingTextIsUnlaid()` rather than on `leadHeight`, which is 0 for
two different situations.

**What that pass checked and found CLEAN**, so the next one does not re-derive
it: `breakBeforeHeaderKeep`'s bounds and overflow behaviour, swept by hand
(a null `bodyRowHeights` with count 0 never dereferences; the `[k-1]` read is
bounded on every iteration); termination and the impossibility of a blank page;
`ParsedText`'s copyability (all members are value types, no raw pointers, no
retained iterators) and the probe's side effects (the only global write is a
`booknotes` raise that is idempotent and that the real layout makes anyway);
the probe's arithmetic against `makePages()` term by term; the cell-cost model;
the anchor, footnote, `pendingAnchorId` and xpath handling at the break; the
untouched rotated / flattened / key-block / `abandonTableBuffer` paths and the
B-037 `<li>`-holding-a-table case; the row loop's state after a break; the
allocation-failure path; and that `SECTION_FILE_VERSION` 52 is sufficient —
`SECTION_FILE_PARTIAL_VERSION` is derived from it, and neither `CSS_CACHE_VERSION`
nor `BOOK_CACHE_VERSION` governs anything this change touches.

**Two siblings found and deliberately left**, both recorded so the next pass
does not re-derive them:

1. **A caption written as a PARAGRAPH above the table is still stranded.**
   `</p>` calls `startNewTextBlock` (`ChapterHtmlSlimParser.cpp`, the
   `headerOrBlockTag` branch of `endElement`), so such a paragraph is already
   laid out on the page by the time `<table>` is even seen — there is nothing
   pending to measure and nothing to carry. Fixing it means either lifting
   already-placed lines off a finished page or holding block placement past the
   block boundary; both are a second mechanism. Not the reported case (the
   report's caption is a real `<caption>`), but the same defect to a reader.
2. **The ROTATED emitter separates a caption from its table by construction.**
   `emitBufferedTableRotated` retires the caption in upright flow and then gives
   the table a page of its own, so the caption is ALWAYS on the page before.
   Carrying it would mean drawing it as rotated text with the table, which is a
   change to that emitter's geometry rather than to its page breaking.

A THIRD case has the same shape and is explicitly out of scope: an `<h1>`–`<h6>`
immediately followed by a paragraph has no keep either, so a heading can be the
last thing on a page. One mechanism at a time.

### [B-037] A table's `<caption>` printed under its own header row — FIXED 2026-08-23
**severity: high · scope: reader, EPUB table layout (T-012 columns path) · fixed 2026-08-23, verified by render**

Reported with a screenshot of a three-column Catalan phrasebook: the caption
text, `English` and `Say it` all drawn on ONE line, glyphs on top of each other,
and `Catalan` fallen to the line below. Body rows underneath were correct.

`<caption>` is handled nowhere (`grep -rn caption lib/Epub/` returns nothing),
so its text takes the ordinary streaming route — `characterData` diverts into
the table buffer only while a CELL is open
(`ChapterHtmlSlimParser.cpp:1928`) — and is still UNLAID at `</table>`, having
consumed no vertical space. `emitBufferedTableAsColumns` then took
`rowTop = currentPageNextY` while the caption was still owed exactly that y;
column 0's `startNewTextBlock` flushed the caption AT rowTop (via `makePages()`,
`:387`), so `Catalan` landed a line lower, and every later column's
`currentPageNextY = rowTop` printed straight over the caption. One defect, both
symptoms. `emitBufferedTableRotated` had the same seam pointed the other way:
the caption surfaced on the page AFTER the table it names.

**Not the SD step-down.** `smallFontId` — the cut `getSmallestReaderFontId()`
cannot actually obtain for an SD family — is read only by the ROTATED emitter
(`ChapterHtmlSlimParser.cpp:605`). The upright columns path measures and draws
with `fontId` at every step (`:2270`, `:763`, `:782`), so no measure/draw seam
exists there. That lead was checked and refuted; the step-down remains a
documented no-op and an open RAM-versus-feature question, untouched.

Fixed by retiring the pending block before either geometry emitter takes the
page cursor (`retirePendingBlockBeforeTable()`), and by moving the per-column
rewind into a pure function that names its preconditions and can only ever
return a y above the cursor when nothing has been drawn between the two
(`tablecolumns::columnStartY`, four tests). The same change closes an unreported
second way the rewind could overlap: a cell that overflows and completes a page
left `rowTop` naming a y on a FINISHED page, and the old code rewound to it.
`SECTION_FILE_VERSION` 47 -> 48. Flash +148 bytes.

Adversarial review caught the fix reintroducing the reported symptom in a case
the repro did not cover — the retired block's style was carried onto the empty
successor block, where the first cell REUSES rather than flushes it, so a
container margin or a `<br>` flag pushed column 0 below its siblings. Confirmed
by render (`<div>a<br/>b<table>` put `Term` one line under `Gloss`), then fixed
with a neutral successor style. The same pass turned up a SEPARATE pre-existing
case with the identical shape: a `<li>` whose whole content is a table had its
marker merged into the first cell, where it wrapped inside a column planned for
the cell's own text. Verified present on the pre-fix tree, fixed here.

**Coverage gap, stated:** no host test links `ChapterHtmlSlimParser`, so the
emitter half of this fix is covered by render only.

Reproduced and re-rendered before and after in the desktop simulator at 12 pt
(at 14 pt the phrasebook's tables fail `planColumns` and take the key-block
fallback, which never had the bug). A fixture that shows it at the DEFAULT
size is now in the repo: `test/epubs/test_table_caption.epub`, two captioned
tables — one that plans as columns at 14 pt, one that rotates. Full writeup,
including the render-scale hypothesis that was measured and refuted:
[docs/table-caption-overlap-2026-08-23.md](docs/table-caption-overlap-2026-08-23.md).

### [B-035] Only one reading font drew Unicode arrows — FIXED 2026-08-20
**severity: medium · scope: SD-card fonts (`.cpfont`), font build tooling · fixed 2026-08-20, verified by render**

Reported: "only the TeXGyreSchola reading font renders Unicode arrows. Other
installed families show nothing (or a fallback box) where an arrow should be."

**It was true, and it was in the FILES, not in the firmware.** Measured by
parsing the shipped `.cpfont` interval tables directly, before changing
anything — coverage of U+2190-21FF, regular style, the 16 pt cut of each
installed family:

| Family | arrows before | after |
|---|---|---|
| TeXGyreSchola | 4 / 128 | 112 / 128 |
| Edgar | 0 | 112 |
| Coelacanth | 0 | 112 |
| LibreFranklin | 0 | 112 |
| LibrisADF | 0 | 112 |
| InknutJunicode | 0 | 112 |

Schola's four are U+2190-2193, which its own face happens to carry. Every other
family had none, which is exactly the report.

**Mechanism.** All six families REQUEST 112 of the 128 arrow codepoints
(`reading` reaches U+2150-22FF; the three `latin-ext` families had the block
added explicitly on 2026-08-17). A codepoint the family's own cmap misses is
filled from its fallback chain, so the chain's coverage IS the shipped
coverage — and the old chain could not answer: NotoSans-Regular carries **0 of
128** arrows (verified here against `builtinFonts/notosans_12_regular.h`, not
taken on trust), TeX Gyre Schola 4. The build therefore asked for arrows, found
them nowhere, and pruned them silently. `7c764cd9f` fixed the CONFIG on
2026-08-17 by appending NotoSansMath (112/128) to the chain
(`build-sd-fonts.py:144`, `fallback_chain_for`).

**Why it was still broken three days later: nothing rebuilt the files.** A
`.cpfont` fix reaches a surface only when that surface is regenerated, and the
tree in `fs_/` still held cuts dated 12 and 15 August — before the fix. The
config was right and every shipped byte was stale.

**And a second, narrower version of the same fault.** Rebuilding 1x exposed it:
`scripts/install-sim-fonts.py` rebuilt only the base tier, pruned the `2x`
companions by FILENAME, and never mentioned `3x` at all — its own comment said
"this script cannot regenerate those" (twice). So the first rebuild fixed
arrows on the device and left every hi-res host build on the old glyphless
cut. That is the worse half of the bug: the owner inspects this project through
the simulator, so the tier he actually looks at was the one that stayed broken.
`build-sd-fonts.py` has had a `--scale` flag the whole time; the installer just
never called it.

**Why not a renderer fallback, which is the reflex fix.** It cannot work here,
and the measurement is the reason rather than a preference. `resolveTextFontId`
(`lib/GfxRenderer/GfxRenderer.cpp:188-216`) is registered for the three chrome
font ids only (`src/main.cpp:456-458`) — the reading font has no fallback at
all — and it is gated on `utf8IsCjkCodepoint` at `:211`, so a missing arrow
would not redirect even if it were. More decisively, **there is no face in the
binary that could answer**: the coverage face is Noto Sans at 0/128, and a scan
of every `builtinFonts/*.h` found arrows only in the editor monospaces, at most
22/128 and only at 12/14 pt. A glyph nothing in flash can draw has to be in the
`.cpfont`. Fixing the class in the renderer would mean embedding an
arrow-capable face at all four reading sizes — a large flash cost for a block a
one-line build change already covers.

**What changed.** `scripts/install-sim-fonts.py` only. It now builds and
installs 1x, 2x and 3x in one run, so a font-config fix cannot again reach one
tier and silently miss the others. `--clean` is passed on the first tier only
(it `rmtree`s the whole output dir, `build-sd-fonts.py:865`). U+2E3B THREE-EM
DASH is dropped **at 3x only**, where it rasterises 276x8 px and overflows
`EpdGlyph`'s uint8 width — a tier-local omission, not a removal: it is still
built into 1x and 2x, which reproduces the 3x set already on the cards
(verified: present in Edgar 1x and 2x, absent from Edgar 3x).

**Verified**, and not by a green build:

* all **72** shipped files (6 families x 4 sizes x 3 tiers) carry 112/128 arrows
  in all four styles, parsed from the on-disk binary;
* rendered through the REAL firmware renderer off-device — all eight reader
  arrows draw in all six families;
* `test/sd_font_arrows/` added, and validated failing-first: rebuilt
  LibreFranklin with the arrow codepoints dropped and confirmed the test fails
  on it, passes on the shipped set;
* `pio run -e default` and `-e simulator` green; full suite 371/371.

**Not confirmed on device.** No hardware in this session. The device reads the
1x tier, which is verified in the files and in a host render, but nobody has
seen an arrow on a panel.

**Card reprovisioning is still owed.** This fixes `fs_/` (the simulator's card)
and the tooling. The physical cards carry the pre-fix cuts until they are
rewritten: BUNNYFIELDS and OWEN_BNF both need `python3
scripts/install-sim-fonts.py` output copied over, hash-verified per
`docs/sd-card-fonts.md`.

### [B-002] Upstream commits unmerged — CLOSED 2026-08-19, both resolved
**severity: low · scope: fork sync · closed 2026-08-19**

**Both named commits are resolved, so the entry has nothing left to track.**

* `9c48609f` (bookmarks survive re-pagination) is **N/A**: it edits
  `lib/KOReaderSync/ProgressMapper`, and `lib/KOReaderSync/` does not exist here
  — the subsystem was deleted on purpose, along with bookmarks themselves.
* `0f747b82` (content-based EPUB sync positions) is **superseded**: this fork
  solved the same problem independently in `section.bin` v35, with a per-page
  word-anchor LUT that repositions to the exact word after a reflow. Upstream
  counts visible characters via a new `VisibleTextUtils`; this fork records
  source byte positions. Different mechanisms, same guarantee — porting theirs
  would replace a working scheme with another working scheme.

**The backlog itself is not a bug and never was.** Re-measured 2026-08-19:
**540 ahead / 66 behind**. That number is a standing cost of a deliberately
divergent fork, it belongs in [docs/fork-sync.md](docs/fork-sync.md), and it will
keep growing at roughly a commit a day whatever this tracker says. Keeping a
never-closing bug for it only made the open count lie.

Two cautions worth carrying over, since this entry was where they were written
down: `scripts/repo-status.sh` OVERSTATES the backlog, because a commit already
applied by hand still counts as unmerged (6 of 45 on the last pass); and never
probe with `git cherry-pick -X ours`, which reports conflicts as clean.

**Original entry follows.**

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


### [B-032] A failed `reserve()` aborts exactly like a bare `new` — FIXED 2026-08-19
**severity: medium · scope: memory safety · found + fixed 2026-08-19**

**Fixed at the three content-sized sites, and deliberately nowhere else.**

| Site | Guard | Degradation |
|---|---|---|
| `codepoints.reserve(text.size())` | needed bytes vs `getMaxAllocHeap()` | the paragraph loses its CJK break opportunities |
| `allowedOffsets.reserve(...)` | same, and likelier to fire — `codepoints` is resident by then, so the heap is tighter | as above |
| the four parallel token arrays | bulk bytes vs half the largest block | the reservation is SKIPPED, not the pushes |

The third is the interesting one: skipping a bulk reserve is safe precisely
because the pushes still work afterwards. Growing an element at a time asks for
smaller blocks than the bulk reservation does, which is what a tight heap can
still satisfy — so the degradation is slower layout, not lost text.

**Not a blind sweep, by design.** Every other `reserve()` in this file and its
neighbours takes a constant or a small bound; converting them would be churn
that hides the three that matter.

**Verified:** `-e default` and `-e simulator` build, 370 host tests pass, and a
real book still paginates and renders unchanged. The guards themselves do not
fire on a host — the simulator's heap is a constant, so it cannot be walked down
past a threshold — which is the same limit recorded in [B-030].

**Original entry follows.**

[B-030] and [B-031] swept every explicit `new` out of the fallible paths. They
did not touch CONTAINER growth, which has the same failure: `-fno-exceptions`
makes `std::vector::reserve()` and `push_back()` abort on OOM rather than fail,
so the sweep closed one door and left another open beside it.

Found while auditing `0x1abin/crossmux`, whose layout-OOM fix exists precisely
because this bites (see [fork-audits.md](docs/fork-audits.md)). Their patch does
not apply — their line breaker diverged — but the premise does.

Where it matters most, because the size comes from the content rather than from
a constant:

| Site | Sized from |
|---|---|
| `ParsedText.cpp:153` `codepoints.reserve(text.size())` | the paragraph's byte length |
| `ParsedText.cpp:170` `allowedOffsets.reserve(...)` | codepoint count |
| `ParsedText.cpp:329-331` the parallel word arrays | token count, doubling |

A long paragraph in a tight heap is the shape that fires it, which is exactly
what crosspoint-jp's CJK bug was.

**Not a blind sweep.** Most `reserve()` calls in this codebase take a constant or
a small bound and are fine; converting them all would be churn. The fix is a heap
check before the content-sized ones, degrading the way the CSS parser now does.

**Done looks like:** the content-sized reservations check the heap first and
degrade rather than abort, and the entry records which sites were judged safe and
why.


### [B-003] Exploded `.epub` directories present as folders, never as books — FIXED 2026-08-19
**severity: low · scope: content · found 2026-08-03 · re-zipped 2026-08-19**

**Fixed by re-zipping all ten**, which was the close-by step this entry named.
They are at `~/src/crosspoint-books/_rezipped/` — note the path, because this
entry said `~/crosspoint-books/_exploded/` and that directory does not exist;
the preserved copies were under `~/src/`. Anyone re-checking this should look
there.

Each was rebuilt with `mimetype` stored first and uncompressed, as OCF requires,
and with macOS `._*` and `.DS_Store` droppings excluded — the AppleDouble files
that would otherwise ride along are the same class of problem the font cards
have had. Verified two ways: every archive re-opened and checked
programmatically (first entry is a STORED `mimetype`, `META-INF/container.xml`
present, no resource-fork files, 10 of 10 clean), and then *Apartment Gardening*
opened in the simulator and rendered its cover and title page.

**Still owed, and it is a copy rather than a fix:** the card still holds the ten
exploded directories. Replacing them with these files is a card operation, on
the device checklist with the rest.

**Original entry follows.**

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


### [B-031] Thirteen more bare `new`, all on the EPUB reading path — FIXED 2026-08-18
**severity: high · scope: memory safety · found + fixed 2026-08-18**

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

**Fixed. The pagination answer: stop the parse, and cache nothing.**

A failed `Page` or `ParsedText` sets a sticky `allocFailed_` and calls
`XML_StopParser(parser, XML_FALSE)`. That choice is the substance of this fix:
expat callbacks cannot return an error, and this file dereferences
`currentPage` 9 times and `currentTextBlock` 37 times on the assumption they are
never null. Stopping the parser means **no further callbacks arrive**, so those
46 sites stay unreachable instead of needing 46 null checks — a diff nobody
could review against one that changes five allocations and one control path.
`parseStep()` turns the flag into `ParseStatus::Error`.

**`finishParse()` refuses to flush the trailing page when the flag is set.**
That is the part that matters for a reader: pages are handed to the caller
through `completePageFn` as they complete, and the caller writes them to
`.crosspoint/sections/*.bin`. A chapter truncated by OOM and then cached is
**silent text loss that survives every later read** — the book simply ends
early, with no error, until someone clears the cache. Failing the build leaves
no finalized cache, so the next attempt re-paginates.

The other eight are `makeUniqueNoThrow` with the failure routed into a path the
callers already had: `Epub::load` and `Xtc::load` return false; the image
decoder factory returns `nullptr`, which is its existing no-decoder answer;
`Page::deserialize` returns `nullptr`, which every caller already null-checks;
`EpubReaderActivity::render` returns early, as it already does in two other
places, so the next render retries when the heap may have recovered.

**Verified:** zero bare `new` outside vendored code (grep), `-e default` and
`-e simulator` build, 353/353 host tests after rebuilding the test binaries
(the first run reused stale ones — worth knowing), and a headless read with the
section caches deleted, which forces the modified parser to lay a chapter out
from scratch: book opens, 9 pages persisted, second pass over the same book
logs zero errors. The OOM branch itself is untested for the reason ruled on in
[B-030] — no fault-injection seam.

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

### [B-043] Edgar -> Inknut at XL renders smaller than Edgar at L — CLOSED 2026-08-27, working as designed
**severity: medium (the control looks broken) · scope: reader font size · filed and reproduced 2026-08-27**

Filed as B-030; renumbered B-043 on 2026-08-29 to resolve a duplicate-id
collision with the unrelated allocation-safety entry above that had already
claimed B-030. Follow an old B-030 reference here if it lands you on this
entry instead.

Owner, from the device: *"in reading mode, switch from edgar to inknut seems to
lose the font size (XL becomes L)."*

**The slot is NOT lost.** Reproduced headlessly on the simulator card, slot
pinned to 5, switching only the family:

```
[SDFS] Slot 5 resolves to 18 pt   (Edgar)
[SDFS] Slot 5 resolves to 16 pt   (InknutJunicode)
```

`fontSizeSlot` is untouched by the switch — the in-reader cycle writes only
`sdFontFamilyName`, and `resolveReaderPointSize` refreshes the derived point
size without touching the slot, exactly as its comment promises. So this is not
B-030-as-filed (now [B-043]); the report is accurate about the SYMPTOM and wrong only about
the cause, which is worth stating because the obvious fix (persist the slot
harder) would change nothing.

**What actually happens.** The two families spend their slots on different point
sizes, and since 2026-08-27 they also carry different `scale:` multipliers from
the Almendra-anchored normalisation:

| | slot 5 (XL) | k | effective |
|---|---:|---:|---:|
| Edgar | 18 pt | 0.960 | **17.28** |
| Inknut Junicode | 16 pt | 0.917 | **14.67** |

A 15.1 % drop. Edgar's **L** is 16 x 0.960 = 15.36, so Inknut's XL renders
slightly SMALLER than Edgar's L. The owner's description is not an
approximation — it is literally what the glass shows.

**And it is partly a consequence of a change he approved.** Before the
normalisation the same pair was 18 vs 16 pt, a 11.1 % drop; the multipliers
widened it to 15.1 %. The pre-existing half is the ramps themselves, which have
always differed.

**The tension this exposes, which is the real finding.** The normalisation
target is WORDS PER PAGE, and at XL it succeeds precisely — Edgar 74.19,
Inknut 74.64 words on a full page, measured the same day. Equal text per page
and equal apparent glyph size are DIFFERENT targets, and for a wide, heavy face
like Inknut they pull in opposite directions: fitting the same words requires
smaller glyphs. A slot cannot mean both "the same amount of book" and "the same
size of letter" across faces.

So this is not a defect to patch but a target to choose, and the choice is the
owner's. Options, with what each costs:

1. **Keep words-per-page.** Book length stays consistent whichever face is
   chosen — the property just shipped and measured. Cost: this report stays
   true, and switching faces at a fixed slot visibly changes letter size.
2. **Normalise on x-height instead.** Letters look the same size across faces.
   Cost: gives up the equal-book-length property entirely, and reinstates the
   151-page spread the normalisation removed. This was the ORIGINAL anchor and
   was abandoned on measurement — see `docs/words-per-page-2026-08-26.md`.
3. **Split the difference** — normalise on a blend. Cost: neither property holds
   exactly, and the blend weight is a new unmeasured constant.

**OWNER RULING, 2026-08-27, and this closes it:** *"we used words per book to
generate sizes, but switching between fonts should only be ranked size."*

So the two questions are separated rather than traded off. Words per page stays
the method that GENERATED the ramps — option 1, no multiplier or ramp changes.
And a slot is a **RANK**, not a physical size: switching a font carries the rank
across and nothing else is promised. That is already exactly what the code does,
verified above — slot 5 in, slot 5 out.

**No code change. Nothing to fix.** The behaviour reported is the design, and
the reason it reads as a size loss is that a rank is not a size: XL means "the
largest of this family's six", and for a wide heavy face that lands at fewer
effective points because it has to, to hold the same words.

Kept in the tracker rather than deleted, because "the largest size got smaller
when I switched fonts" is a report anyone would file again, and the measurement
above is the answer to it. The three options and their costs stay below as the
record of what was weighed.

`docs/almendra-anchored-sizing-2026-08-27.md` carries the measurement.

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

**UPDATE 2026-08-21: the silent half is fixed — the guard hard-fails now.** A
fresh worktree build of `gh_release` refuses with the B-029 banner rather than
succeeding smaller, which is how the 1.5.3-BD release build caught it in
practice. The copy step above is still required (and still unscripted); after
copying, the 1.5.3-BD binary verified at 100 face symbols and full size. Every
shipping worktree needs the copy — the simulator's TestFlight and Mac-app
builds hit the same guard when pointed at a fresh worktree.
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

