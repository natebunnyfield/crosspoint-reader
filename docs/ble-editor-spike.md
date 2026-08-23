# BLE keyboard → text editor: spike findings and remaining work

Branch `spike/ble-editor`. **Throwaway — never merged to main.** The deliverable
is the numbers and the verdict below, not the code. Started 2026-08-05 to answer
the question `docs/manage-files.md` defers the editor on: does a resident BLE HID
host fit the ESP32-C3's 380 KB / no-PSRAM budget alongside the reader?

Status language in this file is literal. **Device-confirmed** means observed on
the USB-connected X4 running `-e default`. **UNCONFIRMED** means not yet observed
on hardware, whatever the code says.

---

## Verdict

**The whole path works on hardware.** Scan → connect → bond → HID service
discovery → subscribe → decoded keystrokes → glyphs on the panel, against a real
BLE keyboard (Geonix rev.2 on profile 2). Device-confirmed 2026-08-05.

**Resident BLE HID host: NO.** Home idle without BLE leaves 97,616 B free. BLE
bring-up costs ~72 KB. That leaves ~26 KB *before a book is open*, against the
>50 KB-free-while-reading bar. Not viable.

**Connect-on-demand: YES, with two conditions.** The permanent cost of being a
BLE-capable build is only ~2,340 B of total heap; the ~72 KB is claimed at
`nimble_port_init()` and returned at `nimble_port_deinit()`, so a reader session
never pays it. The conditions:

1. **BLE must be initialized with contiguous headroom.** `nimble_port_init()` is
   nondeterministic near ~86 KB free / ~73 KB maxalloc — observed succeeding at
   85,976 and **hard-hanging** at 85,980 and 85,752, then succeeding at 94,972.
   The failure mode is the dangerous one: not a panic, not a reboot, no crash
   report. USB CDC stays enumerated and the firmware answers nothing, not even
   `CMD:SCREENSHOT`. Only a power cycle recovers it. Initialising BLE *before*
   the editor's 8 KB buffer was enough to clear the boundary in testing, but the
   real fix is a measured minimum-headroom precondition, not luck.
2. **~6 KB is not returned per open/close cycle** (94,972 before init → 88,972
   after deinit). Leak or fragmentation is undetermined.

**Latency: ~1 s from keypress to glyph, and that is a floor, not a defect.**
Five samples: 927, 1121, 1127, 1641, 1689 ms, with the e-ink refresh a very
stable 570–573 ms of each. The 350 ms keystroke-batching debounce plus a 570 ms
refresh puts the floor at ~920 ms; the observed minimum was 927 ms. The longer
samples are the first key of a burst waiting for the rest. Usable for jotting a
note; not usable as typing feedback.

---

## Device-confirmed measurements

X4, `-e default`, LOG_LEVEL=2.

### Heap

Final run, BLE initialized before the editor buffer:

| Point | Free heap |
|---|---|
| P0 — boot, end of `setup()`, no BLE | 137,772 B (total 253,688, maxalloc 114,676) |
| P0b — Home idle, covers painted | 97,616 B |
| P1 — editor entered, before BLE | 94,972 B (maxalloc 73,716) |
| after `nimble_port_init()` | 29,768 B — **−65,204 B** |
| P2 — after host task start | 23,020 B — **−6,748 B** |
| P2b — after the editor's 8 KB buffer | 14,312 B (maxalloc 11,764) |
| P4 — while typing, connected + subscribed | 13,676 B |
| P5 — after `nimble_port_deinit()` | 88,972 B (min seen 14,056) |

The host-task delta was **byte-identical (−6,748 B) across every cycle**, and
`nimble_port_init()` landed at 64,800–65,204 B, so these are not one-off
readings. **Total BLE bring-up: ~72 KB.**

Total heap with BLE reserved is 250,980–251,348 B vs 253,688 B without — the
permanent price of a BLE-capable build is **~2,340 B**.

### Latency (keystroke → glyph)

