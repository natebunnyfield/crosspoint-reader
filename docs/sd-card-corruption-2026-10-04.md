# Firmware corrupting the FAT on SD cards — 2026-10-04

Owner report, taken as stated: **the firmware corrupts the cards; how is
unknown; assume more than one cause.** Surveyed at `983424f1` (main, 26 ahead of
origin) plus freeink-sdk `cc78188`. Everything below is from reading source and
SdFat 2.3.x as built for `gh_release`/`default`, two read-only audit passes, and
the two damaged cards themselves. **Nothing here is device-confirmed.**

## What the cards showed

Read on a Mac with `fsck_msdos` / `diskutil repairVolume`, 2026-10-04:

| Card | Damage |
| --- | --- |
| BUNNYFIELDS (31 GB, X-series) | a self-nested directory loop (`/Expecting Better.epub/OEBPS/styles/… /styles/…`); "entries after end of directory"; orphan clusters; FSInfo free count wrong; new files written by macOS landed on clusters already in use (a fresh `.bin`'s last 129 KB read back as font bitmap data) |
| OWEN_BNF (15.6 GB, X4) | **two `/.crosspoint` entries at the root**; `/.fonts` pointing at `.fseventsd`'s cluster; a garbage-named entry (file data read as a directory entry) inside `/.crosspoint`; fsck exit 206 |

Three signatures, one root: **clusters in use but marked free in the FAT**
(lost allocations), which the next allocation hands out again. A cluster handed
to a new directory is zeroed, so the old owner's entries after it vanish behind
a 0x00 end marker — which is how a lookup for `/.crosspoint` can miss and
`mkdir` create a second one. A directory cluster handed to a file is overwritten
with file bytes, which then parse as garbage entries.

Separately measured, recorded so it is not re-derived: after a clean erase,
BUNNYFIELDS also returned 32,634 wrong bytes in a 256 MB host-written random
file; OWEN_BNF returned it exactly. That card's media is failing as well. It
does not explain OWEN_BNF, and it does not change the firmware findings below.

## SdFat facts the findings rest on (verified in `.pio/libdeps/*/SdFat`)

* `sd.begin(cs, hz)` is **SHARED_SPI** (`SdFat.h:72-74`); the card is deselected
  after every operation. The EPD driver wraps every transfer in
  `SPI.beginTransaction`/`endTransaction` (`EpdBus.cpp:130-185`). Bus-level
  arbitration between panel and card is therefore correct.
* `CHECK_FLASH_PROGRAMMING` = 1: single-sector writes (every FAT and directory
  sector) wait for programming and check CMD13. Multi-sector writes return after
  the stop token without waiting (`SdSpiCard.cpp:752`); the next command waits.
* **`USE_SEPARATE_FAT_CACHE` = 0 on RISC-V**: one 512-byte cache for FAT,
  directory and data. Consequences:
  * `remove()` and `truncate()` free the chain first; caching the directory
    entry then evicts the freed FAT sector **to the card before the entry is
    marked deleted** (`FatFileLFN.cpp:508-544`, `FatFile.cpp:1317-1355`). A reset
    in that gap = a live entry on free clusters = cross-link on next allocation.
  * `rename()` writes the new entry (old first cluster + size) **before**
    removing the old one (`FatFile.cpp:939-1042`). A reset in that gap = two
    entries sharing one chain.
  * Neither is a firmware bug on its own; they are windows. Every metadata
    operation the firmware does not need is one more window, and every
    uncommanded reset (panic, brownout) is a chance to land in one.

## Findings and what was done

Ranked by how directly each produces the damage above.

1. **The reader rewrote `progress.bin` as temp + remove + rename on every page
   turn** (`ProgressFile.h`), three directory operations and two FAT updates per
   turn, from the render task. **Fixed:** same-length records are rewritten in
   place (`O_RDWR`, no truncate) — one data sector plus the entry's mtime, no
   FAT change. Temp + rename remains for the first save and format changes.
   This is not the #2275 truncate-in-place: truncation frees and reallocates the
   chain; this never touches it.
2. **No low-battery shutdown.** The device ran into the brownout reset
   (`CONFIG_ESP_BROWNOUT_DET`, level 7) — mid-write as often as not — and each
   boot after repeated its state writes on the failing cell. **Fixed:**
   `LowBatteryGuard` sleeps cleanly after 120 s at 0% unplugged (0% is 3.45 V at
   rest in `BatteryMonitor`'s curve). It only arms after reading >0% this boot,
   because the X3 gauge path returns a cached 0 when a read fails and an unread
   gauge must never sleep the device. Host-tested (`test/low_battery_guard`).
3. **SD CS was not driven before the boot panel probe bit-bangs the shared
   SCLK/MOSI** (`HalGPIO::begin` → `XteinkDetect`), while `Storage.begin()` only
   runs later. After a warm reset (`ESP.restart`, a USB-powered deep sleep) the
   card is still powered and still in SPI mode; with CS low it parses bus
   traffic as commands, and a byte stream forming CMD24 + 0xFE writes 512 bytes
   to some sector. Whether GPIO12 actually floats low there depends on the C3's
   pin defaults, which source cannot show. **Fixed regardless:** CS is driven
   HIGH first thing in `HalGPIO::begin` (free; `static_assert` that X3/X4 agree).
4. **File Transfer rebooted with a WebSocket upload still open for write.**
   `CrossPointWebServerActivity::onExit` called `silentRestart()` without
   `webServer->stop()`; only `stop()` aborts the WS upload and the destructor
   never runs. Leaves clusters allocated under a 0-byte entry. **Fixed:** stop
   and reset the server first.
5. **WebDAV MOVE could move a folder into its own subtree.** SdFat's rename does
   not check; it re-parents the folder under its own descendant and rewrites
   `..`, detaching the subtree as a loop — the BUNNYFIELDS shape. **Fixed:**
   refused with 409 via `FsHelpers::isSameOrInside` (case-insensitive, like
   FAT), excluding case-only renames. Manage Files already refused this but
   compared case-sensitively; switched to the same helper.
6. **Mutations under a live WebSocket upload.** `wsUploadFile` stays open across
   many `handleClient()` ticks with HTTP and WebDAV served in between; deleting,
   renaming or overwriting that path (or its folder) orphaned the upload, and if
   the freed slot was reused (WebDAV PUT's temp rename) the upload's close synced
   its chain into the new file's entry — a cross-link. **Fixed:**
   `webUploadIsWriting(path)` is checked by HTTP delete/rename/move/font-delete
   and WebDAV PUT/DELETE/MOVE/COPY.
7. **Manage Files / File Browser folder delete has walked a stale name since
   `72b26b957` (2026-09-10).** That commit added the unreadable-entry guard to
   `FsOps::removeRecursiveWithCacheClear` and deleted the `getName()` call the
   guard was meant to check, so every entry was named by whatever the caller
   last left in the buffer. **Fixed:** call restored.
8. **SDK `SDCardManager::removeDir` named entries into `char[128]`** and ignored
   the result. Longer names (the cards carry 170-character filenames) came back
   empty, the child path became `"<dir>/"`, which SdFat resolves to `<dir>`, and
   a long-named subfolder recursed into its parent until the stack overflowed
   mid-delete. **Fixed in freeink-sdk:** a static buffer sized for FAT's longest
   name (765 bytes UTF-8), stop on an unreadable entry, close the iterator's
   handle before deleting by path.
9. **`Txt::generateCoverBmp` removed the failed BMP with its write handle still
   open** (`lib/Txt/Txt.cpp`): the remove freed nothing (entry never synced past
   cluster 0), orphaning everything written. **Fixed:** close first, as `Epub`
   already did.
10. **`HalStorage::openFileForRead/Write` opened the new file before closing the
    out-param's old handle.** Reusing a handle still open for write on the same
    path would truncate the chain, then write the stale first cluster back. No
    caller does this today (audited every reuse site). **Hardened:** the
    out-param is reset first.
11. **A bare `std::make_unique` wrapped every opened file** — OOM `abort()`
    under `-fno-exceptions`, i.e. a reboot with other write handles unsynced.
    **Hardened:** `HalFile::wrap` uses `new (std::nothrow)` and hands back a
    closed file.
12. **Every settings/state save rewrote the file even when nothing had
    changed** (`PersistableStore::writeDocToFile`: temp create, remove, rename
    — five directory operations and two FAT updates). **Fixed 2026-10-07:**
    the serialized JSON is compared with the bytes on the card first — size,
    then a chunked compare with no heap and no `readFile()` (whose byte loop
    keeps issuing failing reads to its 50 KB cap on an I/O error) — and an
    identical save is skipped. A missing, short or unreadable file compares
    unequal, so doubt always falls through to the write. How often it fires is
    smaller than first thought: in the dominant sleep-from-the-reader cycle
    the state bytes DO change between the paired saves (`readerActivityLoadCount`
    is a crash guard that counts up and resets; `openEpubPath` is blanked at
    wake and restored by the reader's onEnter), so those still write. It fires
    for `RecentBooksStore::addBook` re-adding the open book, the home-branch
    wake followed by onEnter, and any settings save that changed nothing;
    the headless simulator showed `state.json` and `recent.json` skips on a
    cold boot.
13. **The quick-resume frame file was created at every sleep and removed at
    every wake** (`/.crosspoint/sleep_frame.bin`, a fixed size: 48,000 B on
    the X4, 52,272 B on the X3): two FAT-touching operations per cycle.
    **Fixed 2026-10-07:** it persists and is rewritten in place (`O_RDWR`, no
    truncate) at the next quick-resume sleep, so the card sees only data
    sectors into clusters the file already owns — not even the directory
    entry, since SdFat dirties it only on growth or with a date-time callback,
    and none is registered. A persistent file needs its own freshness marker,
    which the first version of this fix lacked (second review, below): a
    reset partway through the in-place write left a correct-size file that
    was half this sleep's frame and half the last one's, and a save that
    failed before writing left the previous frame intact — both shown on the
    next boot, where the old create-on-sleep file could only be missing or
    short. The frame is therefore followed by a 4-byte trailer that is VALID
    only between a completed save and the next load: the save marks it STALE
    before touching the frame and VALID after the last byte, the load marks
    it STALE once it has consumed the frame, and anything but VALID shows the
    splash, exactly as a missing file did. Three single-sector data writes,
    still no FAT. A failed open, write or close removes the file.
    Simulator-verified headless 2026-10-07 (`sleepScreen: 6`; scripted
    `SLEEP`, `POWER`, `QUIT`): the file appears at the first sleep at
    48,004 B with a VALID trailer, a boot that quick-resumes consumes it to
    STALE and leaves it in place, and a second sleep rewrites it in place.
14. **SdFat itself frees a file's clusters before its directory entry is
    marked deleted, and truncates the same way** — the ordering every
    finding above was working around. With the one shared sector cache,
    `remove()` dirties the FAT sectors first and they are evicted to the
    card when the directory sector is loaded, so the entry is still live on
    the card while its clusters are already free. The truncation the firmware
    actually performs is not `FatFile::truncate()` — nothing calls it and the
    linker drops it — but the `O_TRUNC` branch of `openCachedEntry()`, which
    every one of the 51 `openFileForWrite` sites reaches: it frees the chain
    at open and only marks the entry dirty, so the entry on the card keeps
    the old size and first cluster until that handle's `close()`, which can
    be a whole file copy later. A reset anywhere in that span is the
    cross-link seed. (The first version of this patch reordered `truncate()`
    and missed this; the third review caught it from the firmware's symbol
    table.) **Fixed 2026-10-07** at that layer: `scripts/patch_sdfat.py`, a
    pre-build hook like the JPEGDEC and wolfSSL ones, reorders all three
    functions in the libdep tree so the directory entry reaches the card
    first, the chain is freed after, and the frees are flushed before the
    function returns. The same reset now leaves an entry already gone, or
    already emptied, with clusters still marked used — lost space until an
    fsck, harmless to every other file. The hook is idempotent and **aborts the
    build if SdFat's text has moved**, so an SdFat upgrade cannot silently
    ship the old order, and aborts an ESP32 env whose SdFat copy is not there
    to patch. Verified from a clean state: with `.pio/libdeps/default/SdFat`
    removed, `pio run -e default` reinstalled SdFat 2.3.1 through the Library
    Manager BEFORE the pre-build hooks ran, the hook patched the fresh copy,
    and the build passed — a fresh clone gets the reordered functions on its
    first build. A second build applies nothing. Exact original and patched
    text are in the script. Two consequences for the acceptance test: after
    a reset in one of these windows `fsck_msdos -n` will now report lost
    clusters (and, for a long-named file, orphaned long-name slots), which is
    the benign residue by design — a dirty verdict should be read for
    cross-links and bad chains, not for those.
    Cost: an `O_TRUNC` open now writes the directory sector at open as well
    as at close and flushes the frees at once — one or two extra sector
    writes per rewrite, none of them FAT allocations. The third review
    verified the call order in the compiled objects of both envs (sync →
    freeChain → cacheSync) and found no defect; its hardening points — the
    gate checks every block in the env's own copy, the libdeps path comes
    from PlatformIO, a clean does not trip it — are in. Not covered: exFAT
    (a separate code path with the same free-first order, reached by the
    same `FsFile` API; the cards are FAT32, but an SDXC card arrives exFAT
    and nothing refuses it — a patch for `ExFatFile::remove/truncate` is the
    same shape if ever needed) and
    `rename()`, which writes the new entry before removing the old one — a
    reset there leaves two entries on one chain, and the other order would
    leave the file unreachable instead; which is worse is an owner call,
    discussed under "Not done".
15. Data loss, not FAT damage, fixed in passing: WebDAV COPY `A.txt -> a.txt`
    removed its own source; BMP viewer "set as sleep cover" compared
    `/sleep.bmp` case-sensitively and could truncate the file it was reading.

## Adversarial review, same day

A read-only refutation pass over the finished diff. One confirmed defect,
fixed before commit:

* **The new guards only folded ASCII.** SdFat built with
  `USE_UTF8_LONG_NAMES` matches long names with `toUpcase()` per UTF-16 unit
  (`FatFileLFN.cpp:79`, table in `common/upcase.cpp`), so `/Café` and `/CAFÉ`
  are one directory and `MOVE /Café -> /CAFÉ/x` still built the loop. Worse,
  the pre-existing WebDAV case-only rename used `strcasecmp`, so
  `/Café -> /CAFÉ` took the overwrite branch and removed its own source.
  **Fixed:** `FsHelpers` carries a port of SdFat's fold table (MIT, attributed)
  and decodes UTF-8; `isSameOrInside` and the new `isSameFatPath` drive the
  MOVE guard, the case-only rename and the COPY refusal. Tests include é/É,
  Cyrillic, Greek sigma and malformed UTF-8.

**Second pass, 2026-10-07**, over items 12–13 and the progress-file suite.
No correctness defect in the save skip or the suite. On the frame file it
found the two paths above (torn in-place write accepted; failed save leaving
the previous frame), both cosmetic but both new; fixed with the trailer. It
also showed the `readFile()` compare was a poor fit for a save path (byte-wise
reads, and an error loop to the cap) — replaced with the size check and
chunked compare; that the save-skip examples in the first draft of item 12 do
not fire in the reader cycle — corrected above; that the X3 frame is 52,272
bytes, not 48 KB; and that a comment in `ProgressFile.h` wrongly said the
in-place write refreshes the entry's mtime (it writes no directory sector at
all). Noted, not changed: the last quick-resume frame now stays on the card
between sleeps and `/.crosspoint` is readable over WebDAV — but so are the
note and chat files the frame could show, so it exposes nothing new.

Plausible items, handled or recorded:

* Simulator in-process wake kept the `static` guard armed and "empty since"
  — it would re-sleep on the first sample. Guard moved to file scope and reset
  in `setup()`.
* The WebSocket upload path was not normalized, so `//` or `/./` in a
  hand-built START slipped past `webUploadIsWriting`. Now `normalizeWebPath`.
* Recorded, not changed: on a card that is ALREADY cross-linked, the in-place
  progress rewrite writes into whatever cluster the entry points at (the old
  remove would have freed down another file's chain instead — worse); the
  low-battery sleep still does the sleep-screen refresh and, in quick-resume
  mode, the 48 KB frame save at empty charge; an OOM while wrapping a handle
  yields a null `impl`, which any caller that skips `isOpen()` still asserts on
  (no worse than the old abort).

## Not done, deliberately

* **SPI CRC (`USE_SD_CRC`).** The card runs at 40 MHz on the bus it shares with
  the panel with CRC off, so a bit error on the wire is written silently — FAT
  sectors included. No evidence of bit errors was found, and enabling CRC
  changes failure behavior (errors instead of silent writes) on hardware nobody
  has measured. **Owner decision; the cost is measured** (2026-10-07, commit
  `9fe8e3d4f`, `gh_release`, a throwaway worktree with the commercial font
  headers copied in so the baseline matches the shipping build; the define was
  added beside `USE_UTF8_LONG_NAMES` in the SDK's
  `SDCardManager/inject_build_flags.py`, which appends to every library
  builder — `[base] build_flags` in `platformio.ini` reaches libraries too,
  but editing the ini wipes every build directory, and the hook was the
  cheaper place to measure; `crctab` confirmed present in `SdSpiCard.cpp.o`
  with `riscv32-esp-elf-nm`):

  | | Flash used | RAM used |
  | --- | --- | --- |
  | baseline | 5,350,623 B (81.6 %) | 55,772 B |
  | `USE_SD_CRC=2` (table-driven CRC-CCITT) | 5,351,345 B (81.7 %) | 55,772 B |
  | delta | **+722 B** | 0 |

  Compute cost is one CRC-16 over 512 bytes per sector, table-driven: on the
  order of 5–10 µs at 160 MHz against ~100 µs for the sector's SPI transfer at
  40 MHz, so under 10 % on raw sector throughput and invisible behind the FAT
  bookkeeping around it. Not measured on a device. What it buys: SdFat sends
  CMD59 so the card rejects a write whose data CRC does not match (a write
  error instead of a silently wrong sector) and the host rejects a read that
  arrives corrupted. What it changes: a marginal card or bus that used to
  "work" with occasional silent bit errors starts reporting write failures.
  To enable for good, `-DUSE_SD_CRC=2` in `[base] build_flags` (one build-dir
  wipe) or beside the UTF-8 define in `inject_build_flags.py`; nothing else
  changes and the cards' existing data is unaffected.
* **Reordering SdFat's `rename()`.** It writes the new entry (old first
  cluster, old size) and syncs, then removes the old entry. A reset between
  leaves two entries sharing one chain — and the `.tmp`-recovery in
  `PersistableStore` then removes one of them, which frees the chain under the
  other: the self-perpetuating cross-link. The other order (remove the old
  entry, sync, then write the new one) turns that reset into an unreachable
  chain: the file is lost (`PersistableStore`'s staged write would lose that
  save; the previous target was already removed), the card stays consistent.
  Both are defensible; the firmware's settings files are the ones at stake.
  Not built — owner's call. If chosen it is a third block in
  `scripts/patch_sdfat.py`.
* **A post-write "card idle" wait before power-off.** Audited: the last card
  operation before deep sleep is always a single-sector directory/FAT write that
  already waited for programming (`close()` → `cacheSync`). Added then removed.
* **Removing the individual sleep/wake saves.** `EpubReaderActivity`'s onExit
  save looked redundant with `enterDeepSleep`'s, but onExit runs on non-sleep
  paths too, so it is not — items 12 and 13 above take the duplicates and the
  frame file out instead. What remains per cycle is the saves whose content
  really changes (`showBootScreen`, `readerActivityLoadCount`, recents), each
  still a staged temp + remove + rename; an in-place rewrite of same-length
  JSON would be the next lever, and JSON lengths rarely match. A different
  lever, proposed and not built because it changes a crash-safety mechanism:
  `readerActivityLoadCount` and the blanked `openEpubPath` exist to break a
  boot loop into a crashing book, and a crash loop is a sequence of resets,
  not power losses — so the counter could live in `RTC_NOINIT_ATTR` memory
  the way `silentRebootMagic` already does, costing the two staged
  `state.json` writes per wake nothing at all. Owner's call.
* **NimBLE host task writing settings** on first keyboard pairing
  (`BleHidHost.cpp:550-558`) — a third task, low likelihood, not changed.

## Checked and found CLEAN (with where)

* Deep-sleep path waits for the render task via `RenderLock` in
  `processPendingTransitions` (`ActivityManager.cpp:153`); the reader's onExit
  closes the section build; the web server destructor runs on the sleep path.
* OTA and SD firmware-update restarts: flash only reads the card; 1.5 s delay.
* `silentRestart` runs inside `exitActivity` under `RenderLock`.
* No code in `src/` or `lib/` reaches SdFat around `HalStorage`; no other task
  (input, audio, BLE connection) calls Storage except the NimBLE note above;
  logging is an RTC ring, panics write only RTC memory.
* Reader section build handle: every step, suspend, abandon and reset under the
  render lock; Section closes before every remove/rename.
* PersistableStore, NoteEditor, BookNotes, CardSecret, font/library downloads,
  Epub covers/thumbnails/CSS, WebDAV PUT/COPY, HTTP and font uploads, Clear
  Cache, crash-report archive: all close before remove/rename, or collect names
  before deleting.
* Clear Cache's `char[128]` name buffer: an empty name cannot match the
  `epub_`/`txt_`/`xtc_` prefix, so it cannot widen the delete.
* SdFat's `openNext` skips `.`/`..`; `rename` re-reads the entry after `sync()`.

## Verification state

`pio run -e default` and `-e simulator` green; every edited TU confirmed
recompiled. Host suites: `PathContainmentTest` (8), `LowBatteryGuardTest` (5),
`ProgressFileTest` (4 — an operation-counting in-memory card proving a
same-length save issues no remove, rename or truncate),
plus the 8 existing suites linking `FsHelpers` — 53 pass, one perf test skipped
by design. **UNCONFIRMED on device.** To confirm: run the device a week of
normal use on a freshly formatted card, then `fsck_msdos -n` it on a Mac; a
clean result is the evidence, a dirty one should be compared against this list.

Status 2026-10-07 00:15: OWEN_BNF (erased and restored 2026-10-04) verified
clean, but its `state.json` still carried the restore's timestamp and no new
cache directories existed, so the reader had not booted on it — that clean
result is the restore, not a device test. `20261007T1608Z-crosspoint-a159ab59.bin`
(every item above, the SdFat reorder included) is on it, hash-verified after
a remount. The device test
starts when that firmware is flashed and the card goes into the reader.
