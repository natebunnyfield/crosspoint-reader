# Lag reduction plan: page turns, gestures, menus, book open — 2026-11-07

Owner goal, verbatim: *"using the local simulator, devise strong plan for
improving how to reduce lag with processing, gestures, page turns and
everything that can be sped up in menu navigation and reading a book"*.

Surveyed at firmware `de49776e4`, simulator `36009903`, freeink-sdk `1dc07f6`.
Every press below was scripted in the desktop simulator (`-O2`, X4 profile,
800x480, clean card, default settings) with a new trace that stamps each input
edge and each panel call, hashes the framebuffer at every refresh, and counts
storage calls between them. Nothing in the firmware was changed for this
document.

**Status of every item: PLAN. Nothing here is implemented.** Device timings
quoted as "measured" come from earlier on-glass sessions and are cited; every
other device figure is labeled an estimate.

## The one-paragraph answer

The simulator shows that the firmware's own CPU work per action is tiny on the
host (0.2-5 ms press-to-panel-call) and that **almost all lag is panel
waveforms the firmware chooses to run** — how many, which mode, and whether
they change anything — plus a few fixed waits (button release, idle poll,
wake delays). The panel is ~500 ms per FAST and ~1,650 ms per HALF on the X4
(measured, `docs/notes-and-claude.md:205-206`). So the plan is ordered by
**waveforms removed or shortened**, then by **fixed waits removed**, then by
CPU/SD work, and it starts with the one number nobody has: how long the
anti-aliasing (gray) waveform takes on glass, because it runs on every page.

## What a page turn costs today (measured in the simulator)

Default settings: text anti-aliasing STANDARD (`CrossPointSettings.h:357`,
constexpr), HALF every 15 pages (`:477`, constexpr). Anne of Green Gables,
built-in reader font, 40 consecutive forward turns:

| Step | Measured (host) | Device cost |
|---|---|---|
| Finger down → action | fires on **release**: +106-157 ms with the script's 150 ms hold | the whole hold time of a real click (estimate 80-150 ms) |
| Release → BW panel call | 0.6-2.1 ms | CPU unknown on device (see Phase 0) |
| BW waveform | FAST, 1 per turn | **~497 ms** measured |
| Every 15th turn | HALF instead (turns 17 and 32) | **~1,655 ms** measured, +1.16 s |
| Gray (AA) pass | page re-rendered in 12 bands (2 planes x 6 strips), gray waveform @ +1.6-6.9 ms | **never measured on glass** |
| Storage per turn | **442-486 read calls for ~2 KB** (~4 bytes per call), 2-3 seeks, 0-1 opens | per-call overhead x ~460 |
| Idle prewarm of the next page | ran on **6 of 40 turns** | see finding F4 |

With an SD-card font (Nitti Typewriter 14, 12 turns): **639-720 reads,
4.2-7 KB, ~29 seeks per turn** — glyph loads on the critical path, because the
prewarm that should have fetched them did not run (F4).

Chapter boundary (next chapter not cached yet): 1,859-1,929 reads, 23-28 KB,
39-41 writes, panel call at +4.4-5.1 ms on the host.

## What menus cost today (measured)

| Action | Fires on | Panel calls | Storage |
|---|---|---|---|
| Home cursor (front Up/Down) | **press** | 1 whole-panel FAST | none |
| Home side button (page jump) | press | 1 FAST (none at the end of the list — correct) | none |
| Settings row (front Left/Right) | **release** | 1 whole-panel FAST | none |
| Settings → Typography / Colophon | press | 1 FAST (+ gray on Colophon) | none |
| Reader Font picker row | release | **FAST + gray = 2 waveforms** | none |
| Reader → Home, first time | release | **5 FAST**: one is a SAME-FRAME repaint, two are a "Loading" popup and its progress bar, then a JPEG decode of the 1400x2100 cover (1.38 MB read) for the thumbnail | 1.38 MB |
| Settings → Home, any later entry | press | **2 FAST, the second a SAME-FRAME repaint** | 3 reads |
| Home → resume book | release | HALF + gray | 1,682 reads |
| Boot straight into a book whose first page has an image | — | FAST, FAST, FAST(placeholder), HALF, FAST, FAST, gray | 11,017 reads |