Timestamped inside the NimBLE notify callback and read again after
`displayBuffer()` returns, so it measures real key-to-pixels.

| Sample | key→glyph | of which render |
|---|---|---|
| 1 | 1121 ms | 570 ms |
| 2 | 1127 ms | 571 ms |
| 3 | **927 ms** (min) | 571 ms |
| 4 | 1641 ms | 570 ms |
| 5 | **1689 ms** (max) | 573 ms |

Floor = 350 ms debounce + ~570 ms e-ink refresh = ~920 ms, and the observed
minimum was 927 ms. Lowering the debounce trades directly against refresh count.

### HID service of the test keyboard

`0x0019..0x0044`, 14 characteristics, **7 notifiable input reports**: Protocol
Mode `0x2A4E`, six notifiable `0x2A4D` reports (`0x001d 0x0021 0x0025 0x0029
0x002d 0x0031`), a writable `0x2A4D` output report `0x0035`, Report Map `0x2A4B`,
Boot Keyboard Input `0x2A22`, Boot Keyboard Output `0x2A32`, Boot Mouse Input
`0x2A33`, HID Information `0x2A4A`, HID Control Point `0x2A4C`. Keystrokes arrive
on `0x001d` as 8-byte boot-layout reports. Battery level also notifies on
`0x0017` (outside the HID service).

Advertisement: `02 01 06 03 03 12 18 03 19 c1 03 0f 09 "Geonix rev.2-2"` — flags,
service 0x1812, appearance 0x03C1 (keyboard), complete local name.

### Build size

| | baseline | +BLE HID host | delta |
|---|---|---|---|
| RAM (static, `.data`+`.bss`) | 50,508 B | 52,892 B | +2,384 B |
| Flash | 4,087,725 B | 4,310,035 B | +222,310 B |

---

## What the spike settled about the platform

**The keyboard must be BLE (HOGP).** The C3 has BLE 5.0 and no Bluetooth
Classic. Test keyboard: Geonix rev.2, `Services: 0x400000 < BLE >`, static random
address — confirmed via `system_profiler SPBluetoothDataType` before any code was
written. Paired Apple keyboards on the same Mac report `HID ACL` (Classic) and
are unusable here.

**No `custom_sdkconfig` work is needed.** Stock arduino-esp32 for the C3 already
ships `CONFIG_BT_ENABLED=y`, `CONFIG_BT_NIMBLE_ENABLED=y`,
`CONFIG_BT_NIMBLE_ROLE_CENTRAL=y`, `CONFIG_BT_NIMBLE_GATT_CLIENT=y`,
`SECURITY_ENABLE=y`, `NVS_PERSIST=y`, and the isolated core rebuild preserves
them. **Zero `platformio.ini` edits were required**, so the build-dir wipe never
had to be paid.

**Use the raw ESP-IDF NimBLE C API, not NimBLE-Arduino.** NimBLE-Arduino 2.x
bundles its own host while `libbt.a` already defines `ble_gap_connect`,
`ble_hs_init` and `ble_gattc_disc_all_svcs` — a duplicate-symbol gamble — and
adding it means a `lib_deps` line and a full wipe.

**`btInUse()` is load-bearing and its absence is a hard crash.** `initArduino()`
calls `esp_bt_controller_mem_release(ESP_BT_MODE_BTDM)` at boot unless
`btInUse()` returns true (`esp32-hal-misc.c:345`). The core's definition is
`__attribute__((weak))` and returns false, and nothing else in the build defines
it — verified by grep over the whole framework after `lib_ignore = BLE` was
removed in `7fee9a8c`: the only other definition is the weak one at
`esp32-hal-bt.c:26`, so the strong override at `BleHidHost.cpp:138` still wins.
(The original wording credited `lib_ignore = BLE` for that exclusivity. It never
provided it — the Arduino BLE library was never in the LDF graph anyway, since
nothing includes `BLEDevice.h`.) Without a strong override the controller's RAM is donated to the
heap at boot and `btdm_controller_init()` later executes into it:

