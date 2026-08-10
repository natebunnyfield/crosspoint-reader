# tools/patches — fixes for code that lives outside this repository

Two input-timing defects were confirmed while tracing long-press behavior. Neither
one is in this repository's own sources, so neither can be fixed by a commit here.
Both are carried as unified diffs against their real home, with the defect, the
behavior they must match, and the test recipe in the patch header.

| Patch | Target repo | Fixes |
|---|---|---|
| `0001-crosspoint-simulator-halgpio-completed-hold.patch` | [crosspoint-simulator](https://github.com/crosspoint-reader/crosspoint-simulator) `src/HalGPIO.cpp` | `getHeldTime()` returns 0 on the release frame, so every `wasReleased(X) && getHeldTime()` check takes the wrong branch in the simulator |
| `0002-freeink-sdk-inputmanager-per-button-held-time.patch` | [freeink-sdk](https://github.com/Free-Ink/freeink-sdk) `libs/hardware/InputManager/*` | `getHeldTime()` is one global chord timer, so a front button pressed during a held side button reports the side button's elapsed hold |

## Why they cannot live in this repo

**0001 — the simulator is a PlatformIO git dependency.** `platformio.ini:200` pulls
it as `simulator=https://github.com/crosspoint-reader/crosspoint-simulator` into
`.pio/libdeps/simulator/simulator/`. That tree is build output: PlatformIO
re-clones it whenever the dependency or the lockfile changes, and `.pio/` is
gitignored. An edit there survives until the next dependency refresh and is
invisible to every other checkout, so the only durable fix is upstream.

**0002 — freeink-sdk is a git submodule.** `.gitmodules` pins
`freeink-sdk` → `https://github.com/Free-Ink/freeink-sdk.git` (branch `main`),
currently at `e7d3361`. A commit in this repo can only record a new submodule
*pointer*; the SDK content itself has to be committed in the SDK repo first.
Editing the submodule in place produces a change that no `git push` from here
carries, that CI cannot see, and that every other clone silently lacks. It is
deliberately **not** applied.

## Applying locally for testing

Both patches are plain `-p1` unified diffs (leading prose is ignored by both
tools). Verified with `git apply --check` and `patch -p1 --dry-run` against
pristine copies of the target files.

```bash
# 0001 — simulator. Re-apply after any `pio pkg update` / libdeps refresh.
cd .pio/libdeps/simulator/simulator          # a real git checkout, branch main
git apply -p1 ../../../../tools/patches/0001-crosspoint-simulator-halgpio-completed-hold.patch
git diff --stat                              # confirm; `git checkout -- src/HalGPIO.cpp` to revert
# then rebuild the simulator env

# 0002 — SDK. Local experiment only; do not commit the submodule pointer with it.
cd freeink-sdk
git apply -p1 ../tools/patches/0002-freeink-sdk-inputmanager-per-button-held-time.patch
git diff --stat                              # `git checkout -- libs/hardware/InputManager` to revert
```

Note for 0001: a patched `HalGPIO.cpp` is shared by every simulator build in this
working copy. Do not leave it applied while someone else is building or running
the simulator.

## Where to send them upstream

* **0001** → PR against `crosspoint-reader/crosspoint-simulator`, branch `main`
  (base `ed31bd9`). Self-contained, one file. The patch header lists the three
  firmware call sites it repairs and the `CROSSPOINT_SIM_INPUT_SCRIPT` runs that
  demonstrate before/after — the reviewer needs no device.
* **0002** → PR against `Free-Ink/freeink-sdk`, branch `main` (base `e7d3361`,
  the commit this repo pins). Additive only: `getHeldTime()` keeps its exact
  current behavior, a new `getHeldTime(buttonIndex)` overload is added, so no
  SDK consumer changes when it lands. After it lands, bump the submodule here and
  migrate call sites individually — the list is in the patch header.

## Recommendation for defect 0002 (per-button timer vs. the ad-hoc locks)

**Land the additive SDK accessor, then migrate the firmware call by call. Do not
redefine `getHeldTime()`, and do not treat the existing locks as the fix.**

* Redefining the no-arg accessor (for example to "the most recently pressed
  button") would retarget ~20 call sites at once, including continuous
  list-scroll repeat (`src/util/ButtonNavigator.cpp:70`) and the multi-stage
  keyboard holds (`src/activities/util/KeyboardEntryActivity.cpp:200,294,323,332`),
  on behavior that cannot be validated without hardware. It would also change
  meaning for every other freeink-sdk consumer.
* The existing ad-hoc locks are **not** workarounds for this defect and must not
  be removed when it is fixed:
  * `src/activities/home/FileBrowserActivity.cpp:86` `lockLongPressBack` is armed
    from `isPressed(Back)` in `onEnter()`. It suppresses a *carried-over* hold —
    the reader's long-press-Back jump enters the browser while Back is still
    down. Per-button timers do not help: Back's own timer is genuinely past the
    threshold. Same for `lockNextConfirmRelease` at line 79.
  * `src/activities/home/RecentBooksActivity.cpp:49-54` `longPressFired` swallows
    input until Confirm is physically released, so the release that follows a
    fired long press does not also open the book. That is release-edge
    suppression, unrelated to which timer is read.
  * `src/activities/reader/EpubReaderActivity.cpp:439` `ignoreNextConfirmRelease`
    is the same pattern.
* The reader has **no** such lock on Back
  (`src/activities/reader/EpubReaderActivity.cpp:476`), which is why the chord
  bug is live there: hold a side page-turn button for >1 s, then press Back, and
  the file browser opens on the press instead of Home on the release.
* If a fix is wanted before the SDK bump, the only option that ships from this
  repo alone is the pattern `KeyboardEntryActivity` already uses: latch
  `wasPressed(button)` in the activity and time the hold with `millis()`, which
  is immune to the chord timer. That is a firmware change, in files this task
  does not own, and it duplicates timing state per activity — which is why the
  SDK accessor is the better destination.

Every call site whose behavior would change once migrated to
`getHeldTime(buttonIndex)`, and the three that need extra plumbing first, are
enumerated at the end of `0002-freeink-sdk-inputmanager-per-button-held-time.patch`.