"Whole-panel" because every list render starts with `clearScreen()`, which
marks the entire screen dirty (`GfxRenderer.cpp:2262`), so the windowed
refresh path (`:2340-2354`, used when the dirty rect is <= 2/3 of the panel)
never triggers for a cursor move.

## Findings

Each finding: what, evidence, how sure.

**F1. Reader page turns fire on release, and nothing needs them to.**
`detectPageTurn` uses the release edge unless `longPressButtonBehavior == OFF`
(`ReaderUtils.h:72-84`); that setting is `constexpr FONT_SIZE_STEP`
(`CrossPointSettings.h:528`), and it governs the SIDE buttons. The front
Left/Right have had no hold meaning since chapter-skip was retired on
2026-09-01 (`EpubReaderActivity.cpp:648-650`; `detectHeldTurnDirection` has no
callers). `docs/ui-conventions.md:76-87`: *"A screen with no Confirm hold may
fire on press, and should keep doing so (a release-fired tap feels a frame late
on e-ink)."* Measured: action at release, +106-157 ms after the press in the
script. Verified (code + sim).

**F2. Every entry to Home paints the same frame twice.** `HomeActivity::render`
always requests a second render after the first (`HomeActivity.cpp:500-503`);
when no thumbnail needs generating the second frame is byte-identical — the
trace's framebuffer hash matched on every Home entry in every run (4 of 4). On
the X4 that is a ~500 ms waveform that changes nothing, and the next cursor
press waits behind it. Verified (sim hash).

**F3. First Home entry after opening a book generates the cover thumbnail
in the foreground,** drawing a popup and a progress bar (2 more FAST
waveforms) and decoding the full 1400x2100 JPEG (`HomeActivity.cpp:68-96`).
Verified (sim). Device decode time unknown; a full-size JPEG on a 160 MHz C3 is
the kind of thing that takes seconds (estimate).