```
Guru Meditation Error: Core 0 panic'ed (Instruction access fault)
MEPC 0x00000000   RA -> r_lld_env_init   T0 -> btdm_controller_init
```

The release is **irreversible until reboot**, so BLE-capability is a boot-time
decision, not a runtime one.

**~~PlatformIO does not put the `bt/` include dirs on the compile line for
project sources~~ — SOLVED 2026-08-06, cause found.** The observation was right
(325 `-I` flags reached a project TU and none was nimble, even though
`flags/includes` listed them) but it was not PlatformIO being unhelpful: it was
`lib_ignore = BLE` in `[base]`. pioarduino maps that token onto the ESP-IDF
component `bt` (`component_manager.py:899-903`) and regex-deletes all 55 `bt`
CPPPATH entries from the framework's `pioarduino-build.py`. `-lbt` survives the
strip, so it presents as a missing header with no link error.

`7fee9a8c` removed the entry, so the nimble includes now reach project sources
on every device env with no wrapper. **`spike-build.sh`'s
`PLATFORMIO_BUILD_FLAGS` injection is superseded and should be deleted from
that script** — it is now ten redundant `-I` flags that still feed
`project.checksum`, so leaving it in costs a full rebuild whenever it changes.

**Auto-sleep destroys unattended measurement.** Deep sleep drops the USB CDC
port, so a 10-minute idle strands any capture and needs a physical wake. The
spike pins `SETTINGS.sleepTimeoutMinutes = SLEEP_TIMEOUT_NEVER_MINUTES` — note
**not `0`**, because `getSleepTimeoutMs()` clamps small values *up* to the
minimum; only `>= SLEEP_TIMEOUT_NEVER_MINUTES` means never.

**A bare CoreBluetooth CLI binary cannot scan on macOS.** It takes SIGABRT
(exit 134) because TCC has nothing to attribute the request to. It needs a signed
`.app` bundle with `NSBluetoothAlwaysUsageDescription`, launched via
LaunchServices (`open -a`) — and since stdout is not connected that way, results
must be written to a file.

---

## Bug found in shipped (non-spike) code

`HomeActivity::getMenuItemCount()` hardcodes the home-menu row count, and that
count is what bounds `selectorIndex`. The `menuItems` vector and the count are
two separate sources of truth. Add a row to one and not the other and the row
**renders but can never be selected** — it reads as a dead menu entry, not an
off-by-one. Fixed on the spike branch for the spike's own row; the fragility
remains in main.

---

## Remaining work

### A. Bugs found but NOT fixed — these matter most

1. **CCCD subscription is broken; notifications only worked because of a stale
   bond.** `ble_gattc_disc_all_dscs()` returns `rc=3` (`BLE_HS_EINVAL`), so no
   CCCD handle is ever found and **no CCCD is ever written** — the log says
   "report val=0x… has no CCCD, skipping" for all 7 reports. Keystrokes still
   arrived because the keyboard had persisted its CCCD from an earlier bonded
   session, and HOGP peripherals store CCCD per bonded client. **A fresh,
   never-bonded keyboard would connect, discover, and then deliver nothing.**
   The per-characteristic handle range passed to `disc_all_dscs` is wrong; fix
   it and re-test against a keyboard whose bond has been deleted on both sides.
2. **`/notes-spike.txt` was never written** — the test ended with an empty
   buffer, so `save()` correctly logged "nothing to save". The SD write path is
   therefore **UNVERIFIED**. Type something and exit with Back.
3. **Arrow keys and everything above usage 0x38 are unmapped** — 23 such events,
   mostly `0x50`/`0x4f` (Left/Right arrow). Harmless here, but a real editor
   needs cursor movement, so the keymap must extend past 0x38.

### A2. Editor TODO (owner request 2026-08-06)

