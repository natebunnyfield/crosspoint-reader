# Fork ecosystem survey, third pass — 2026-11-07

Builds on [fork-ecosystem.md](fork-ecosystem.md) (2026-08-15),
[fork-audits.md](fork-audits.md) (2026-08-19),
[matcha-heap-audit.md](matcha-heap-audit.md) and
[ecosystem-survey-2026-09-14.md](ecosystem-survey-2026-09-14.md) (non-forks,
the SDK, SnailOS). Nothing recorded there as taken, ruled out or rejected is
re-proposed here. Upstream sync process: [fork-sync.md](fork-sync.md) (not
edited by this pass).

**Surveyed** 2026-11-07/08 from branch `lag-reduction` at `22eaac906`
(the tree moved to `9263d7b7a` during the pass, other sessions committing),
SDK pin `1dc07f6`, against `upstream/develop` at `818a15400` and
`Free-Ink/freeink-sdk` `main` at `ac146e4`. Nothing was changed in the source
and nothing was cherry-picked; this file is the whole output.

**What "pertinent" meant**, in order: (1) lag reduction
([lag-reduction-plan-2026-11-07.md](lag-reduction-plan-2026-11-07.md) — its
Implementation section is the baseline every lag candidate was compared
against); (2) the BOOX Picco port
([picco-port-2026-11-05.md](picco-port-2026-11-05.md)); (3) Picco gestures
([picco-gestures-2026-11-07.md](picco-gestures-2026-11-07.md), rejected passes
2/3/3b not re-proposed); (4) P0/P1 crash and data-loss; (5) upstream sync.
Deleted subsystems (KOReader sync, Calibre/OPDS, status bar, bookmarks, auto
page turn, reader menu, tabbed Text Settings, extra themes, PDF, stats,
dictionary, plugins, apps, tilt) and X4 Pro-only work were marked N/A.

**How sure.** Every TAKE below was read as a diff and checked against our code
with grep + read; the `file:line` citations are ours. Claims marked INFERRED
were reasoned, not traced or reproduced. Nothing here was built, run in the
simulator, or seen on a device — every lag saving is an estimate and every
panel effect is UNCONFIRMED on glass.

---

## 0. Owner rulings, 2026-11-08

Owner, answering the open items in one line: *"no quick panel, yes to loading
icon, yes to faster, albo issue is what?, yes to any bug fixes"*.

| # | Ruling |
|---|---|
| 3 | Already landed: the other session hand-ported upstream `b6746c1df` (CSS cache retry, `CSS_CACHE_VERSION`), `fork-sync.md` batch 3. |
| 9 | **KEEP the loading icon on wake** ("yes to loading icon", read as yes to the icon). No change. |
| 10 | **TAKE the faster factory 4-level waveform** ("yes to faster"). Device-only to judge; ships UNCONFIRMED on glass. |
| all bug-fix TAKEs | **TAKE** ("yes to any bug fixes"), plus restoring the dark-background re-drive the SDK lost in `69349ec` (p0-p1 sweep, "Known and NOT fixed"). Each one's status goes in the table below as it lands. |

### Status of every bug-fix row, 2026-11-08

Firmware commits are named by subject (a rebase before the push changes their
shas); SDK commits are on `natebunnyfield/freeink-sdk` `main`, stable. "Failing
first" means the new host test was run against the old code and failed.

