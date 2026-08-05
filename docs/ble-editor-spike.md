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

**Resident BLE HID host: NO.** Home idle without BLE leaves 97,616 B free. BLE
bring-up costs 71,548 B. That leaves ~26 KB *before a book is open*, against the
>50 KB-free-while-reading bar. Not viable.

**Connect-on-demand: YES, and cheaper than expected.** The permanent cost of
being a BLE-capable build is only ~2,340 B of total heap. The expensive 71,548 B
is claimed at `nimble_port_init()` and returned at `nimble_port_deinit()`, so a
reader session never pays it.

Both conclusions rest on device-confirmed numbers. The typing half of the spike
(pairing, keystroke decode, latency) is **UNCONFIRMED** — see Remaining work.

---

## Device-confirmed measurements

X4, `-e default`, LOG_LEVEL=2.

### Heap

| Point | Free heap |
|---|---|
| P0 — boot, end of `setup()`, no BLE | 137,772 B (total 253,688, maxalloc 114,676) |
| P0b — Home idle, covers painted | 97,616 B |
| P1 — editor entered, before BLE (after the 8 KB buffer) | 124,968 B |
| after `nimble_port_init()` | 60,168 B — **−64,800 B** |
| P2 — after host task start | 53,420 B — **−6,748 B** |
| P3 — steady-state scanning | 44,112 B (min 14,248, maxalloc 40,948) |
| after `nimble_port_deinit()` | 80,300 B |

The two independent open/close cycles produced **byte-identical** deltas
(−64,800 and −6,748 both times), so these are not one-off readings.
**Total BLE bring-up: 71,548 B.**

Total heap with BLE reserved is 251,348 B vs 253,688 B without — the permanent
price of a BLE-capable build is **2,340 B**.

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

### A. Finish the spike (blocked on hardware/keyboard)

1. **Power-cycle the X4.** It wedged after `heap before nimble_port_init: 85980`
   with no USB enumeration for 100 s+ and auto-sleep disabled, so it is not
   sleeping. The panic text was lost with the port.
2. **Capture that panic.** Attach serial *before* sending `CMD:BLEEDIT` so the
   Guru Meditation dump is not lost when CDC drops, then `addr2line` it. The same
   call succeeded at 86,052 B free, so the 85,980 B failure is not obviously OOM
   and must not be assumed to be.
3. **Get the Geonix advertising HID.** The X4 saw 25+ distinct advertisers and
   **zero** carrying service 0x1812. Cross-check with the Mac-side scanner
   (`scratchpad/BleScan.app`) to establish whether this is the keyboard or the
   scanner. If the keyboard advertises HID only in its GATT table and not in the
   advertisement — legal HOGP — UUID-only scanning will never find it and the
   host must connect first and discover second.
4. **P3/P4 measurements**: heap while connected+bonded+subscribed, and while
   typing.
5. **Latency**: keystroke → glyph. Already instrumented — timestamped inside the
   NimBLE notify callback and read again after `displayBuffer()` returns, so it
   measures real key-to-pixels, not just render time.
6. **Verify `/notes-spike.txt`** is written correctly on Back.

### B. Must be resolved before this becomes a real feature

7. **The ~5.7 KB not recovered per open/close cycle.** Teardown returned 80,300 B
   against 86,052 B at entry. Genuine leak or heap fragmentation is unknown;
   repeated open/close would compound either. Instrument N cycles and plot.
   *This needs no keyboard — only the X4.*
8. **Decide the boot-time posture.** Because the controller-memory release is
   irreversible, the product choice is: always reserve 2,340 B (BLE available on
   demand), or never reserve (BLE impossible without a reboot into an editor
   mode). This is a user ruling, not an implementation detail.
9. **Flash budget.** +222,310 B against a 6,553,600 B app partition currently
   62.4% full. Fits, but it is the single largest feature addition on record here
   and should be an explicit ruling.

### C. Known-incomplete in the spike code itself (throwaway quality, listed so nobody mistakes it for done)

10. US layout only; no key repeat, no modifiers beyond Shift, no dead keys, no
    non-ASCII. `tr()`, settings rows and theme polish deliberately skipped.
11. Report-map parsing is skipped entirely — the code assumes the 8-byte boot
    keyboard layout and tolerates a 9-byte report-ID prefix. A keyboard using a
    different report descriptor would decode as garbage.
12. Bond persistence is wired (`ble_store_config_init()`, NVS) but
    **UNCONFIRMED** — never observed surviving a reboot.
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
