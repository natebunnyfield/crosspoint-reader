# Stack frame budget gate

Enforces the Resource Protocol's stack rule ("limit local function variables to
< 256 bytes", `CLAUDE.md`) mechanically, instead of discovering violations from
device crashes.

## Why

`lib/EpdFont/SdCardFont.cpp:265-284` is the case study. `buildMiniKernMatrix`
had six 256-entry scratch tables on the stack, making its frame **1648 bytes** —
6.4x the limit. It was found by a device crash report that ended mid-way through
a mini-kern build with an *empty* panic reason (the documented signature of a
blown stack), followed by a manual `-fstack-usage` run. Six more oversized
frames were found the same way in a single session: 6240, 4416, 4224, 3120,
2912 and 2432 bytes.

Every one of those was measurable at compile time. Nothing was measuring.

## Quick start

```sh
tools/stack_budget/stack_budget.py                    # build + check (~2 min)
tools/stack_budget/stack_budget.py --no-build         # re-check without rebuilding
tools/stack_budget/stack_budget.py --threshold 384    # see what a tighter gate would catch
tools/stack_budget/stack_budget.py --suggest          # emit allowlist lines for violations
tools/stack_budget/stack_budget.py --json /tmp/f.json # full frame list for analysis
```

Exit codes: `0` pass, `1` a frame is over budget, `2` usage or build error.

Last line is a single greppable summary for CI logs:

```
STACK_BUDGET: PASS threshold=512B env=default frames=1854 over=0 regressions=0 allowlisted=20 stale=0 obsolete=0 slack=0 worst=1008B(src/activities/boot_sleep/SleepActivity.cpp)
```

## Why python3 and not bash

Both were viable; python3 won on three counts.

1. **Parsing correctness.** A `.su` line is
   `location:line:col:signature<TAB>size<TAB>qualifiers`, and the signature is
   *demangled*, so it contains spaces, commas and colons. `str.split('\t')`
   cannot get this wrong. The awk equivalent needs `-F'\t'` plus `$2+0` to force
   numeric comparison, and the failure mode when you forget is silent garbage
   (see Gotcha 2).
2. **The allowlist needs real data structures** — keyed lookup, per-entry match
   state, and four distinct ratchet verdicts (in-use / stale / obsolete / slack).
   That is associative-array work that bash does badly.
3. **python3 is already a hard build dependency.** `platformio.ini:66-72` runs
   five python `extra_scripts` (`gen_i18n.py`, `build_html.py`, …), so this adds
   no new requirement. Uses only the standard library.

## How it works

1. Builds `-e default` with `-fstack-usage` appended via
   `PLATFORMIO_BUILD_FLAGS`, into a **private** build dir.
2. Reads every `*.su` under it (456 files, 9869 frames at time of writing).
3. Keeps frames whose location starts with `src/` or `lib/`, minus vendored
   subtrees — 1854 first-party frames.
4. Dedupes by (file, signature) keeping the **largest** size. A header-defined or
   template function appears once per translation unit that instantiates it,
   sometimes at different sizes after inlining; the worst one is the one that can
   blow the stack.
5. Compares against the threshold and `allowlist.txt`, prints worst-first, exits
   non-zero on any unexcused frame.

### The two gotchas that cost hours

**Gotcha 1 — you must use a private build cache.** `platformio.ini:3` sets
`build_cache_dir = .cache`. On a cache hit PlatformIO restores the `.o` file
*without running the compiler*, so no `.su` is written and the gate happily
checks zero frames. The script sets `PLATFORMIO_BUILD_CACHE_DIR` to its own
directory, which both guarantees `.su` output and leaves the shared cache
untouched. If you ever see `no .su files under …`, this is why.

**Gotcha 2 — the size column is not field 2 of a space split.**
`riscv32-esp-elf-gcc` writes demangled signatures:

```
lib/EpdFont/SdCardFont.cpp:265:6:bool SdCardFont::buildMiniKernMatrix(PerStyle&, const uint32_t*, uint32_t)	1648	static
```

`awk '$2 > 256'` compares `SdCardFont::buildMiniKernMatrix(PerStyle&,` against
`256` as a string. Always `awk -F'\t' '$2+0 > N'`, or `split('\t')` in python.

### What is excluded, and why

`lib/miniz/`, `lib/expat/`, `lib/uzlib/`, `lib/MiniBidi/` are vendored sources
that happen to live under `lib/`. Measured frames in them:

| Frame | Size |
|---|---|
| `crosspoint_tinfl_decompress_mem_to_callback` (miniz) | 8480 B |
| `crosspoint_tinfl_decompress_mem_to_heap` (miniz) | 8464 B |
| `crosspoint_tinfl_decompress_mem_to_mem` (miniz) | 8400 B |
| `do_bidi` (MiniBidi) | 1392 B |
| `handleUnknownEncoding` (expat) | 1056 B |

Without the exemptions the gate would red-light forever on code we do not
maintain. `freeink-sdk/`, `.pio/libdeps/**` (wolfSSL, ArduinoJson, PNGdec,
JPEGDEC, SdFat, …) and the toolchain packages are already outside `src/` and
`lib/`; they are listed in `EXEMPT_SUBSTRINGS` anyway so the filter stays correct
if the include set is ever widened.