- **Pagination.** The editor shows only the tail of the buffer; there is no way
  to page back through a note. Port the `TextViewerActivity` approach — keep the
  page-start offsets so paging backwards replays exactly — and pair it with the
  gap buffer, since paging plus a cursor is what turns this from a capture box
  into an editor.
- **Markdown awareness while editing.** Render a deliberately small subset in
  place: `#`–`###` headings **bold** (and a size bump where the slot allows),
  `_italics_` / `*italics*` italic, `**bold**` bold, `` `code` `` and fenced
  blocks in the editor-group monospace face, `-`/`*`/`1.` list prefixes with a
  hanging indent, `>` blockquote indented. The font styles already exist
  (`EpdFontFamily::BOLD` / `ITALIC`), so this is a layout-and-span problem, not
  a font problem. Tables and images stay unrendered.
  Sequencing note: do the read-only viewer version first — the editor version
  has to keep byte offsets and rendered spans in sync as the cursor moves, and
  that is a materially harder problem than displaying a finished file.

### B. Remaining measurements

4. **Bond persistence across reboot.** On reconnect the peer reported
   `BLE_HS_EALREADY` and encryption completed in ~670 ms with no pairing
   exchange, which *indicates* the NVS bond survived several X4 reboots — but
   that was never isolated as its own test, so it is **UNCONFIRMED**.
5. **The ~6 KB not recovered per open/close cycle** (94,972 B before init vs
   88,972 B after deinit; an earlier cycle showed 5.7 KB). Leak or
   fragmentation is undetermined. Instrument N cycles and plot.
   *This needs no keyboard — only the X4.*
6. **Find the real minimum-headroom precondition for `nimble_port_init()`.**
   Ordering BLE before the editor buffer cleared the hang in testing, but the
   boundary was only ever bracketed (hangs at 85,752/85,980, succeeds at
   85,976/94,972). A shipped feature needs a measured threshold and a refusal
   path, because the failure mode is an unrecoverable hang.

### C. Product decisions

7. **Decide the boot-time posture.** Because the controller-memory release is
   irreversible, the product choice is: always reserve 2,340 B (BLE available on
   demand), or never reserve (BLE impossible without a reboot into an editor
   mode). This is a user ruling, not an implementation detail.
8. **Flash budget.** +222,310 B against a 6,553,600 B app partition currently
   62.4% full. Fits, but it is the single largest feature addition on record here
   and should be an explicit ruling.

9. **Is ~1 s key-to-glyph acceptable?** It is a floor set by the e-ink refresh
   (570 ms) plus the batching debounce (350 ms), not by BLE — the radio side is
   negligible. Partial-region refresh instead of full-screen is the only real
   lever, and that is its own piece of work.

### D. Known-incomplete in the spike code itself (throwaway quality, listed so nobody mistakes it for done)

10. US layout only; no key repeat, no modifiers beyond Shift, no dead keys, no
    non-ASCII. `tr()`, settings rows and theme polish deliberately skipped.
11. Report-map parsing is skipped entirely — the code assumes the 8-byte boot
    keyboard layout and tolerates a 9-byte report-ID prefix. A keyboard using a
    different report descriptor would decode as garbage.
12. Keycap-to-glyph mapping was not validated: the decoder assumes US layout,
    and a keyboard doing its own layout remapping in firmware will produce
    letters that do not match its keycaps.
13. Editor buffer is a flat 8 KB with no scrollback beyond the visible window and
    no cursor movement; Back is the only exit.
14. `layout()` re-wraps the whole buffer on every burst by calling
    `getTextWidth()` per character — fine at 8 KB, would not scale.

---

## Q&A: from spike to a real note editor (rulings pending, 2026-08-05)

Owner questions after the spike completed, with grounded answers and next
steps. Nothing here is committed work — each item that grows firmware surface
needs its own scope ruling (Philosophy: dedicated e-reader, not a Swiss Army
knife).

### What remains for a fuller-featured note editor, and what are the memory concerns?

The spike proves the input path; almost everything editor-shaped remains:

- **Fix the CCCD discovery bug first** (item A.1 above) — without it, only
  previously-bonded keyboards work.
- **Cursor movement + editing beyond append/backspace.** Keymap stops at usage
  0x38; arrows (0x4F–0x52), Home/End/Del, and an in-buffer cursor are needed.
  The buffer should become a **gap buffer** — insertion at a cursor in a flat
  array is O(n) per keystroke.
- **Scrollback.** The line ring shows only the tail; paging up needs the
  TextViewerActivity approach (page-start offsets).
- **Open existing files.** Chunked load, and an explicit cap with a refusal
  path for files larger than the buffer.
- **Layout cost.** `layout()` re-wraps the whole buffer per refresh via
  per-character `getTextWidth()` — fine at 8 KB, needs incremental relayout
  (only the dirty line onward) for anything bigger.
- **Save UX**: save-as (filename via `KeyboardEntryActivity`), autosave every
  N chars, and save-on-disconnect.

Memory concerns, in order of bite:

1. **~72 KB while BLE is up** — the editor is a connect-on-demand surface and
   can never coexist with an open book. Entering the editor from a reading
   session must fully close the book's caches first.
2. **`nimble_port_init()` needs measured contiguous headroom** or it hard-hangs
   (no panic, power-cycle only). A shipped editor needs a
   check-free-heap-else-refuse gate at entry.
3. **~6 KB lost per BLE open/close cycle** — must be root-caused (item B.5).
4. **Buffer growth**: 8 KB buffer + gap is fine; a 100 KB note is not. Cap
   note size (e.g. 32 KB) rather than engineering a rope.
5. While typing the whole system ran at **13,676 B free** — workable but with
   no slack for feature creep inside the editor screen itself.

### Can Edit be a Manage Files option? And New file?

Yes — this is the natural home, and `docs/manage-files.md` already defers the
editor to exactly this measurement. Concretely:

- **Edit** joins the per-item action menu (`OptionPopup`) for `.txt`/`.md`
  files: opens the editor seeded with the file (short-press View stays the
  read-only path).
- **New file** joins the folder context menu next to the deferred "New
  folder": name via the existing rename keyboard, then straight into the
  editor.
- The editor stays one activity; Manage Files passes a path or empty-with-name.
  BLE bring-up happens on editor entry (with the heap gate), not in Manage
  Files itself.

### Can vim be supported?

Real vim: **no** — it is a POSIX process; the C3 has no processes, no MMU, no
tty, and vim's runtime alone exceeds total RAM. Vim *keybindings*: **yes,
cheaply** — a modal layer over the decoder (normal/insert modes; `hjkl`, `i`,
`Esc`, `x`, `dd`, `w`/`b`, `0`/`$`, `o`). It is a state machine over already-
decoded keys, costs no meaningful RAM, and suits e-ink well because normal-mode
motions don't need per-key redraws. Scope ruling required; if approved, make it
a setting defaulting off.

### Can Space Mono be installed at hi-res for this?

Two separate facts:

- **Space Mono as an editor face: yes, trivially.** It is OFL on Google Fonts;
  `sd-fonts.yaml` already has a Monospace section (IBM Plex Mono, Source Code
  Pro) to copy a recipe from. Build `.cpfont` cuts, put them on the card, have
  the editor request the family by name.
- **"Hi-res" does not exist on the device.** The X4 panel is 1-bit 800×480 at
  scale 1; the 2x companions exist for `CROSSPOINT_RENDER_SCALE=2` **host
  builds** (desktop/iOS simulator). So: device gets the 1x cuts, and the 2x
  companions make it crisp in the iOS app.
- **S-tier note:** the installed-families ruling (seven families since
  2026-08-23) governs *reading* faces. An editor monospace is a new surface —
  needs an explicit ruling either extending S tier or declaring the editor font
  out of its scope. Do not silently install one more family.

### Can the iOS app support internal and external keyboards now?