| # | Status | Where | How verified |
|---|---|---|---|
| 1 | already fixed (B-078) | `patch_sdfat.py` | p0-p1 sweep |
| 2 | already fixed (B-079) | — | p0-p1 sweep |
| 3 | already on `main` | campaign batch 3 hand-port of upstream `b6746c1df` | `CSS_CACHE_VERSION` in `CssParser.cpp` |
| 4 | **DONE** | SDK `a5ca7c5` "fix(picco): realign the frontlight table…" | upstream's smooth-ramp host test, failing first; device UNCONFIRMED |
| 5 | **DONE** (taken early, upstream `149dd53299`) | "fix(epub): chunk the zip size lookup so huge spines do not abort" | host `LargeSpine.*` (2,500 chapters, matched 2500/2500); the abort itself needs device heap |
| 7 | already on `main` | campaign batch 4 `3767b412a` (upstream `93b6fe111`) | — |
| 11 | **DONE** (taken early, upstream `10cb2c3b29`) | "fix(reader): loop() reads section only under a tried render lock; turns queue to the render task" | lag gate OK; simulator `semphr_reboot` gains the try case (the old shim hangs on it) |
| 12 | **DONE**, early-stop half only (upstream `f8936ba67c`) | "fix(reader): a press stops the background chapter build at its next parse step" | X4 build; lag gate OK; device feel UNCONFIRMED (the Picco and the simulator answer false and wait for `update()`) |
| 15 | already on `main` | campaign batch 4 `5c7b00246` (upstream `c4d8c395d`) | — |
| 16 | **DONE** | "fix(store): refuse a save whose JSON lost members or was cut short" | host `JsonSaveGuard.*`, overflow case failing first |
| 17 | **DONE** | "fix(epub): cap the stylesheet list an OPF or zip can grow" | host `CssManifestCap.*`, failing first (5000 vs 256) |
| 18 | NOT taken | — | a visual tuning plus a new image waveform, device-only, not a bug fix |
| 19 | **DONE** (no filter) | SDK `55aeccb` "fix(picco): battery ADC median of three…" | SDK host test, 5 checks failing first |
| 20 | **DONE** | SDK `2e943b7` "fix(picco): a new TMA525C track id ends the old contact…" + `e05bb17` "a two-finger touch is spent until every finger lifts" (review finding: `2e943b7` alone released the first finger when a second was listed first, and the firmware decoder then fired a tap) | SDK host test: the lost-lift case fails on the old SDK, the two-finger case fails on `2e943b7`; device feel UNCONFIRMED |
| 22 | already on `main` | `f9be57963` (upstream #3847) holds `modeMutex` across check and switch | read `HalPowerManager.cpp:43-80` |
| 23 | **DONE** | same commit as #11: page turns queue to the render task, idle work reads `section` only under the lock | lag gate (14 turns, pages 1..14 in order) and a 4-chapter run crossing spines both ways |
| 28 | **DONE** | SDK `9148674` "fix(x4): UC8279 LUT_VER 0x03/0x67…" | detect host test failing first; the 0x67 AA skip is device-only, no such panel on hand |
| dark re-drive | **DONE** | SDK `c7fc361` (reverts `69349ec`, public `setBackgroundHint`) + "fix(sdk): bump freeink-sdk…" (HalDisplay passes the hint) | `run_uc8253_power.py` asserts the re-drive; device effect UNCONFIRMED |

Rows 6, 8, 13, 14, 21, 24, 25 are performance, not bug fixes, and were not
part of this ruling (8 is already on `main` from the campaign).

## 1. What is new since 2026-08-15

| | Count | Note |
|---|---|---|
| Direct forks of `crosspoint-reader` | **1,629** (GraphQL `totalCount`) | REST `forks_count` says 2,257 — that counts the whole fork *network*. |
| Created since 2026-08-15 | 532 | |
| Pushed since 2026-08-15 (push hour ≠ creation hour) | 623 | |
| …of those, ahead of `upstream/develop` | **218** | 404 are 0 ahead; 1 (`KIddoKg/crosspoint-reader-kiddo`) has no common ancestor |
| Non-merge commits since 08-15 on those 218, not on upstream | 12,116 (11,351 unique subjects excluding ours) | default branches only |
| Second-level forks (forks of forks) | 587 under 52 parents; 333 pushed since 08-15 | CrossInk alone has 272 forks |
| Their commits not on upstream nor on their parent | 9,940 | |
| Keyword hits read by subject | ~2,400 first-level + 477 second-level | regexes in §7 |
| `Free-Ink/freeink-sdk` forks | 174 direct | our pin is **61 ahead / 481 behind** `main` |
| `crosspoint-simulator` forks | 49 | §5 |

**Correction to the 2026-08-15 method.** That survey recorded "the API stops
paging at ~1,100 of the 1,443". The likelier explanation, measured now: REST
paged **all 1,632 direct forks** (the four `sort=` orders return the identical
set), and GraphQL's `totalCount` agrees (1,629); the larger number is
`forks_count`, which counts forks of forks. So the direct list is complete; the
gap is the second and third levels, which this pass enumerated separately.

**Also measured:** this branch is **950 ahead / 466 behind** `upstream/develop`
(merge-base `1a7f5a9e3`, 2026-07-31; `git cherry` finds only 7 of the 466 by
patch-id, but many were hand-ported — the Picco ones, for example).

### The forks that matter now (new or newly active)

| Fork | New commits since 08-15 | What it is, for us |
|---|---|---|
| `uxjulia/CrossInk` — **branch `development`** | 782 | Still the centre. `main` has not moved since 10-04; work is on `development` and `release/v2.0.0`. `Unintendedsideeffects/ForkDrift-crosspointReader` and `wildfire070/InkCap` largely mirror it. Picco support landed there (`2c0d70567e`, 11-04). |
| `eszter007/matcha-reader` | 1,106 | Still the disciplined heap/latency fork; now Picco-aware (`18ee2c8a51` publishes a Picco build). |
| `Belphemur/Xerif-eReader` (new, 08-25) | 504 | TTF engine, PSRAM staging, background image decode; carries a Picco release matrix. |
| `TenorGroup/cross` (new, 09-26) | 207 | Input latency during refresh, SdFat free-cluster/FSInfo work measured on an X3. |
| `InsiderPhD/crosspoint-reader` | 210 | One-day perf sweep (10-11) — buffered I/O, glyph raster, popup double-refresh. |
| `0x1abin/crossmux` | 303 | ReadPico (an S3 + i80 board, **not** the BOOX Picco) — idle AA page cache. |
| `homunet/crosspoint-reader-jp` (+ its SDK fork) | — | Factory 4-level waveform for text; one-pass gray planes. |
| `jpirnay/freeink-sdk` (SDK fork) | — | One-pass anti-aliased text on X4 and X3. |
| `alyapin/crosspoint-reader` (new, 10-28) | 417 | Touch gestures, light sleep, presses during refresh. |
| `prokrypt/CrossInk` | 595 | X4 Pro light sleep, draw-ahead, swipe dead zones. |
| `lidiaev/crosspoint-plus` | 83 | Typing-latency lab (X4 Pro UC8279 only). |

Out of scope by volume, recorded so they are not re-read: `ma-r-s/crossplay`
(1,418 — games/apps hub), `CarpeTelam/crosshatch-player` (Lua games),
`jerji/crosslua`, `rfordinal/explorink` (T5S3 + LoRa map; its refresh-pricing
docs are the only reusable part), `hiitsalice/minuta`, `beatrizq/crosspoint-planner`,
`crossrose-reader/CrossRose`, `3r1pm4V/CrossImple`, `yazdipour/CrossXT`,
`AarynSmith/CrossInk-GrimmorySync`, the RSS/Anki/AO3/chess/calendar forks.

---

## 2. Ranked shortlist

`TAKE` = read, applies, small. `TAKE+` = take with changes. `OWNER` = an
architecture or style pick (present options before building, house rule).

| # | Verdict | Source (fork, sha) | What, in one line | Effort | Our evidence |
|---|---|---|---|---|---|
| 1 | **TAKE** (P0) | upstream `e9245489f2` patch 0001; also InsiderPhD `dbc1d1e756`, endqwerty `d60b5534d0` | A failed SdFat sector read leaves `FsCache` claiming the old sector over a clobbered buffer; a later write puts wrong bytes (FAT sectors included) on the card. One `invalidate()`. | data loss | `.pio/libdeps/default/SdFat/src/common/FsCache.cpp:40-43` returns without invalidating; `scripts/patch_sdfat.py` has no such block |
| 2 | **TAKE** (P0, ours) | found while checking Xerif `26d66ca2f9` | `drawTextRotated90CCW` has no font-cache scan guard, so the idle prewarm's scan of page N+1 DRAWS rotated-table text into the live framebuffer showing page N. Shows up in anything presented without a redraw: quick-resume sleep frame, "Entering sleep" popup, the Indexing popup's tile window. | lag work regression | `lib/GfxRenderer/GfxRenderer.cpp:3149` (no guard) vs `:761` (`drawText`) and `:906` (`drawLine`); caller `lib/Epub/Epub/Page.cpp:92`. Not reproduced; real books with rotated tables INFERRED |
| 3 | **TAKE+** (P1) | ForkDrift `fbc42a088a`, upstream `b6746c1dfe` | A CSS parse cut short (heap gate, temp-file I/O failure, failed `loadFromStream`, the 32 KB mid-parse floor) is saved as the book's CSS cache and preferred forever. Take upstream's shape: complete/partial flag + `CSS_CACHE_VERSION` bump + retry next open. | cache correctness | `lib/Epub/Epub.cpp:416-419, :435-452, :455` (result ignored), `:462` saves unconditionally; `Section.cpp:634` builds only from that cache. Cache-format change → owner |
| 4 | **TAKE** | Picco SDK `1c141b7`→`e8ab635`→`471ccf6` (Free-Ink, as one unit) | **Picco frontlight table is misaligned by two entries in our pin (upstream #167)**: full brightness is never reached. Also smoother steps and no warmth colour jump. Take the `FrontlightManager.cpp` hunks + host test. | Picco | `freeink-sdk/libs/hardware/FrontlightManager/src/FrontlightManager.cpp:187` — row "colorTemp 0" ends `136,0,31,0` and row 1 starts `157,0,31,0,177,0,…`, i.e. the vendor rows shifted by 8 bytes. e8ab635's last two warm-only steps are extrapolated, not stock data |
| 5 | **TAKE** | upstream `149dd53299` | Books with thousands of chapters abort the index build (16 B/spine item built for the whole spine; a deque growth aborts under -fno-exceptions). Chunk by 2,048. | P1 abort | `lib/Epub/Epub/BookMetadataCache.cpp:312-336`; `ZipFile.cpp:316-375` writes by index so the chunking ports unchanged |
| 6 | **TAKE** | kimchi `dca60d87a7`, aurora `de36df38b6` | One page walk per gray plane instead of six strips per plane. The deferred AA pass always takes the 12-walk strip path, because plane buffers are only allocated on the overlapped path; allocate them when they fit (PSRAM on the Picco). | AA cost / lag | `src/activities/reader/EpubReaderActivity.cpp:2051-2059`, `:2082`, `:2117-2147`; prerender already uses a full-height band (`:2306`). Saving unmeasured |
| 7 | **TAKE+** | upstream `93b6fe111c` | When the ~10.7 KB glyph arena fails on a fragmented heap, every glyph goes to the 16-slot overflow path (SD read per glyph). Port the retry-with-prefix (~30 lines). | SD I/O on the page path | `lib/EpdFont/SdCardFont.cpp:1280-1286`; the state is the one recorded at `:144-147` |
| 8 | **TAKE+** | upstream `e33e3cf39d` (+ crossmux `03a9c7ce4d`, InsiderPhD `0ec8c9cd40`) | `USE_SPI_ARRAY_TRANSFER` is 0, so SdFat clocks every SD byte as its own `transfer()`; upstream reports ~3x on reads. Its send path puts 512 B on the stack — run `tools/stack_budget` first. No effect on the Picco (SDMMC). | SD I/O / lag | `.pio/libdeps/default/SdFat/src/SdFatConfig.h:184-192`, `SdSpiLibDriver.h:57-60, :88-96`; `platformio.ini` does not set it |
| 9 | **OWNER** | upstream `f7eedbae18` (CrossInk port `891a06f7f6`) | Drop the loading-icon refresh on quick-resume wake: the X3 restores its baseline with no refresh, the first page carries the change. One waveform fewer per wake; on the SSD1677 the initial-full promotion then lands on the first page instead. Cost: no "wake registered" mark. | wake latency | `src/main.cpp:1163-1172` draws `LoadingIcon` and refreshes FAST |
| 10 | **OWNER** | homunet SDK `79b5439e` + firmware `36dc1f67d6`/`cdbda5558f` | Factory 4-level waveform with frame-rate byte 0x22→0x88 (they measured 1,084→336 ms on an X4) — text pages straight to AA in one refresh (~0.65 s) vs our BW + deferred gray. Competes with deferred AA; needs a visual comparison and a cold test, behind an opt-in flag. | AA / lag | our factory LUT path `freeink-sdk/.../Ssd1677Driver.cpp:747-770`, `:755` |
| 11 | **TAKE+** | upstream `10cb2c3b29`, InsiderPhD `db26a8afef` | The reader's idle work checks the render lock is free, then takes it BLOCKING; a render grabbing it in between stalls the main loop (and press sampling) for a whole render + refresh. Add a try-lock. | input latency | `EpubReaderActivity.cpp:310/315, 365/368, 392/395, 430/431, 2267/2296`; `RenderLock.h` has no try mode. Window narrow (INFERRED) |
| 12 | **TAKE** (part) | upstream `f8936ba67c` | Stop a background build step when a button goes down (`buildSomeMore` runs under the lock and cannot stop). Take only that; the deque half is a >1,024-page heap fix, and "finish in idle" costs heap and CPU. | input latency | `EpubReaderActivity.cpp:403`; `Section.h:51` still vectors |
| 13 | **TAKE** | TenorGroup `4912609e48` (write-deferral part) | Book open writes `APP_STATE` and `RECENT_BOOKS.addBook` (which stats up to 10 books on the card, then writes JSON) BEFORE the first page; move after first ink, skip when unchanged. | book-open latency | `EpubReaderActivity.cpp:188-190`; `RecentBooksStore.cpp:46, :117-121, :63`. Tens of ms INFERRED |
| 14 | **TAKE** | upstream `38280863a5` | Reopening a book erases and re-inserts its recent entry, fresh strings every open; `std::rotate` instead. Same file as #13. | fragmentation | `src/RecentBooksStore.cpp:48-61` |
| 15 | **TAKE+** | upstream `c4d8c395dc` | Release the SD-font mini arenas and overflow ring (not advance/kern tables) before a chapter build. | P1 under pressure | `Section.cpp:485` releases nothing; only the web server calls `releaseSdFontCaches` (`CrossPointWebServerActivity.cpp:103,325`) |
| 16 | **TAKE** | endqwerty `d60b5534d0` (PersistableStore part only) | `writeDocToFile` checks neither `doc.overflowed()` nor the serialized length; under OOM a key-less document overwrites good settings atomically. | data loss | `src/PersistableStore.cpp:58` |
| 17 | **TAKE+** | wildfire `8c98d7808b` | CSS file lists grow without bound from the OPF, then are copied and used to size a vector + map. Cap to the device (theirs is 65,535). | P1 abort | `ContentOpfParser.cpp:239`, `Epub.cpp:282, :148, :380-398` |
| 18 | **TAKE+** | Picco SDK `daa11e8`, `684ea60` | SSD2677: drive every incoming black pixel fully before an AA base (needs a firmware hint so ordinary turns are untouched); and the slot-4 BOOX image waveform for four-shade pictures. Device-only. | Picco | absent from `freeink-sdk/.../Ssd2677Driver.cpp:161-176` |
| 19 | **TAKE+** | Picco SDK `19767de` | Median-of-three battery ADC, the duplicated 100%/95% anchor fix, one-read charger decode (skip its filter; the firmware averages). | Picco | `BatteryMonitor.cpp:276-288`; firmware average `HalPowerManager.cpp:410-415` |
| 20 | **TAKE+** | Picco SDK `2264d13` (the half not taken) | Single-touch guard only: when the TMA525C track id changes mid-press, emit release + press; count > 1 counts as moved past tap slop. This is the "new finger inherits a stale contact" case the gesture bench models. | gestures | our decoder reads only `rec = &buf[7]` (`freeink-sdk/.../InputManager.cpp:1228`). Guard INFERRED; gesture half rejected (single-touch ruling) |
| 21 | **TAKE+** | crossmux `75f42bd1e3` + `cf821fbccc` | ReadPico idle cache of all three AA images (BW + LSB + MSB) so a turn is copies + waveform (~12 ms vs ~700 ms render, theirs). Extends our Picco prerender, which is BW only. Also: they retire the cache with one render-epoch counter, not a key list — sturdier than our 7-field key. | Picco lag | `EpubReaderActivity.cpp:1868-1871` (key), `:2290` (one PSRAM buffer). Pairs with deferred AA, so the gain is the gray pass's CPU only |
| 22 | **TAKE+** | matcha `6582faa808` (premise) | `setPowerSaving` reads the lock mode without the mutex and switches frequency unserialized; a render can start at 10 MHz with its lock held. Hold `modeMutex` across read + switch. | lag, rare | `lib/hal/HalPowerManager.cpp:43-71, :419-441` |
| 23 | **TAKE+** | ForkDrift `3a9c9c5667` (the race, not #822) | `loop()` reads `section` unlocked and `pageTurn()` dereferences it while the render task can reset it on failure paths. Queue the turn to the render task rather than locking (a lock would block input for a render). | P1 crash, rare | `EpubReaderActivity.cpp:713, :1096, :1112` vs resets at `:1274, :1316, :1336, :1458, :1466, :1477, :1542`. Reachability INFERRED |
| 24 | **TAKE** | upstream `87f9c34c62` | ZIP central directory read in one call per entry (~13 today), with a 335-line host test. Chapter open/build, not page turns. | I/O | `lib/ZipFile/ZipFile.cpp:114-192` |
| 25 | measure first | TenorGroup `e886964777` (+ `beddc01c6c`, `acd8e5ae16`, `241322f0fb`) | SdFat's free-cluster search restarts at cluster 1 every mount and is pulled back by every free, so the first new file after boot/wake can read thousands of FAT sectors (3.3–4.3 s on their X3). `e886964777` is contained; the FSInfo three add a write path SdFat never had. Time the first `.tmp` write after boot on the owner's card before choosing. | boot/wake lag | `.pio/.../FatLib/FatPartition.cpp:403, :317-319` |
| 26 | **OWNER** | slink2111 `77021b6b35` | 1-bit edge dithering (2x2 Bayer in the BW pass) instead of a gray waveform: AA-ish edges in one refresh. A type-style decision; needs a render. | AA | — |
| 27 | **OWNER** | jpirnay SDK `8f0a81ec`, `f798c312` (X4), `20c38bad`, `1fa6fa42` (X3) | One-pass anti-aliased text: one refresh per AA page. Needs two frame buffers (`beginOnePassMask` uses `frameBufferActive`), so null on our single-buffer builds; a redesign would stream the mask into SSD1677 RED RAM by strips. The best panel-side lag lead found, and a project. | AA / lag | all envs build `-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1` (`platformio.ini:106`) |
| 28 | **TAKE+** (optional) | Free-Ink SDK `1ff1e92` | UltraChip X4 batches reporting LUT_VER 0x03/0x67 go to the wrong driver here; take the detection + `selectAaLuts` hunks plus a fork-side AA-off for 0x67. Not the owner's SSD1677 X4. | crash-adjacent | `XteinkDetect.cpp:419-430`, `Uc8279X4Driver.cpp:86-91` |

Smaller, cheap, optional (read; none fixes a crash we can reach): allocate the
`HalFile` wrapper before an `O_TRUNC` open (`HalStorage.cpp:141-153`, endqwerty);
check the `f.close()` result in the SDK's `writeFile` (`SDCardManager.cpp:327-329`,
fragment of `2ab0b6f`); keep a corrupt `settings.json` aside instead of
overwriting it with defaults (`PersistableStore.cpp:130-145`; InsiderPhD's
`.bak`-per-save is rejected, it adds a rename to every save against
[sd-card-corruption-2026-10-04.md](sd-card-corruption-2026-10-04.md)'s goal);
fix the SDK comment at `SDCardManager.cpp:90` (`FsVolume::end()` flushes
nothing); free the glyph arenas once when a background build pauses on low heap
(dead band between `SdCardFont.cpp:149` 12 KB and the build's 16 KB pause,
`EpubReaderActivity.h:161` — INFERRED); X3 `usbDetect` unassigned
(SDK `edcc629`, latent: nothing reads it on X3).

Leads, not candidates (recorded so they are followed up rather than
rediscovered): a **VCOM trial on the X4** — Free-Ink `e91e34e` changed only the
Metalio LUT but its comment says 0x30 ghosted on sleep covers, and the X4's
`lut_factory_quality` is still 0x30 (`Ssd1677Luts.h:108`); **black-pulse
partial refresh** (`629b44a`, Metalio-only flag) as a non-flashing HALF; on the
X4 with the fast-DU shortcut the 0xCC gray pass marks the screen off
(`Ssd1677Driver.cpp:519`), so the next FAST resends power-on bits — cost
unknown; the Picco's power-on/off BUSY waits may burn up to 100 ms per page if
BUSY never asserts (Free-Ink `b53dc34` kept the Picco off its faster wait);
upstream's unmerged `fix-display-updates` branch `5c406229` "SSD2677 Picco
night-mode refresh handling" (the slide switch flips dark mode); the X3 UC8279
batches have no windowed refresh (`PanelDriver.h:60-62`), so tile windows and
note-editor typing fall back to whole-panel FAST there — lidiaev's X4 Pro
UC8279 lab is the reference if that is ever built.

**Do NOT take, and why it is a trap:** Free-Ink `c27da30` (2026-11-02,
"diagnostic rollback" of jpirnay's UC8253 X3 driver). It reverts `08073b3` and
also the split refresh, the interrupt-driven wait and the single initial full
sync that our pin still has (`Uc8253X3Driver.cpp:151, :160-231`) — taking it
puts X3 latency back. Watch upstream for what its "persistent residue"
investigation concludes.

---

### #10, the factory 4-level waveform: design options (2026-11-08, not built)

Owner ruled "yes to faster"; how it is built is an architecture choice, so the
options come first. Sources read: homunet SDK `79b5439e` (frame-rate byte),
firmware `cdbda5558f` (dual-plane window render target) and `36dc1f67d6` (the
reader's banded absolute path); ours: `Ssd1677Driver::displayGray`
(`factoryMode` already exists and selects `lut_factory_quality`, but nothing in
the firmware asks for it: `GfxRenderer::displayGrayBuffer()` never passes
`absolute`), and the deferred AA path in `EpubReaderActivity` (plan 2.2).

What it is: on the X4's SSD1677, render the text page straight into ABSOLUTE
4-level planes and refresh ONCE with the factory waveform, with its frame-rate
byte raised 0x22 -> 0x88. homunet measured on an X4, room temperature: factory
waveform 1,084 -> 336 ms; a whole text page ~0.65 s, against ~1.0 s for BW base
+ gray overlay. Ours today: BW FAST ~497 ms (measured), then the gray overlay
only after a 600 ms rest (`GRAY_DEFER_MS`); the overlay waveform itself has
never been timed on glass.

**X4 only in every option.** The X3's UC8253 has no absolute bank
(`HalDisplay::supportsAbsoluteGrayscale()` is false there) and the Picco's
SSD2677 is a different driver. Image pages keep today's path in all options
(homunet's does too).

| | Option | Page turn on the X4 | AA arrives | RAM per text page | Cost to build |
|---|---|---|---|---|---|
| A | Keep deferred AA (no change) | ~0.5 s to a readable BW page | ~1.1 s later, only if the reader rests | strip scratch, as today | none |
| B | Replace deferred AA with the one-refresh factory page | ~0.65 s (est.; +~150 ms per turn, also when flipping fast) | with the page, no second flash | 2 x ~9.6 KB contiguous (5 bands; more, smaller bands when the heap is fragmented, else a BW page) | high: port the window target (~150 lines of `GfxRenderer`), the band loop (~130 lines, against a reader since reworked by the lag work: prerender, tile hashes, queued turns), the SDK window write + frame-rate byte; the lag gate's 2.2 checks need an X4-mode variant |
| C | B behind a setting ("Anti-aliasing: Deferred / One refresh"), X4 only, default **Deferred** | as A or B | as A or B | as B when on | B + one setting row (two languages) |
| D | C with default **One refresh** | as B | as B | as B | as C |

Shared unknowns, whichever is picked: 0x88 shortens every drive pulse, so
contrast and ghosting in the COLD are untested (homunet: "check before using
it in cold environments"); a guard that drops to 0x22 below a panel
temperature is possible but unmeasured. Dark mode with absolute planes, and the
tile-hash windowed present after an absolute page, both need checking. The
simulator cannot judge any of it: its e-ink mode models the X3's waveforms,
not the SSD1677's factory bank.

Recommendation: **C** (a setting, default Deferred), so the owner can judge
both on his X4 before either becomes the default.

## 3. Checked — already present here, or N/A (do not re-propose)

### Lag and input (compared against the 2026-11-07 implementation)

| Candidate | Reality here |
|---|---|
| Presses dropped during a refresh — upstream `483c5cf681`, TenorGroup `613750deff`, matcha `a155f4b4cc`, Xerif `a2311a882b`, InsiderPhD `5a294bd4cf`, alyapin `f5ecffc85b` | Covered. List moves take no render lock in the loop (only directory changes, `FileBrowserActivity.cpp:218-224, :259-268`); during a panel wait the loop light-sleeps in 20 ms slices that wake on BUSY/Power (`HalPowerManager.h:73`, `.cpp:316-390`); an in-chapter turn takes no lock (`EpubReaderActivity.cpp:1096-1097`) and queued requests collapse into one render (`ActivityManager.cpp:51`). Only a chapter-boundary turn blocks (`:1101`). |
| Short presses lost after idle — upstream `9ba1608639` | Covered by the 20 ms slice (`HalPowerManager.h:68`, `.cpp:222`) plus fast re-poll while debouncing (`main.cpp:1504-1511`). Upstream samples at 10 ms; their shortest measured press was 31 ms. |
| Lists navigate on press — upstream `1f08ce9342` | Plan 3.3, done (`docs/ui-conventions.md:88-97`). |
| Panel rails off after a minute idle — TenorGroup `be0743745c` | N/A: we never drop the rails between frames on X4/X3 (`fadingFix` constexpr 0, `CrossPointSettings.h:553`; `Uc8253X3Driver.cpp:89`; `Ssd1677Driver.cpp:454, :486-489`). A timer would ADD a power-on. |
| Burst of turns as one refresh — TenorGroup `6ed2e28fa9` | Reverted by its own author in `559ac795ff` ("bought nothing"); that commit's two-page look-ahead is covered by our five (`EpubReaderActivity.h:181`). |
| Second refresh after `drawPopup` — InsiderPhD `557fc79c3c`, Jester `557817ba65` | None here; every `drawPopup` site checked (`BaseTheme.cpp:1031`), and the tile-hash skips a same-frame repaint anyway. |
| Loading popup on a current cache — InsiderPhD `4da27b3c41` | Ours shows it only when `book.bin` is missing (`ReaderActivity.cpp:56-60`). |
| Home paints FAST after a book — matcha `e7258dc35c` | Always FAST here (`HomeActivity.cpp:499`). |
| Next-page draw-ahead — prokrypt `585870b6bb`, Xerif `8afeea6f0e` | Ours is the Picco PSRAM prerender (plan 3.1). Xerif's X4/X3 variant draws into the live framebuffer and brings back exactly the hazards of finding #2 — hold until device CPU is measured. |
| Interrupt-driven idle — prokrypt `3b0383a62f` | Picco already wakes on buttons and touch INT (`HalPowerManager.cpp:235-270`). Gap: a release after a >1 s hold is seen up to 50 ms late. |
| Quick-resume wake without a full refresh — CrossInk `0a85da2a07` | Ours by another route: RAM baseline restored on every board (plan 2.6, `main.cpp:1150-1172`). |
| Defer AA / images during queued turns — CrossInk `6037033c6d`, `60e96fd5de`; jetaudio `7eb6273dc2` | Superseded by deferred AA (plan 2.2). |
| Power-button wake timing — upstream `cdd89b1d75`, `e37164ca91` | Ours takes the fast path (short Power is constexpr SLEEP, `main.cpp:1046-1047`); the cold-boot fix is for boards we do not build. |
| Low CPU clock while the panel refreshes — Jester `6ebc84417b` | Rejected: we light-sleep during panel waits, which saves more and keeps input polling. |
| Overlap gray preparation with the base refresh — Free-Ink `0dbf8cc` (UC8179) | The reader already overlaps (`EpubReaderActivity.cpp:2085-2091`); nothing more on the C3 without full-plane memory. |
| X3 AA stability — upstream `1f3d7458a7` | Not lag; our EPUB reader never calls the X3 grayscale base/precondition passes, only the XTC reader does (`XtcReaderActivity.cpp:261-266`). Worth a look on glass now that AA is deferred. |
| X3 grayscale SPI deadlock — upstream `93a14afd9c`, Free-Ink `c2d711a` | Not exposed: our UC8279 driver ends the transaction explicitly (`freeink-sdk/.../Uc8279Driver.cpp:346-350`); the bug came with upstream's RAII bus conversion, which we never took. |
| AA precondition at 60% — Xerif `1398ad81cb` | Only an SDK pointer to a Belphemur SDK commit not fetched; our only precondition waveform is the X3's, used by the XTC reader. INFERRED N/A. |
| Picco display/TLS buffers in PSRAM — glintux `2c53a7f976`, `72c91686ce`, `287e3fdba1` | Not needed: our Picco keeps the framebuffer internal (`BoardConfig.h:309`) with ~247 KB free; a PSRAM framebuffer would slow drawing. Their SDK half not fetched (INFERRED). |
| X4 Pro work — prokrypt `187b0ad8e7`; lidiaev `135b023583`, `615aa896f5`, `386c9d0249` | Out of scope (UC8279 X4 Pro driver, `Uc8279X4Driver.h:1-6`). Reference only. |
| Deleted surfaces — upstream `bb93a462cc`, `45986f29e1`, `44f78b5e6e`, `8f4d035cbc` (we have `slideSwitchAction`); CrossInk `4a39788657`; alyapin `8724fc47d1` | N/A. |

### SD, crash, data loss

| Candidate | Reality here |
|---|---|
| Sync the FAT cache before directory data — upstream `6c17b0eef6`, matcha `bdca4eeedb` | N/A: the bug needs SdFat's separate FAT cache; we use the shared one (`FatPartition.h:208-216`, `SdFatConfig.h:411-415`). **Becomes mandatory if `e9245489f2` patch 0002 is ever taken.** |
| Separate FAT-sector cache — upstream `e9245489f2` patch 0002 | Not taken: +512 B RAM, re-proving our `patch_sdfat.py` reorders, unmeasured gain. (Patch 0001 is shortlist #1.) |
| SD shut down for deep sleep — upstream `4a679d5d4b` | Present for the Picco (`HalStorage.cpp:39-44`, `main.cpp:792, :1051`); a no-op on SPI boards upstream too. |
| Flush SdFat caches before sleep cuts the card — TenorGroup `cbf0b7b9a9` | Rejected: flushing the FAT without the directory entry turns "nothing written" into lost clusters. Every writer closes before sleep. |
| Clear ligature views when SD font caches are released — upstream `06b5d5b3ae`, InsiderPhD `cba3ce22dd` | Present (matcha `9011c91fd`, taken 2026-08-15; `SdCardFont.cpp:187-197`). |
| Corrupt `.cpfont` glyph entries — upstream `c1d99e569c` | Crash half covered by the geometry check at both load sites (`SdCardFont.cpp:22-25, :1261, :1850`) and short-read checks (`:1317, :1870`). |
| Stale footnote caches — upstream `596f0d3f71` | N/A: our footnote href was never widened (`FootnoteEntry.h:6`, still 96). |
| Atomic `settings.json` — InsiderPhD `2ed70cfdf0`, CrossInk `7fc1c2366a` | Present ([T-010]; `PersistableStore.cpp:78-95`, recovery `:103-127`). Free-Ink `2ab0b6f` (temp swap in the SDK) rejected: would nest `.tmp.tmp`. |
| Power-hold sleep panic — InsiderPhD `01df2f6a05` | Present since 2026-08-21 (`ActivityManager.cpp:253-266`). |
| A card that stops answering aborts the reader — jetaudio `e7b2c122d5` | Present (`Serialization.h:49-77` bounds the length prefix). |
| Never abort on a failed allocation — BlindBat `c115022547` | Present; no bare `new` left in `lib/` or `src/` (B-030/B-031 sweeps). |
| Corrupt `book.bin` — BlindBat `db122679db` | Present: commit-point sentinel (`BookMetadataCache.cpp:227, :392-397`) and header bound (`:563-573`). |
| uzlib OOB on corrupt Huffman trees — BlindBat `7a00254a09` | Not reachable from the card: uzlib decodes only built-in fonts in flash (`InflateStream.h:12-20`, `FontDecompressor.cpp:79-81`). Optional. |
| Unbounded growth in ChapterHtmlSlimParser — wildfire `fa9dd2e8ba` | Present by another route: anchor growth heap-gated (`ChapterHtmlSlimParser.cpp:3180-3197`), table buffers byte-capped (`.h:203-207`). |
| Font-render OOB / zip offset overflow — wildfire `b5cc64412c` | Zip half needs a >4 GB file (impossible on FAT32); smaller overflows go negative and are refused. |
| Cumulative spine sizes back into `book.bin` — upstream `7365c1acd8` | Rejected: <1 KB saved on normal books, adds SD reads + a mutex on the progress path (`EpubReaderActivity.cpp:1300`, `Epub.cpp:1077-1082`). |
| SD-font cache restructure — upstream `9610da7022`; fragmentation — `c80c537f28` | Rejected: streams kern class tables from SD every page; its capacity-doubling advance table was already tried and reverted ([reading-path-heap-budget-2026-09-10.md](reading-path-heap-budget-2026-09-10.md)). Ours already frees-before-alloc, keeps a largest-block floor and underuse hysteresis (`SdCardFont.cpp:105-111, :149, :160`). |
| Batched page-position reads — upstream `1a5c8cd119` | N/A: the function does not exist; loadPage reads through a buffer since plan 1.5 (`Section.cpp:1084-1089`). |
| Low-memory ParsedText abort — CrossInk `b1f3e91dba` | N/A as written; [B-032] ruled out blanket-converting small per-line reserves. |
| 36–45 KB render refusal band — CrossInk `0f851eff4e` | N/A: no render-refusal gate here. |
| Signed overflow on BMP height — matcha `ea551e70ac` | `hasCompleteBmp` does not exist here; the similar negation at `Bitmap.cpp:108` is caught at `:126`. |
| FAT32 path mid-UTF-8 cut — InsiderPhD `1892d32f3d` | N/A: `sanitizePathComponentForFat32` has no callers. |
| matcha chapter-build heap — `2509cd1297`, `4c06305ffb` | Vertical writing/stats deleted here; the framebuffer-loan inflate is present (`EpubReaderActivity.cpp:1268`, `InflateStream.cpp:25-40`); the remaining dead band is the optional item above. |
| endqwerty SdFat pointer guard (its patch 0003) | Null-pointer arithmetic never dereferenced (`FatFile.cpp:854-885`). |

### SDK (Free-Ink) — not compiled for us, superseded, or other boards

All firmware envs, Picco included, build `-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1`
(`platformio.ini:106`), and all secondary-buffer code is inside
`#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE` (`FreeInkDisplay.cpp:457-546, :897-905`).
So `2496abe` (drain before freeing the secondary buffer), `a1e7700` and
`8fddc45` (grayscale baseline from the write/lent framebuffer) are **not
compiled here** — and note our drivers return early on `nullptr` without
clearing their in-sync flag (`Ssd1677:783`, `Uc8253X3:561`, `Uc8279:467`),
so `8fddc45`'s contract would not hold on our pin if dual-buffer ever arrives.
Also not taken: `08073b3` (reverted upstream; our HALF resync covers the
sleep-cover case, `HalDisplay.cpp:150-157`); `ab49da0` (guards an
`esp_pm` lock we do not use — our light sleep is explicit,
`HalPowerManager.cpp:178, :278, :355`); `d37a158` (the code after
`esp_deep_sleep_start()` is unreachable — IDF 5.5.2 aborts there,
`sleep_modes.c:1303-1309`; prebuilt libs matching INFERRED); `eff79e8`
(depends on reverted `08073b3` and a `displayWindow` our Uc8279X4 lacks);
`e263163`, `9559ed3` (EEGO A4); `e91e34e`, `629b44a`, `0ac8661` remainder
(Metalio); `64dc507` (FreeType, no `libs/font` here); `d06f277`, `121d644`
(no callers); `b53dc34` (Sticky); Belphemur/iandchasse/hooligan333 SDK-fork
work (Silkscreen board, X4 Pro, or a rail power-off that adds 20–40 ms per
refresh — against this effort). Already ours by equivalent:
`5e94fe7`→`a8fd0e2`, `a666c92`→`551db00`, `0ca892a`→`edd96d8`,
`dda4dc5`→`cb34b73`, `543a8d6`→`1888620`, `0ac8661`→`d7403b3` (prereq only),
`2264d13`→`9dbf4f2` (reduced).

### Idea-level only (code not portable)

- Xerif `5a4b32ceca` background image pre-decode on core 0 into a tone cache
  (3,600 lines, TTF-engine-bound). The idea fits the Picco's second core; the
  code does not port.
- alyapin `bf4e865752` narrower edge-Back band plus right-edge Back — would
  change a gesture ruling (our band is the left quarter, `EdgeSwipe.h:23`);
  not one of the rejected passes. Owner's call if ever wanted.
- matcha `fc263a463e` drop the placeholder pass before a cold image decode
  (we still do it, `EpubReaderActivity.cpp:1937-1941`): one FAST fewer on cold
  image pages, but first ink waits for the decode (1.7 s on a 464x709 image,
  theirs).

---

## 4. Upstream (`crosspoint-reader/crosspoint-reader@develop`), pertinent only

The full backlog is [fork-sync.md](fork-sync.md)'s business. Of the 466
commits behind, the pertinent ones were read above (shortlist #1, #3, #5, #7,
#8, #9, #11, #12, #14, #15, #24 and the "checked" tables). Picco commits
`261726f61`, `49b84fdc1`, `471243b2d`, `fb2a8a8f1`, `8f4d035cb`, `5936f4850`,
`37138937b` were hand-ported by the 2026-11-05 port. Not read in diff, recorded
for the next pass: `70faa29dac`/`81e1927093` light-sleep + refresh downclock
then its revert (our light sleep is [T-017]); `bb93a462cc` button remapping on
release (no remap screen here); `e70e3f1aa4` FtFont sharing (no TTF here).

---

## 5. The simulator side

49 forks of `crosspoint-reader/crosspoint-simulator`. **Our fork has no common
ancestor with upstream's `main`** per the compare API (404), so its own
divergence cannot be measured that way any more. Forks with work of their own:
`eszter007/crosspoint-simulator-ios` (57: iOS, `CROSSPOINT_SIM_PSRAM`),
`0x1abin` (36: ReadPico desktop sim), `PlutoKeating` (19: Emscripten web build),
`marginalia-os` (12: storage fault replay hooks), `flyboy-byte` (9),
`byronwind` (6: M5PaperS3 touch), `zukamas` (5: Murphy touch),
`Aelerinya` (4, pushed 11-08). The rest are HAL-stub maintenance or nothing.

One worth reading for the latency trace (`tools/latency/`): **Aelerinya
`eaddac491`** — `CROSSPOINT_SIM_FRAMES_DIR` saves every displayed frame from
the render path (catches frames drawn while the loop is blocked), and host
keyboard/mouse input is ignored while `CROSSPOINT_SIM_INPUT_SCRIPT` is set,
because *"a key the host reported as held reached the firmware as a held Power
key and slept the device mid-script"*. Our simulator has neither
(`grep FRAMES_DIR\|SIM_HOST_INPUT` in `~/src/crosspoint-simulator/src` → 0 at
`586f03db`). Its `CROSSPOINT_SIM_SPEED` clock scaling would distort latency
numbers — do not take that half. No Picco simulator exists anywhere in the
ecosystem (glintux's `simulator_picco` is an env name; INFERRED from subject).

---

## 6. Limits of this survey

- **Default branches only** for every fork except CrossInk (whose
  `development` and `release/v2.0.0` were fetched once found). A fork doing
  its work on a side branch was missed, exactly as CrossInk nearly was.
- **Subjects first, diffs second.** ~2,900 subjects were filtered by regex and
  read; only the ~90 that looked pertinent were read as diffs. A good fix with
  a vague subject ("various fixes", "v0.13", release-roll-up commits such as
  3r1pm4V `b78e050c03`, `2c0d70567e` with 229 files) was not read.
- **Third-level forks** (~40, e.g. `agosez/CrossInk-Bookorbit`'s 14,
  `ryokun6/crossmux`'s 7) were not enumerated.
- **SDK forks:** 5 of 174 fetched (jpirnay, homunet, iandchasse, Belphemur,
  hooligan333); `zrn-ns/community-sdk` is not a GitHub fork of Free-Ink and was
  not searched. The SDK sweep since 2026-09-14 was path-filtered.
- **Not fetched:** the SDK commits behind Xerif `1398ad81cb`, glintux's Picco
  trio, homunet's SDK pointer, TenorGroup's SDK `efe8e40`.
- **Large commits** (`a2311a882b`, `8afeea6f0e`, `613750deff`, upstream
  `b6746c1dfe`'s 829-line CssParser rewrite, `c80c537f28`, `9610da7022`) were
  read from their messages plus key hunks only.
- **The Picco's SdFat copy** (`.pio/libdeps/picco/SdFat`) was assumed to match
  the default env's, because the `patch_sdfat.py` gate covers every env.
- **Licences:** everything above is a CrossPoint (MIT) fork or Free-Ink (MIT);
  no third-party licence question arises. Re-check any file that a fork itself
  vendored before lifting it.
- The three verification agents were read-only subagents; their load-bearing
  claims for #1, #2, #3 and #4 were re-checked by hand (`FsCache.cpp:40-43`,
  `GfxRenderer.cpp:3149` vs `:761`, `Epub.cpp:455-462`,
  `FrontlightManager.cpp:187`). The rest rests on their reading, with their
  citations.

---

## 7. Method (re-runnable)

Everything was fetched into scratch bare repos, never into the working clone
(no remote was added). `$S` is any empty scratch directory.

```bash
# 1. Direct forks. REST pages them all; GraphQL gives the true count.
for p in $(seq 1 30); do
  gh api "repos/crosspoint-reader/crosspoint-reader/forks?per_page=100&page=$p&sort=newest" \
    --jq '.[] | [.full_name,(.stargazers_count|tostring),.name,.created_at,.pushed_at,.default_branch,(.forks_count|tostring)] | @tsv'
done > forks_rest.tsv      # 1,632 rows; sort=oldest/stargazers/watchers give the SAME set
gh api graphql --paginate -f query='query($endCursor:String){repository(owner:"crosspoint-reader",name:"crosspoint-reader"){forks(first:100,after:$endCursor){totalCount pageInfo{hasNextPage endCursor} nodes{nameWithOwner stargazerCount name createdAt pushedAt forkCount defaultBranchRef{name}}}}}' \
  --jq '.data.repository.forks.nodes[] | [.nameWithOwner,(.stargazerCount|tostring),.name,.createdAt,.pushedAt,(.defaultBranchRef.name // ""),(.forkCount|tostring)] | @tsv' > forks_gql.tsv

# 2. Pushed since the last survey, and not merely created then.
awk -F'\t' '$5>="2026-08-15" && substr($5,1,13)!=substr($4,1,13)' forks_gql.tsv > recent.tsv

# 3. Divergence (one call per fork).
gh api "repos/crosspoint-reader/crosspoint-reader/compare/develop...OWNER:REPO:BRANCH?per_page=100" \
  --jq '{ahead:.ahead_by,behind:.behind_by}'
#    NB: .commits is OLDEST-first, so page 1 of a big fork shows no recent work.
#    That is why step 4 fetches instead.

# 4. Every fork that is ahead: fetch its default branch into one bare repo.
#    (bash, not zsh: in zsh "$br:refs" applies the :r modifier and mangles the refspec)
git init --bare $S/eco.git
git -C $S/eco.git fetch https://github.com/crosspoint-reader/crosspoint-reader.git '+refs/heads/develop:refs/remotes/upstream/develop'
git -C $S/eco.git fetch --no-tags "https://github.com/OWNER/REPO.git" "+refs/heads/${BR}:refs/remotes/OWNER__REPO/${BR}"
#    218 forks + 333 second-level + 12 parents: 3.9 GB.

# 5. New work = non-merge commits since 2026-08-15 not on upstream/develop.
git -C $S/eco.git log --no-merges --since=2026-08-15 --format='%h%x09%cs%x09%an%x09%s' upstream/develop..OWNER__REPO/BR

# 6. Forks of forks: the GraphQL query per parent with forkCount > 0; new work
#    excluded against upstream AND every ref of the parent.
git -C $S/eco.git log --no-merges --since=2026-08-15 CHILD ^upstream/develop \
  $(git -C $S/eco.git for-each-ref --format='^%(refname)' refs/remotes/PARENT/)

# 7. Keyword filter on subjects, deduped by subject, minus subjects already on
#    upstream/develop or our main. Case-insensitive regexes:
#    lag:   refresh|waveform|\blut|ghost|partial|windowed|ssd1677|ssd2677|uc8253|uc8179|uc8279|gr[ae]yscale|\bgray|anti.?alias|\baa\b|\bspi\b|latency|\blag\b|faster|perf|prerender|prefetch|prewarm|page.?turn|debounce|light.?sleep|\bdu\b|\ba2\b|busy|responsive|slow
#    picco: picco|esp32-?s3|\bs3\b|psram|touch|tma525|frontlight|sgm41562|charger|slide.?switch|gesture|swipe|\btap\b|backlight|warmth
#    crash: crash|abort|\boom\b|out of memory|heap|leak|use.after.free|overflow|corrupt|\bfat\b|fat32|exfat|sd ?card|\bsd\b|watchdog|\bwdt\b|panic|reboot|hang|freez|null|stack|data loss|atomic|brick|boot.?loop|fragment|malloc|alloc

# 8. CrossInk's real branch.
gh api 'repos/uxjulia/CrossInk/branches?per_page=100' --jq '.[].name'   # development, main, release/v2.0.0

# 9. The SDK: same GraphQL fork query on Free-Ink/freeink-sdk, then
git -C $S/sdk.git fetch https://github.com/Free-Ink/freeink-sdk.git '+refs/heads/*:refs/remotes/upstream/*'
git -C $S/sdk.git fetch <worktree>/freeink-sdk '+HEAD:refs/ours/pin'
git -C $S/sdk.git cherry ours/pin upstream/main      # hint only: trimmed ports change patch-ids
git -C $S/sdk.git log --no-merges --since=2026-09-14 ours/pin..upstream/main -- <driver paths>

# 10. The simulator: GraphQL fork query on crosspoint-reader/crosspoint-simulator,
#     then compare main...OWNER:REPO:BRANCH for each.

# 11. Upstream itself.
git -C $S/eco.git fetch <worktree> '+refs/heads/lag-reduction:refs/ours/lag-reduction'
git -C $S/eco.git cherry ours/lag-reduction upstream/develop | grep '^+'

# 12. Each candidate: read the diff, then grep + read our code.
git -C $S/eco.git show <sha> [-- <path>]
```

Three read-only agents did step 12 in parallel, partitioned by area (SDK; SD
and crash; lag, input and Picco), each told to try to disprove every take and
to report what it found clean. GitHub API use for the whole pass: well under
the 5,000/hour authenticated limit.