## Which stack a frame lands on decides whether it is fatal

A 1 KB frame is harmless in one task and fatal in another. There are two that
matter:

- **Render task — 8192 bytes.** Created in
  `src/activities/ActivityManager.cpp:25-31` (`xTaskCreatePinnedToCore(…, 8192,
  …)`). Everything in `Activity::render()`, the theme `draw*` methods, the font
  path and the EPUB layout path runs here. This is the tight one, and it is the
  stack the `buildMiniKernMatrix` crash blew: by the time that ran,
  `FontSelectionActivity::render` (224) + `renderPreviewPane` (400) +
  `prewarmCache`/`prewarm`/`prewarmStyle` (224) were already on it, with the SD
  and FATFS read frames still to come underneath.
- **loopTask — 16384 bytes.** Pinned by `SET_LOOP_TASK_STACK_SIZE(16 * 1024)` at
  `src/main.cpp:319`. Before that it was whichever weak
  `getArduinoLoopTaskStackSize()` the linker picked (8192 or 16384 — link order,
  not intent). The web server, WebDAV handler and settings-list builder run here;
  `src/main.cpp:314-318` names them as the reason for the 16 KB.

So when triaging a violation, ask which task reaches it. A 700-byte frame in
`CrossPointWebServer` has 16 KB underneath it; the same 700 bytes in a theme's
`drawRecentBookCover` has 8 KB, already partly consumed by the activity render
frames above it.

The gate deliberately bounds **per-frame** cost only — see Limitations.

## Threshold and the ratchet path

Measured distribution of first-party frames (`-e default`, `-Os`, git `9346cdb7`):

| Frames above | Count |
|---|---|
| 1024 B | 0 |
| 768 B | 3 |
| 640 B | 9 |
| **512 B** | **20** |
| 448 B | 25 |
| 384 B | 38 |
| 320 B | 56 |
| 256 B | 74 |

**Initial threshold: 512 bytes**, with the 20 frames above it seeded into
`allowlist.txt`. Chosen because it is the tightest round number where the
exception list is small enough to name and justify individually, and it is 1/16
of the render task's 8192 bytes — a frame at the limit still leaves room for the
~10-deep nesting the font path actually reaches. The gate passes today.

The intended ratchet toward the Resource Protocol's 256 bytes:

| Step | Threshold | Work it forces |
|---|---|---|
| now | 512 | 20 allowlisted; gate blocks anything new over 512 |
| 1 | 512 | Attribute the 13 `not yet attributed` entries. No threshold change — just replace TODO reasons with causes. |
| 2 | 448 | Fix the ~7 `char name[500]` / `char output[512]` locals. They are one recurring idiom, not seven bugs (see below), so this step is cheap per frame. |
| 3 | 384 | 18 more frames, mostly render-path (`drawRecentBookCover`, `*Activity::render`). Highest value: these are on the 8 KB stack. |
| 4 | 320 → 288 | Diminishing returns; expect several genuine `# reason` keeps here. |
| 5 | 256 | Protocol limit. Some frames will legitimately stay allowlisted — a function with three `std::string` locals is already ~96 bytes before it does anything. |

Each step is: run the tool, fix or allowlist what it reports, lower the default,
commit. The tool assists directly — it reports **slack** (frame is smaller than
its allowlisted budget: tighten it) and **obsolete** entries (frame is now under
the threshold: delete the entry), so the ratchet is mechanical rather than a
guessing game.

**The single highest-value fix.** Of the 20 seeded frames, 7 are dominated by one
idiom — a ~500-byte `char` local:

```
src/network/CrossPointWebServer.cpp:424    char name[500];
src/network/CrossPointWebServer.cpp:490    char output[512];
src/network/CrossPointWebServer.cpp:1166   char output[512];
src/network/CrossPointWebServer.cpp:1335   char output[512];
src/network/WebDAVHandler.cpp:228          char name[500];
src/activities/util/BmpViewerActivity.cpp:35   char name[500];
src/activities/boot_sleep/SleepActivity.cpp:118 char name[500];
```

Each is alone 2x the protocol limit. A shared helper (or
`makeUniqueNoThrow<char[]>`) would clear a third of the allowlist in one change.

## Allowlist

`allowlist.txt`, one frame per line:

```
<budget-bytes> <repo-relative-file> <demangled signature>  # reason
```

- **The reason is mandatory.** An entry without `# reason` is rejected *and fails
  the gate* — an exemption nobody had to justify is how a budget dies.
- **The budget is a cap, not a licence.** It is the size measured when the entry
  was added. If the frame grows past it, the gate fails as a `REGRESSION`.
- **Keyed on file + signature, never line number.** Line numbers move whenever
  anything above the function is edited; keying on them would rot the file on
  every commit. The cost is that changing a function's signature drops its
  exemption — which is correct, that is a different function now.
- Run `--suggest` to get correctly-formatted lines with the exact signatures.
- Once the list is short, add `--fail-on-stale` to CI so dead entries cannot
  accumulate.

