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

1. **BLE must be initialised with contiguous headroom.** `nimble_port_init()` is
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

Final run, BLE initialised before the editor buffer:

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
`__attribute__((weak))` and returns false, and `lib_ignore = BLE` means nothing
overrides it. Without a strong override the controller's RAM is donated to the
heap at boot and `btdm_controller_init()` later executes into it:

```
Guru Meditation Error: Core 0 panic'ed (Instruction access fault)
MEPC 0x00000000   RA -> r_lld_env_init   T0 -> btdm_controller_init
```

The release is **irreversible until reboot**, so BLE-capability is a boot-time
decision, not a runtime one.

**PlatformIO does not put the `bt/` include dirs on the compile line for project
sources** — 325 `-I` flags reach a project TU and none is nimble, even though
`flags/includes` lists them. `spike-build.sh` injects them via
`PLATFORMIO_BUILD_FLAGS`, which feeds `project.checksum`: hold it constant across
a session or every build rebuilds from scratch.

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

## Reproducing

```bash
./spike-build.sh                 # build
./spike-build.sh upload          # build + flash the USB-connected X4
python3 spike-capture.py         # wait for the port, flash, log to spike-capture.log
python3 spike-drive.py 600       # send CMD:BLEEDIT and stream the log
```

`CMD:BLEEDIT` over serial opens the editor without anyone at the buttons; the
home-menu row does the same thing by hand. Log markers: `SPIKE-HEAP P0..P5`,
`SPIKE-LATENCY`, and `BLESPIKE` for the GAP/GATT trace.