Yes, and more easily than the device did. The simulator has no BLE, so
`BleHidHost` gets a `#ifdef SIMULATOR` twin with the same header: instead of
GAP/GATT it feeds the same ring buffer from SDL events — external (hardware)
keyboards arrive as normal SDL key events on iOS, and the internal on-screen
keyboard appears when the app calls `SDL_StartTextInput()`. The editor activity
is unchanged; it already consumes `popChar()`. This also makes the editor
testable headlessly (scripted key events), which the device path never can be.

### Can page turns be triggered over Bluetooth? Battery?

**RAM blocks it before battery does.** A BLE page-turn remote needs the host
resident *while reading* — that is the exact configuration the spike ruled out
(~26 KB free before a book opens, vs the >50 KB bar). Do not plan this against
the current heap.

If the heap picture ever improves: battery cost has two parts. A maintained
BLE connection at a long connection interval is small (low single-digit mA
average on the C3, modem-sleep between events) — but it also forbids the deep
sleeps an e-reader lives on, so the real cost is "device can never nap while a
remote is paired". That is a lifestyle change for the battery, not a rounding
error, and would need on-device measurement before any promise. Status:
blocked, revisit only after a major heap reclamation.

### Can markdown lists / "enough of markdown that Claude cares about" be supported?

Storage-side: markdown **is** plain text — the editor already "supports" it by
not mangling it, and that is all a file consumed by Claude needs. Rendering is
the only real question, and a deliberately small subset is feasible in the
viewer/editor: `#`–`###` headings (bold + size bump), `-`/`*`/`1.` list
prefixes (hanging indent), `**bold**`/`*italic*` (font styles already exist),
`` ` `` inline code and fenced blocks (monospace family — pairs with the Space
Mono item), `>` blockquote (indent + rule). That subset covers what
Claude-authored notes actually use. A full CommonMark renderer is out of scope;
tables and images stay unrendered.

Practical next step: render markdown-lite in the **viewer** first (read-only,
no cursor math), keep the editor plain; promote to the editor only if the
viewer version earns it.

### Can I put prompts for Claude in a text file and get a response saved back?

Three routes, in order of nearness:

1. **Card-shuttle / sync (works with existing pieces).** Write `prompts/*.md`
   in the editor; sync the card to the Mac (`scripts/sync-card.sh` WebDAV
   mirror, or USB); a Mac-side watcher runs `claude -p "$(cat prompt.md)"` and
   writes `prompt.answer.md` beside it; sync back; read the answer on-device
   in the viewer. No firmware work at all — this can be tried today.
2. **Wi-Fi transfer mode + watcher.** Same loop but over the device's existing
   web server (File Transfer), so no card removal. Small watcher script; no
   firmware change.
3. **On-device API call (real feature, needs a ruling).** The firmware already
   speaks TLS 1.3 via wolfSSL (OTA does HTTPS today), so a "Run prompt"
   action POSTing to the Anthropic API and streaming the response to
   `foo.answer.md` is technically feasible from a non-reading activity.
   Concerns: TLS needs the same ~50 KB-free regime the MEMFIX work targets
   (fine in a dedicated activity, never during reading); the API key would
   live in plaintext on the SD card unless a settings-side secret store is
   built; and it is the single clearest "is this still an e-reader?" scope
   question on this list. Route 1 proves the workflow before any of this is
   built.

---

## How to test and verify this firmware

Three tiers, cheapest first. The rule that matters: **push every check down to
the cheapest tier that can actually catch it**, and be honest that the top tier
cannot be skipped for radios, TLS-at-real-heap, or e-ink timing.

### 1. Host unit tests — seconds, no hardware, runs in CI

```bash
cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release
cmake --build build/test
ctest --test-dir build/test --output-on-failure -j
```

Pure logic only, but that is more of this spike than it looks. `test/ble_keymap`
covers the HID decoder: shift tables, control characters, unmapped usages
(arrow keys must produce nothing), held-key-emits-once, release-then-repress,
and a real n-key-rollover capture from the Geonix. That suite exists because
the decoder was extracted into `src/spike/HidKeymap.h` — free of Arduino and
NimBLE — specifically so it could be tested without a radio.

Still untested at this tier and worth adding: the word-wrap in `layout()`,
transcript formatting and the timestamp fallback chain, and `trimHistory()`'s
two caps.

### 2. Headless simulator — a minute, no hardware, full activity flows

```bash
CROSSPOINT_SIM_INPUT_SCRIPT="2000:DOWN;...;5600:ENTER" \
CROSSPOINT_SIM_SCREENSHOTS="8000:/tmp/shots/editor.bmp" \
pio run -e simulator -t run_simulator
```

Drives real activities with scripted button presses and captures BMPs, so
navigation, layout and end-to-end flow are verifiable without touching the
device. Typed text goes in via `fs_/ble-input.txt` (see the SIMULATOR twin in
`BleHidHost.cpp`), which makes the editor scriptable in a way the device path
never can be. This tier caught two real bugs: `uptime+16s` timestamps and the
missing conversation context.

**Do not run a bare `pio run -e simulator` between a device build and a flash.**
It omits the nimble `PLATFORMIO_BUILD_FLAGS`, which changes `project.checksum`
and cleans *every* env's build directory — `firmware.bin` disappears.

### 3. Device — minutes, the only place some things are real

```bash
pio run -e default -t upload     # or esptool directly; see below
# spike-run-exchange.py drove the whole exchange from one port owner (save wifi,
# open the editor, stream). Deleted 2026-08-08 — see Reproducing above.
```

Only the device can verify: BLE scan/pair/GATT, wolfSSL TLS at real heap, the
BLE↔WiFi handoff, e-ink refresh timing, and anything measured in bytes of free
heap. Everything in the "Device-confirmed measurements" section above came from
here and cannot be obtained anywhere else.

**One serial port owner, always.** A background capture holding
`/dev/cu.usbmodem*` resets the chip mid-write, which presents as
"chip stopped responding" 1–3 % into every flash and looks exactly like failing
hardware. This cost hours before it was identified; kill every capture process
before flashing.

### CI

`.github/workflows/ci.yml` builds, `pr-formatting-check.yml` runs clang-format.
Tier 1 is the only one of the three that CI can run today; tier 2 needs SDL and
tier 3 needs hardware.

## Reproducing

The four `spike-*` helpers this section used to invoke were **deleted on
2026-08-08** (owner ruling, TODO T-005). What each did, so it can be rebuilt if
the work resumes:

| Script | What it did | Why it went |
|---|---|---|
| `spike-build.sh` | injected the ten NimBLE `-I` paths through `PLATFORMIO_BUILD_FLAGS`, then built/uploaded | **Superseded.** `7fee9a8c` removed the `lib_ignore = BLE` entry, so those includes now reach project sources on every device env with no wrapper — see above. A plain `pio run -e default -t upload` is the whole thing now |
| `spike-capture.py` | blocked until the USB CDC port appeared, uploaded, then logged every line to a file — the headless half of `scripts/debugging_monitor.py`, which opens a matplotlib window and is useless unattended | still worked; removed as spike scaffolding |
| `spike-drive.py` | opened the port, waited out the boot, sent `CMD:BLEEDIT`, streamed the log live | still worked; removed as spike scaffolding |
| `spike-run-exchange.py` | drove one Claude request/response over the serial link | still worked; removed as spike scaffolding |

The last three were working device automation, not dead code. They are one
`git revert` away if unattended capture is wanted again; the build wrapper is
the only one that would be wrong to restore.

```bash
pio run -e default -t upload     # build + flash the USB-connected X4
```

`CMD:BLEEDIT` over serial opens the editor without anyone at the buttons; the
home-menu row does the same thing by hand. Log markers: `SPIKE-HEAP P0..P5`,
`SPIKE-LATENCY`, and `BLESPIKE` for the GAP/GATT trace.