**F4. The idle prewarm of the next page is skipped for most of a first
read.** It requires `!section->isBuilding()` (`EpubReaderActivity.cpp:272`),
and the reader builds a chapter lazily, 5 pages ahead
(`EpubReaderActivity.h:144`), so a chapter is "building" until the reader nears
its end. Measured: prewarm ran on 6 of 40 turns (only after chapters
finished) with the built-in font, 2 of 12 with an SD font. With an SD font
every other turn then reads glyphs from the card on the critical path
(~29 seeks, `FCM prewarmCache(SD)` in the turn's own render). Verified (sim).

**F5. Page loads are unbuffered: ~460 read calls per turn.**
`Section::loadPageAt` hands a bare `HalFile` to `Page::deserialize`
(`Section.cpp:1062-1078`, `Page.cpp:252`); `serialization::BufferedFileReader`
exists (`lib/Serialization/BufferedFile.h:83`) and is used only by
`BookMetadataCache`. This is the read-side twin of the write fix in
`docs/performance-indexing-2026-08-23.md` (398 → 1.3 write calls per page).
Plus `saveProgress` opens the same section file twice more per turn
(`Section.cpp:1225`, `:1295`) — that doc's item 3, still true. Verified
(sim counts + code). Device saving: estimate only; the panel dwarfs it.

**F6. Anti-aliasing doubles the waveforms of every text page, and its
waveform has never been timed.** BW refresh, then `displayGrayBuffer` with the
0xCC custom LUT (`Ssd1677Driver.cpp:501-508,746-779`), then a cleanup write.
The overlapped turn (`EpubReaderActivity.cpp:1835-1840`) hides only the gray
planes' CPU work under the BW waveform; it hides nothing before first ink.
`docs/grayscale-fast-refresh-spec-2026-09-14.md:571-577` records that no
gray-waveform duration exists. Verified (code); duration UNKNOWN.

**F7. Image pages run 3-5 waveforms.** Placeholder FAST if a decode is
needed, HALF if a clean base is pending, then a double FAST, then gray
(`EpubReaderActivity.cpp:1849-1880`), and the *next* page is forced to HALF
(`:1888`). Deliberate (ghosting fix #2190, pablohc's technique). Verified (sim:
5 panel calls on the title page, HALF on the following turn).

**F8. Wake from sleep pays ~750 ms of fixed waits plus two HALFs on the X4.**
`delay(250)` before `Serial.begin` is compiled into `gh_release` too
(`ENABLE_SERIAL_LOG`, `platformio.ini:313`; `main.cpp:959`); a 500 ms settle
loop runs on every Power wake only to detect recovery mode
(`main.cpp:1067-1075`); the X4 shows the sleep frame + loading icon with a
blocking HALF (`main.cpp:1139-1145`, the X3 uses a differential FAST), then the
first page is another HALF (`ReaderActivity.cpp:35-36`). Verified (code).

**F9. Opening a book from Home is always a HALF first page** (+1.16 s over a
FAST), because `allowFastInitialRefresh` is set only on the X3 wake path.
Verified (code + sim).

**F10. The first press after 1 s idle can wait up to 50 ms.** The ADC-ladder
buttons are not light-sleep wake sources (`HalPowerManager.cpp:210-213`), the
idle slice is 50 ms (`HalPowerManager.h:61`), and debounce adds ~one 10 ms
poll. A reader is almost always idle when they turn a page. USB disables light
sleep (`HalPowerManager.cpp:198-204`), **so a USB-tethered measurement hides
this**. Verified (code); device feel unconfirmed (`TODO.md:279`).

**F11. Two panel speedups ship in the SDK, off.**
`-DFREEINK_X4_FAST_DU_SHORTCUT` (the 0x1C differential path, "~85 ms/refresh
win", `Ssd1677Driver.cpp:240-266`) and `-DFREEINK_X4_OVERCLOCK_SPI` (40 MHz,
"~17-20 ms/refresh faster", `BoardConfig.h:836-845`). Neither appears in
`platformio.ini`. Both carry the SDK's own warning: validate on your panel
across temperature and long sessions. Verified (code).

**F12. The font picker renders its specimen twice per move and runs a gray
waveform per move** (perf doc item 7, still true: `FontSelectionActivity.cpp:856`
→ `:614`, and `:926`). Verified (sim: FAST + gray per row).

### Checked and found clean

- **No double-press windows.** No path holds a single press waiting for a
  second (grep for multi-tap/press-count/`_WINDOW_MS`; the only hit,
  `TOUCH_HELD_OVERRIDE_WINDOW_MS`, never delays a fire).
- **Home cursor moves:** press-fired, 1 FAST, zero storage calls (sim). The
  cover is restored from a RAM snapshot (`HomeActivity.cpp:414`); perf doc
  item 6 (cover header opened per redraw) is not live — only `LyraSixTheme` is
  built and it gates the open on `!coverRendered`.
- **Settings, file browser, chapter list renders:** no storage or JSON calls.
- **Activity transitions** run in the same frame; the input swallow on a swap
  delays nothing.
- **CPU clock:** an input edge restores full clock before the activity loop
  runs (`main.cpp:1385-1388`), and the render task's power lock restores it too.
- **The render task** wakes on a notify with no coalescing delay
  (`ActivityManager.cpp:51,87-93`).
- **Host CPU per action is not the problem:** 0.2-5 ms from release to the
  panel call for every action measured, chapter boundaries included.

## The plan

Ordered by expected device saving per unit of risk. "Sim-provable" means the
change can be proved in the simulator with the trace below (panel-call count,
SAME-FRAME, storage calls, press-to-call time); "glass" means only the device
can judge it (ghosting, feel, waveform time).

### Phase 0 — measure on glass first (one session, gates Phases 2-3)

| # | What | Why |
|---|---|---|
| 0.1 | **Time the gray (AA) waveform** on the X4, X3 and Picco. The `[ERS] Page render (tiled): ... gray_display=…ms` line already exists (`EpubReaderActivity.cpp:2046`); it is `LOG_DBG`, so it needs a `LOG_LEVEL=2` build — the Picco env already is one (`platformio.ini:402`). | F6: it runs on every page and is the largest unknown in the turn. Decides whether 2.2 is worth building. |
| 0.2 | **Add `CMD:PRESS:<button>` to the serial command handler** (`main.cpp:1364`, beside `CMD:SCREENSHOT`) so a USB script can drive 50 turns and capture the timeline without a human. | Makes every later device claim re-runnable. |
| 0.3 | **A battery run for idle latency**: stamp press-edge → first-panel-call into the RTC log ring, read it back over USB afterwards. | F10 is invisible on USB. |
| 0.4 | From the same logs, **the device/host CPU ratio** for the render lines. | Converts every host number here into device milliseconds. |

### Phase 1 — firmware only, no panel risk, sim-provable

| # | Change | Expected saving (device) | Proof |
|---|---|---|---|
| 1.1 | **Page turn on press** for the front buttons (keep release for anything with a hold meaning). Follows the written convention (F1). | the click's hold time, **~80-150 ms on every turn** (estimate) | trace: action at btn-down |
| 1.2 | **Skip Home's second render when nothing changed** (F2). | **~500 ms per Home entry**, and the first cursor move no longer queues behind it | trace: no SAME-FRAME on Home |
| 1.3 | **Move cover-thumbnail generation off Home's foreground**: generate it when the book is indexed (the reader has the epub open then) or in an idle tick, with no popup frames (F3). | 2 waveforms (~1 s) + the JPEG decode on first Home entry | trace: 1 FAST on first Home entry |
| 1.4 | **Let the idle prewarm run during a build** when the next page is already laid out (`nextPage < section->pageCount`); `loadPageDuringBuild` already serves mid-build pages (F4). | SD-font glyph reads (~29 seeks) off ~85% of first-read turns | trace: prewarm on ~all turns; turn reads drop |
| 1.5 | **Buffer `Page::deserialize`** through `BufferedFileReader`, and **reuse one open** for `saveProgress`'s two lookups (F5). Section output must stay byte-identical (reader-only change, no `SECTION_FILE_VERSION` bump). | ~460 → a few read calls per turn; 2 fewer opens | trace: reads/turn; page render pixel-identical |
| 1.6 | **Wake: skip the 250 ms serial delay when USB is not connected**, and **shrink the 500 ms recovery-chord settle** (sample `BTN_UP` early, or only wait the full window when it reads pressed) (F8). | up to **~750 ms per wake** | code + device stopwatch |
| 1.7 | **Font picker: render the specimen once per move** (F12). | CPU only; the gray waveform stays | trace |

### Phase 2 — panel behavior (device-validated; each one is an owner pick)

| # | Option | Expected saving | Risk |
|---|---|---|---|
| 2.1 | **Windowed refresh for list cursor moves**: mark dirty only the old and new rows instead of `clearScreen`'s whole panel, so the existing windowed path runs (`GfxRenderer.cpp:2340-2354`). | menus: ~500 ms → shorter per move; `docs/notes-and-claude.md:257-260` saw 141 ms refreshes for small work | partial-window ghosting; needs eyes on glass |
| 2.2 | **Deferred anti-aliasing**: show the BW page, start the gray pass only after the reader rests (e.g. 300 ms), cancel it if another press arrives. Rapid flipping costs one waveform per page; a resting page still ends up anti-aliased. | the whole gray waveform on every turn but the last of a run (size from 0.1) | the brief BW-only look; ghosting of an interrupted overlay |
| 2.3 | **X4 fast-DU shortcut** (F11). | ~77-85 ms per refresh, every refresh | SDK warns of ghosting/blotching on some panels |
| 2.4 | **X4 SPI at 40 MHz** (F11). An AA turn writes ~4-5 planes of 48 KB. | ~17-20 ms per plane, ~80 ms per AA turn (estimate) | out-of-spec clock; glitched plane writes on marginal wiring |
| 2.5 | **Book open from Home as FAST, not HALF** (F9), like the X3 wake path already does. | **~1.16 s per book open** | ghosting from the Home screen under the first page |
| 2.6 | **X4 wake as a differential FAST** (restore the RAM baseline the way the X3 does, `main.cpp:1132-1144`). | ~1.16 s per wake | same as 2.5 |
| 2.7 | **Fewer waveforms on image pages** (F7). | 1-3 waveforms per image page | the #2190 ghosting this sequence exists to prevent |

### Phase 3 — architecture

| # | Option | Notes |
|---|---|---|
| 3.1 | **Picco: pre-render the next page (BW + both gray planes) into PSRAM while idle**, so a press is only SPI writes + waveform. | The Picco has PSRAM (`docs/picco-port-2026-11-05.md`); the X4/X3 have one 48 KB framebuffer and no room (`docs/reading-path-heap-budget-2026-09-10.md`). |
| 3.2 | **Shorter light-sleep slice** (50 → 20 ms) or an ADC-ladder wake path (F10). | Trades idle current for first-press latency; needs the 0.3 battery measurement first. |
| 3.3 | **List cursor moves on press** (Settings, file browser, chapter list, Recent Books — all release today because a hold pages the list). Home already does press + hold-to-repeat with the same `ButtonNavigator`. | A behavior change, so the owner rules it. |

### Phase 4 — keep it fast: a gate, not a paragraph

Turn the trace into a simulator test that fails when a scripted tour regresses:
a SAME-FRAME refresh anywhere; more than 2 panel calls for an ordinary page
turn; more than N storage reads per turn; an action that fires on release where
the convention says press. Every finding above was invisible until counted, and
each one would come back silently.

## Recommended first moves

1. **0.1 and 0.2 on the Picco** (already LOG_LEVEL=2 and on USB): the gray
   waveform time decides 2.2.
2. **1.2, 1.1, 1.4, 1.5** — sim-provable, no panel risk, and between them they
   cover every page turn and every Home entry.
3. Then one owner pick from Phase 2, starting with **2.2 (deferred AA)** if 0.1
   says the gray waveform is large, else **2.1 (windowed menu refresh)**.

## Method (re-runnable)

The trace is a simulator patch, kept here rather than committed to the
simulator: [`docs/data/sim-latency-trace-2026-11-07.patch`](data/sim-latency-trace-2026-11-07.patch)
(new `src/SimLatencyTrace.h`; hooks in `HalGPIO.cpp` input injection,
`HalDisplay.cpp` refresh/gray calls, `HalStorage.cpp` open/read/seek/write).
`CROSSPOINT_SIM_LOG_LATENCY=1` arms it; unarmed it is one latched branch per
hook. Each `[lat]` line carries `micros()`, the event, and the storage calls
since the previous line; a refresh line also carries an FNV-1a hash of the
framebuffer.

```bash
# isolated worktrees so the shared trees and their .pio are untouched
git -C ~/src/crosspoint-reader worktree add --detach $W/crosspoint-reader HEAD
git -C ~/src/crosspoint-simulator worktree add --detach $W/crosspoint-simulator HEAD
git -C $W/crosspoint-simulator apply ~/src/crosspoint-reader/docs/data/sim-latency-trace-2026-11-07.patch
cd $W/crosspoint-reader && git submodule update --init --recursive
PLATFORMIO_BUILD_FLAGS="-O2 -g" pio run -e simulator     # -O0 is the native default
# card: books/anne.epub (from ~/src/crosspoint-books), .crosspoint/state.json
# {"openEpubPath":"/books/anne.epub","showBootScreen":false} and a recent.json
# naming it. A FRESH COPY EVERY RUN: progress and caches persist.
tools/latency/run_scenario.sh $CARD run.log "3000:RIGHT:150;3900:RIGHT:150;...;QUIT"
```

`tools/latency/summarize.py` prints one line per press. Script holds are 150 ms
(80 ms drops presses, per the simulator scripting notes).

### Limits of this survey

- **Host CPU and SD are not device CPU and SD.** The trace's transferable
  numbers are panel-call counts, refresh modes, SAME-FRAME repaints and storage
  call counts; host milliseconds only rank CPU work.
- **The simulator's panel is instantaneous**, so waveform overlap, BUSY waits
  and input queued behind a refresh do not show; device panel times are the
  cited X4 measurements.
- **X4 profile only.** The X3 (UC8253, differential FAST on wake) and the
  Picco (touch, PSRAM) were read in code, not run.
- One book, one SD font, default settings; dark mode, which disables the async
  overlap (`FreeInkDisplay.cpp:580-582`), was not run.
- Touch gestures were read, not scripted: Picco taps fire on lift-off
  (`PiccoInput.h:163-181`) and release-edge consumers one loop later
  (`HalGPIO.cpp:157-163`). So 1.1 and 3.3 shorten taps on the Picco too, by
  about a loop rather than a hold.