## Alternative evaluated: `-Werror=frame-larger-than=N`

The obvious simpler design: add the flag to `platformio.ini` and delete this
tool. No `.su` files, no parsing, no script. It was tested, not just reasoned
about — a full build with `-Wframe-larger-than=1024` produced **9 warnings, all
of them in code this project does not maintain**:

```
lib/MiniBidi/minibidi.c                                   1320 bytes
lib/miniz/src/../third_party/miniz.c                      8392 bytes
lib/miniz/src/../third_party/miniz.c                      8376 bytes
lib/miniz/src/../third_party/miniz.c                      8408 bytes
lib/expat/xmlparse.c                                      1040 bytes
freeink-sdk/libs/network/SecureNet/include/SecureHttpClient.h  1040 bytes  (x4)
```

Four findings from that run:

1. **It fires on third-party code first, and there is no way to exempt by path.**
   At the 512 threshold this gate actually uses, `.su` data shows **16 distinct
   third-party frames over 512 bytes** across 7 components: miniz (3),
   framework-arduinoespressif32 (3), wolfSSL (3), MiniBidi (2), SecureNet (2),
   SdFat (2), expat (1). `-Werror` would fail the build on all 16.
2. **Those suppressions are mostly not reachable.** `#pragma GCC diagnostic`
   requires editing the source: `.pio/libdeps/**` is regenerated on demand,
   `freeink-sdk/` is a submodule, and the three
   `framework-arduinoespressif32` frames are in the toolchain package. You would
   be reduced to coarse `-Wno-frame-larger-than` per library, which switches the
   check off exactly where a real regression could hide.
3. **Header functions warn once per translation unit.** `SecureHttpClient.h`
   produced 4 identical warnings for the same function. There is no dedupe, so
   log noise scales with include fan-out, and a per-frame allowlist is impossible.
4. **The two mechanisms do not report the same numbers.** Same build, same files:
   the three large `miniz.c` frames are 8400/8464/8480 in the `.su` data but
   8376/8392/8408 in the warnings; `minibidi.c` is 1392 vs 1320; `xmlparse.c`
   1056 vs 1040. The anchor line numbers differ too, so the two outputs cannot
   even be joined per function without more work. A threshold tuned with one tool
   therefore does not transfer to the other, and neither can be swapped in for
   the other mid-ratchet.

It also cannot do the two things that make a budget survive contact with a real
codebase: **a per-frame allowlist with a written reason**, and **regression
detection** (fail when an already-known frame *grows*, even while it stays under
the global threshold).

### Recommendation

**Ship the `.su` gate as the enforcing check.** It is the only option that can
exempt vendored code by path, dedupe header/template frames, hold a reasoned
allowlist, and ratchet per frame. Cost is a ~2 minute build plus a parse step,
which is CI-shaped work, not inner-loop work.

**Do not adopt `-Werror=frame-larger-than` as the gate** while 16 unfixable
third-party frames sit above the threshold.

Optionally, `-Wframe-larger-than=2048` in **warning** form is a reasonable
zero-cost extra: at 2048 only the three miniz frames fire, so the noise is
bounded and known, and a developer gets the signal in compiler output
immediately rather than waiting for CI. That needs a `platformio.ini` change and
ideally a per-library `-Wno-` for `lib/miniz`, so it is proposed here rather than
done — neither file belongs to this tool.

## CI wiring

Add a job alongside the existing `build` / `cppcheck` jobs in
`.github/workflows/ci.yml`:

```yaml
  stack-budget:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v6
        with:
          submodules: recursive
      - uses: actions/setup-python@v6
        with:
          python-version: '3.14'
      - uses: astral-sh/setup-uv@v7
      - run: uv pip install --system -U https://github.com/pioarduino/platformio-core/archive/refs/tags/v6.1.19.zip
      - run: tools/stack_budget/stack_budget.py --build-dir /tmp/sb/build --cache-dir /tmp/sb/cache
```

Do not share a cache key with the `build` job — this job needs the compiler to
actually run (Gotcha 1).

## Limitations

- **Per-frame, not per-call-path.** `-fstack-usage` gives each function's own
  frame; it does not sum a call chain. Twelve compliant 200-byte frames still
  overflow a 2048-byte task stack. Bounding true depth needs a call graph on top
  of this data (`--json` exists for that). Until then, keep using
  `uxTaskGetStackHighWaterMark()` for the depth question.
- **`-e default` only** by default. `gh_release` and `slim` differ in
  `LOG_LEVEL` and `ENABLE_SERIAL_LOG`, which changes some frames. Pass `--env` to
  check another.
- **Frames marked `dynamic`** (alloca/VLA) are reported with a warning: GCC's
  number covers only the static part, so actual use is unbounded. None are
  first-party today.
- **Inlining moves cost to the caller.** A large frame may be the sum of several
  inlined callees, so the reported line is where the space is spent, not always
  where the code is written.
- **Optimisation-level sensitive.** Numbers are `-Os` for the riscv32 target.
  A different `build_type` will shift them, so re-seed rather than mixing.
