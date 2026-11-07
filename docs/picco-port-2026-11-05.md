# BOOX Picco: building this fork for it (2026-11-05)

Status: **measuring.** Two routes are being built in scratch worktrees
(`.claude/worktrees/picco-a`, `.claude/worktrees/picco-b`); the owner picks one
when the measured costs are in. Nothing from this has shipped.

## The device (verified 2026-11-05 on the owner's unit)

- USB descriptor on stock firmware: product `BOOX Picco`, VID `0x303a`
  (Espressif) PID `0x1001`, serial `<redacted, the MAC>` (its MAC), one interface:
  `TinyUSB MSC`, which reports no medium until the device offers it. No serial
  port in normal operation. Measured with `system_profiler SPUSBHostDataType`
  and `ioreg -p IOService -r -n "BOOX Picco"`.
- TinyUSB MSC means the USB-OTG block, so ESP32-S2/S3 class, not a C3 like the
  X4. Upstream records it as ESP32-S3, 16 MB flash, 8 MB octal PSRAM, 800x480
  SSD1677/SSD2677 panel (auto-detected), capacitive touch, warm/cool I2C
  frontlight, SGM41562 charger, slide switch
  (upstream `platformio.ini` comment above `[env:picco]`).
- **Download mode:** hold Power + Volume Up together for more than 10 s
  (upstream web installer, `crosspoint-tools` `src/pages/install/FlashTools.jsx`
  `APP_ONLY_INSTALLS.picco`). A first 20-minute watcher saw the device never
  re-enumerate (same IORegistry id `0x10016dd92` throughout). Later the same
  day it re-enumerated as `USB JTAG/serial debug unit` on `/dev/cu.usbmodem1101`,
  but the ROM did not answer a no-reset sync ("No serial data received"): the
  chip was running code with USB-Serial/JTAG exposed, not sitting in ROM.
  `esptool --before default-reset` (the USB-JTAG reset into the bootloader, as
  the web installer does) worked. Chrome held two
  `AppleUSBHostDeviceUserClient`s on the stock device; close any tab using it.

## What is on this unit's flash (read 2026-11-05, read-only)

`esptool v5.4.0`, all commands `--after no-reset`:

- Chip: ESP32-S3 (QFN56) rev v0.2, embedded PSRAM 8 MB (AP_3v3), 40 MHz
  crystal, USB mode USB-Serial/JTAG, MAC `<redacted MAC>`.
- `get-security-info`: flags 0x0, **Secure Boot disabled, flash encryption
  disabled**, `SPI_BOOT_CRYPT_CNT` 0, all six key blocks USER/EMPTY. Unsigned
  custom images boot.
- `flash-id`: manufacturer `c8` (GigaDevice), device `4018`, 16 MB.
- Partition table at 0x8000: identical to upstream `partitions_picco.csv`
  (above), with an MD5 entry.
- **otadata (0x15000, 0x2000) is entirely 0xFF.** A stock IDF bootloader with
  blank otadata and a valid factory partition boots the factory one. Yet the
  stock reader runs from `app`, and upstream CrossPoint installs into `app`
  without touching otadata and runs on retail units. So this vendor bootloader
  reaches `app` some other way. Not investigated further.
- Image headers (all magic 0xE9, DIO, 16MB/80m, chip id 9):
  - 0x0 bootloader: 3 segments, entry 0x403c8898.
  - 0x20000 `safeboot`: app `nano_reader` `0.0.1+2621`, built Sep 15 2026,
    IDF 5.5.4.
  - 0xE0000 `app`: app `picco` `1.0.0+2918`, built Sep 30 2026 15:40:53, IDF
    v5.5.4. This is the stock reader any CrossPoint flash overwrites.

### How this unit boots (observed on the serial log, 2026-11-05)

- With otadata blank, the bootloader starts **`safeboot` (`nano_reader`)**,
  Boox's recovery app. On a USB reset (`rst:0x15 USB_UART_CHIP_RESET`) it
  re-enumerates USB, probes the panel (its log first prints
  `[SSD1677] ID (2E): FF…` / `Panel ID not recognized for SSD1677`, then finds
  the right driver), enumerates the TMA525C touch controller, and draws its
  recovery main menu (`[RECOVERY] draw ui=0 … main_menu`). It then idles with
  USB-Serial/JTAG up. That FF-ID line is the recovery app's own probe order,
  not a dead panel.
- The Power + Volume Up "serial flash mode" combo is almost certainly this
  recovery app, which explains why the ROM did not answer a no-reset sync:
  esptool's USB-JTAG reset is what reaches the ROM.
- Upstream's installer ends with *"Press and hold the Power button to restart
  the device"*. A USB/RTS reset after flashing lands back in recovery. On this
  unit, with USB connected, the device never power-cycled: its USB session
  stayed 37 minutes old through the wait (IOUSB `sessionID` vs
  `mach_absolute_time`), so the Power-button handoff never happened.
- **What worked: point otadata at `app`.** 2026-11-05 ~23:15, owner's "go":
  re-read otadata (still all 0xFF, matching the saved copy), then wrote one
  4 KB sector at 0x15000 = `esp_ota_select_entry_t{ota_seq=1, seq_label=0xFF…,
  ota_state=2 (VALID), crc=0x4743989A}` + 0xFF, exactly what
  `esp_ota_set_boot_partition(ota_0)` produces (CRC checked against IDF's
  known value for seq 1). Sector 1 left erased. The bootloader then booted
  `app` directly, with no recovery app in the log, so it is a standard IDF
  bootloader. **To undo** (back to the recovery app): esptool
  `--before default-reset`, then write 8 KB of 0xFF at 0x15000 (the saved
  original is in the session scratchpad as `otadata.bin`). Consequence: the
  Power + Volume Up recovery combo may no longer reach `nano_reader`, if the
  stock app was what implemented it. USB-Serial/JTAG plus esptool is the way in.
- **Serial without resets:** opening the port with pyserial resets the chip
  (it clears DTR before RTS, and DTR-low/RTS-high is the USB-JTAG reset), so a
  reconnecting listener loops the device through resets. Open with
  `O_NONBLOCK` and clear RTS **before** DTR via `ioctl(TIOCMBIC)`.

## Hello world (2026-11-05)

`~/src/boox-picco/hello/` (no git): standalone PlatformIO project on the
upstream SDK pin 5e94fe7 (local clone), same pioarduino platform as `[base]`,
stock framework (no `custom_sdkconfig`), `ARDUINO_USB_MODE=1`/`CDC_ON_BOOT=1`.
Draws "Hello, world" with FreeInkUI's 24 px Noto Sans at 3x. Image 394,176 B,
sha256 `4580b740…`; PSRAM via IDF `CONFIG_SPIRAM_BOOT_INIT` (`cpu_start.c.obj`
references `esp_psram_init`; with stock config the Arduino-side reference is
absent by design). **Written at 0xE0000 only, hash verified**
(`write-flash --flash-mode keep --flash-freq keep --flash-size keep 0xE0000`),
overwriting stock `picco` 1.0.0+2918 per the no-backup ruling.

**It runs** (after the otadata write above; serial captured with one
controlled reset, port opened RTS-first): `board: picco`; `Picco CMD 0x70
reply 07 01 01`; panel **SE0400NQW47 on an SSD2677**, 800x480; SSD2677
reset/init/power-on, full refresh 426 ms, power-off, all BUSY waits complete;
free heap 332,904 B; **PSRAM 8,388,608 B detected, 8,289,420 free**; a log line
every 10 s. Screen content is UNCONFIRMED by any image: expected is a large
centered "Hello, world" plus "BOOX Picco - custom firmware" in portrait.
Upside-down text would point at the FreeInkUI Portrait (90° CW) transform.

## Stock flash layout and the one hazard

Upstream `partitions_picco.csv` (read from a retail unit):

| name | type | offset | size |
|---|---|---|---|
| nvs | data/nvs | 0x9000 | 0xC000 |
| otadata | data/ota | 0x15000 | 0x2000 |
| safeboot | app/factory | 0x20000 | 0xC0000 |
| app | app/ota_0 | 0xE0000 | 0xE70000 |
| spiffs | data/spiffs | 0xF50000 | 0x80000 |
| coredump | data/coredump | 0xFD0000 | 0x10000 |

CrossPoint must write **only** the app at 0xE0000. The vendor bootloader at
0x0, the NVS and the `safeboot` recovery image stay untouched. The upstream web
installer does exactly that (`crosspoint-tools` `src/lib/flasher.js`
`flashAppOnly()` / `pickBootAppPartition()`: reads the table at 0x8000 and
otadata, writes one image, flash mode/freq/size `keep`).

**Upstream issue #4248 (open):** `pio run -e picco -t upload` is NOT app-only.
`scripts/app_only_upload.py` only does `env.Replace(FLASH_EXTRA_IMAGES=[])`,
but the platform builder already appended each extra image to `UPLOADERFLAGS`,
so the upload also writes the Arduino bootloader at 0x0 (destroying the vendor
one), the partition table at 0x8000, and `boot_app0.bin` at 0xE000, inside the
stock NVS. Never use upstream's upload target on a Picco until the fix (strip
the pairs from `UPLOADERFLAGS` too) is in. Manual equivalent of the installer:

```
esptool --chip esp32s3 -p <port> write-flash --flash-mode keep --flash-freq keep --flash-size keep 0xE0000 firmware.bin
```

Upstream release `2.0.0` ships `crosspoint-2.0.0-picco.bin` (5,386,144 bytes,
sha256 `adf67272c0eb0546d9212b24e0b9e01b5f30268503a05c85d6e34aa26f491c8f`);
`esptool image-info`: ESP32-S3, 16MB/80m/DIO, checksum and SHA-256 valid, no
signature block.

## Where this fork stands (measured 2026-11-05)

- Fork `main` d1f0f33cc vs `upstream/develop` bb93a462c: **890 ahead / 456
  behind**, merge-base 1a7f5a9e3 (2026-07-31). Picco landed upstream in
  261726f61 (#3947, 2026-10-19), after the merge-base.
- Upstream firmware Picco commits: 261726f61, 49b84fdc1 (SDK pointer),
  471243b2d, fb2a8a8f1, 8f4d035cb, 5936f4850, 37138937b (SDK pointer).
- The fork also lacks prerequisites upstream's Picco code uses:
  `lib/hal/HalFrontlight.*`, `src/activities/util/FrontlightPanelActivity.*`,
  `src/util/SlideSwitch.h`, `src/network/FirmwareBoardTag.cpp`. Upstream has 40
  `lib/hal` commits and 54 SDK bumps since the merge-base.
- SDK: fork pin 4566e224d vs upstream's pin 5e94fe748: **31 ahead / 435
  behind** (327 non-merge). About 25 upstream SDK commits touch the Picco
  (`git log -G'[Pp][Ii][Cc][Cc][Oo]' 4566e224d..5e94fe748`). Of the fork's 31,
  `git cherry` shows 17 already upstream by patch-id; 14 are the fork's own:
  10 keyboard, 3 uc8253 X3 windowed refresh, 1 SD `removeDir` fix.

## The two routes being measured

- **A, keep the fork SDK:** cherry-pick only the Picco SDK commits and their
  prerequisites onto 4566e224d; port upstream's Picco firmware onto fork
  `main`, gated so X4/X3/Sticky do not change.
- **B, move the SDK to upstream's pin:** re-apply the 14 fork-only SDK commits
  (the 10 keyboard ones are expected to conflict: upstream reworked the same
  component, see `docs/fork-sync.md` 2026-09-14), fix the fork firmware against
  the moved SDK, then the same firmware port.

## Results (workflow `wf_f7b8d8a5-466`, 7 agents, 2026-11-05)

Per-agent JSON: `~/.claude/projects/-Users-natebunnyfield-src/c7c0168d-…/subagents/workflows/wf_f7b8d8a5-466/journal.jsonl`.
Builds ran without the gitignored commercial editor faces (absent in fresh
worktrees), so absolute Flash numbers sit below a primary-checkout build; the
comparisons are like for like.

Control: unchanged `main` d1f0f33cc + fork SDK, `pio run -e default`: RAM
55,668 B, Flash 4,968,141 B, bin 4,981,120 B.

### Route A: keep the fork SDK (branches `picco-a`, `picco-a-sdk`)

- SDK: 27 commits on 4566e224d (`picco-a-sdk` 1a44c59). Of the 27 upstream
  commits that mention the Picco, 19 taken (several trimmed to their Picco
  part), 11 skipped as not needed, 1 left for the owner; 9 prerequisites cut
  down to what the Picco needs. The PICCO board profile is byte-identical to
  upstream (109 compiled values match). Gaps: TMA525C is single-touch (no
  multi-touch gestures); touch-driver compile gating (95edf0c) not taken;
  SSD2677 driver text differs because the Sticky refactors were not taken.
- The 14 fork-only SDK commits are intact (keyboard.h and Uc8253X3Driver.cpp
  byte-identical).
- Firmware: `picco-a` 72d2e093d. `env:picco`/`env:picco-gh_release`,
  HalFrontlight, slide switch (Frontlight / Dark Mode), SGM41562 power and
  charger, sleep fixes, `CROSSPOINT_NO_DEVICE_FLASH` (one app slot, so no
  on-device OTA or SD flashing), #4248 upload fix, 7 strings in English and
  Spanish. Dropped hunks: everything touching the deleted reader menu,
  bookmarks, KOReader sync, FrontlightPanelActivity.
- **Picco build: SUCCESS.** firmware.bin 4,769,616 B (31.5% of the 0xE70000
  slot), RAM 67,800 B, sha256 `15aa456c…`; PSRAM init referenced. `esptool
  image-info` valid. Upload dry run (`pio run -e picco -t upload_plan`): the
  only write is `write-flash … 0xe0000 firmware.bin`.
- **X4 cost: RAM +32 B, Flash +1,560 B.** Sticky builds.
- Host tests: 766 passed, 2 skipped; `ActivityInputTest` does not compile
  (`SavedWifiJoin.h` vs the `HostWiFi` stub), not from this work (read, not
  rebuilt on main).

**Refuted by the skeptic (must fix before shipping):**
1. `83ac460` says the SSD1677 default SPI clock change was NOT taken; it was.
   `Ssd1677Driver.cpp:297` returns 20 MHz, and the Sticky uses the default
   (`displaySpiHz = 0`, BoardConfig.h:1372), so **the Sticky's display clock
   halves**. One-line fix: restore 40000000.
2. **The Picco image carries no board tag** (0 occurrences; X4 and Sticky have
   1): under `CROSSPOINT_NO_DEVICE_FLASH` nothing references `board_tag::TAG`
   and `--gc-sections` drops it, so a Sticky would accept a Picco bin from SD
   or WebDAV. Fix: a Picco-gated volatile read of `TAG[0]`.
3. The upload script's openocd strip never matches (`program_esp {{…}}`
   double braces); it fails closed (refuses), not a write hazard.

**New risks found:** with no touch (this fork's ruling, `docs/daisywheel.md`
2026-08-05) and only Power/Up/Down (+ an unconfirmed GPIO5 key), Up/Down cannot
move a list selection (next/previous maps only to the front Left/Right pair,
`MappedInputManager.cpp:99-110`), so **button-only navigation of Home and the
menus may be impossible**. Device-UNCONFIRMED. Also: Arduino `initArduino()`
erases the whole nvs partition if `nvs_flash_init` reports NO_FREE_PAGES or
NEW_VERSION_FOUND (esp32-hal-misc.c:328-334): upstream behaves the same; this
unit's stock IDF is 5.5.4, same major as ours. Holding Up during a Picco wake
enters recovery mode and skips reader resume. Upstream 45986f29e (landscape
side-button swap) is not in the fork.

### Route B: SDK to upstream's pin (branches `picco-b`, `picco-b-sdk`)

- The 4 non-keyboard fork SDK commits apply clean; **all 10 keyboard commits
  conflict as design choices** (upstream reworked the same drawKey code) and
  were not applied. Without them both fork keyboards render upstream's design:
  1 px outlined keys on a dithered gray pane, and the one-cell OK key renders
  as "…" again (the bug `e448f69` fixed).
- Fork firmware against the moved SDK: 5 errors, 2 root causes, both fixed
  (RAII bus Transaction port mirroring c760dc6/d967a5f; KeyboardIconTarget).
  `default`: RAM +328 B, Flash +17,882 B. Sticky builds. Picco: route A's six
  firmware commits apply with no conflicts; bin 4,785,616 B.
- **Shipped X4/X3 behavior changes (device-unconfirmed):** display SPI clock
  halves on every X4 and X3 batch (39606d5: 20 → 10 MHz); X4 UC8279 units
  gain windowed refresh; X4 units reading LUT_VER 0x03/0x41/0x42/0x67, or a
  blank-MTP probe, switch display driver; X4 UC8179 dark-gray waveform changes
  (kDarkGrayLut); X3 UC8279 loses four-tone image grayscale (3080ca2);
  networking (DNS pre-check, header limits); atomic `Storage.writeFile`;
  aborted deep sleep now restarts.

### Build trap found (both routes; matters for any checkout that builds both picco and sticky)

pioarduino's custom-sdkconfig rebuild check (`builder/frameworks/arduino.py:616-643`)
hashes only the custom sdkconfig text and the MCU. `picco` (dio_opi) and
`sticky` (qio_opi) share both, so switching between them compiles against one
memory type's `sdkconfig.h` and links the other's libs. **The image builds
SUCCESS with plausible sizes and never initializes PSRAM.** Route B's first
Picco image was exactly that (proved from firmware.map: `esp_psram_init` only
in discarded sections). Check every Picco image:
`grep esp_psram_init .pio/build/picco/firmware.map` must show a reference from
`esp32-hal-psram.c.o`. Route A's image passes.

### Touch for the keyboard only (owner 2026-11-05: *"make a touch version just for the keyboard on pico"*)

Workflow `wf_e8b0d536-359` (understand ×3, implement, review ×2, fix);
journal `…/subagents/workflows/wf_e8b0d536-359/journal.jsonl`. Branch `picco-a`
1eefad300 (8 commits on 72d2e093d), `picco-a-sdk` 5f34795.

- **What was already there:** `KeyboardEntryActivity` carries upstream's touch
  keyboard from the merge base (`fui::TouchHoldRouter`; a tap goes through
  `activateValue`, the same call a Confirm release makes;
  KeyboardEntryActivity.cpp:653-679). Before this work nothing gated it on the
  Picco, so on 72d2e093d every inherited Sticky-era touch path was live too
  (edge-swipe Back, Home/menu swipes, tap rows, reader tap zones).
- **Gate:** one chokepoint in `lib/hal/HalGPIO.cpp`, Picco-only. Every touch
  query returns "no touch" unless `setKeyboardTouch(true)`; `wasSwipe` is always
  false on the Picco. Setting the flag calls `suppressTouchContact()`, so a
  finger already down cannot type when it lifts. `hasTouch()` is NOT gated,
  since it sets the hint bar height and gating it would move every keyboard by
  40 px. All four keyboard surfaces opt in: full-screen grid/QWERTY, the daisy
  wheel, and the note editor and Claude chat strip.
- **Fixes in the same branch:** SSD1677 default SPI back to 40 MHz (5f34795;
  disassembly of the Sticky ELF loads 0x2625a00); board tag kept in Picco
  images (b8c1c8a89; 1 occurrence now); **upload gate**: `app_only_upload.py`
  refuses `-t upload`/`upload_plan` unless this link's firmware.map places
  `esp_psram_init` and the octal driver (1eefad300, test `AppOnlyUploadTest`
  with mutation checks).
- **Evidence:** `KeyboardTouchTest` 20/20 (every key's center and corners on
  the 13-Grid, QWERTY EN/ES with all layers, the panel grids, the daisy strip
  and wheel; misses off-key). X4 default: **RAM +0, Flash +0**, and the X4 ELF
  contains none of the touch symbols. Picco: RAM +0, Flash +4,372. Picco bin
  4,773,984 B, image-info valid, PSRAM coherent, board tag 1. The bin's hash
  changes on every build, because `build_html.py:110` gzips without `mtime=0`.
- **The 13-Grid's 'z' was dead to touch upstream too:** the 64-slot
  interaction table overflowed at 65 keys. Fixed by sizing the table to the
  key count (`KEY_SLOTS`).
- **SHIPPED, UNCONFIRMED on device (nothing flashed):** TMA525C app mode,
  calibration and orientation, inertness elsewhere, one refresh per tap, feel.
- **Review findings not yet fixed (all minor):** a touch that fires returns
  before the pick-button code, so a button released in the same pass is lost;
  `resetTouch()` keeps the active index; a tap can type a key before the first
  render; a finger resting anywhere disables the buttons; a lost TMA525C
  lift-off becomes a phantom hold, which can fire Del's clear-all. Gates are
  proposed for the board tag and the SSD1677 default clock.
- **Blocking ruling needed:** with swipes inert, **the Picco has no Back on
  any screen** (no Back/Left/Right buttons; the left-edge swipe was its only
  Back). Create Note and Settings cannot be exited. Also Up/Down do not move a
  list selection (`MappedInputManager.cpp:99-110`), and GPIO5's physical role
  as Confirm is unconfirmed.

**Ruling 2026-11-06, Back on the Picco: "Left-edge swipe = Back".** The one
touch gesture kept outside keyboards: a left-edge swipe (≥60 px) is Back on
every Picco screen, keyboards included. Home/menu swipes, tap rows and reader
tap zones stay inert. Accepted risk: a sloppy drag from the left quarter of a
keyboard cancels. Do not re-ask.

**Left-edge Back build (workflow `wf_e34cc877-62d`), `picco-a` c997ecf57:**
left-edge swipe → Back via `HalGPIO::wasBackSwipeCandidate` + `edgeswipe::isLeftEdgeBack`
(start x ≤ 25%, rightward, more across than down); the five minor fixes;
reviewer's major (Back swipe sampled at 20 Hz on battery) fixed by polling at
100 Hz while a contact is down (2ae48151e). KeyboardTouchTest 26/26. X4 default
RAM/Flash +0, with `nm -S` identical. Open minors: the drawn layer is published
before `displayBuffer()`; the panel grids have no first-draw gate; touch
hold-Del clear-all was removed on the Picco (upstream has it).

**Flashed 2026-11-06** (0xE0000 only, hash verified; otadata already ota_0,
re-read first): bin 4,774,832 B, sha256 `409ae8bd…`. Boot log: charger
configured, RTC found, SDMMC mounted, frontlight 60%/warm 50%/off, SE0400NQW47
panel, `[OTA] running app, state=2; already committed`, Home "row 0 of 8",
heap free 247,972. To look at: `[MAIN] Hardware detect: X4` (mislabel),
`gpio_isr_handler_remove … GPIO isr service is not installed`, and no TMA525C
log line. Touch is UNCONFIRMED.

**Ruling 2026-11-06, Picco menu navigation:** asked how the Up/Down rocker
and Power should drive menus (Up/Down only page and never move the selection;
Select sits on GPIO5, whose physical existence is unconfirmed), the owner
answered: *"adopt the norms from cross-point simulator. and the default iOS
app settings"*. The Picco's input mapping and its settings defaults follow
`~/src/crosspoint-simulator` and its iOS app's defaults. Mapped before
implementing (workflow `wf_6d0d1153-fff`; simulator at 0842477c, picco-a at
c997ecf57; five claims spot-checked against the source):

- **On iOS, no firmware touch path ever runs.** `hasTouch()` is false for the
  X3 profile (simulator `src/BoardConfig.h:173`); the harness turns every
  gesture into a 60 ms button tap (`ios/CrossPointZenRecognizers.mm:343`).
  Zen ships ON (`ios/CrossPointPrefs.mm:333-336`).
- iOS zen defaults (`ios/GestureBindings.h:638-689`): tap = Right, swipe left
  = Right, swipe right = Left, swipe up = Back, swipe down = Confirm, hold
  0.75 s = Confirm, hold above the paper = Power, Volume Up = Right, Volume
  Down = Left. Two-finger, pinch, shake and tilt bindings exist, but the
  TMA525C is single-touch and the Picco has no motion sensor in use, so
  those are dropped.
- **Settings:** the iOS build changes no pinned firmware setting (it only
  seeds darkMode, `ios/CrossPointIOSShim.cpp:1812`), so adopting the iOS
  defaults needs no settings change.
- **Carried to the Picco:** rocker upper = Right (next row, page or option),
  rocker lower = Left; Power = sleep; outside keyboards, gestures are
  synthesized buttons (tap = next, swipes = next/previous, swipe up = Back,
  swipe down and hold = Confirm). The left-edge swipe stays Back. Firmware tap
  rows and tap zones stay gated.
- **Conflicts:** (A) the keyboard-only touch ruling; (C) inside keyboards iOS
  tap = move a column, but the Picco tap types the key under the finger;
  (E) the rocker stops sending Up/Down, so list paging (PageNext/Previous),
  keyboard row moves and IntervalSelection's large step become unreachable
  (font size and family stay in Settings > Reader).
- Code: Picco-only `mapButton` remap and a synthetic gesture mask in
  `src/MappedInputManager.cpp`; ungated `wasGestureTap`/`wasGestureLongPress`
  in HalGPIO; a unit test for geometry → button. X4/X3: zero by construction.

**Ruling 2026-11-06, Picco input = the full iOS norm, keyboards type.** Owner:
*"rocker is currently pg up and pg dn, but should be left right or whatever.
but yes to first option"*. So: the rocker sends Right (upper) and Left (lower)
instead of page up/down, everywhere. Outside keyboards, gestures are
synthesized buttons per the iOS defaults (tap = Right, swipe left = Right,
swipe right = Left, swipe up = Back, swipe down = Confirm, hold = Confirm), and
the left-edge swipe stays Back. Inside keyboards, a tap types the key under
the finger; the left-edge swipe is Back; other gestures are off. Power =
sleep. Rocker list paging and IntervalSelection's large step are given up
(accepted). This supersedes "touch just for the keyboard" for gestures; the
firmware's own tap-row and tap-zone paths stay gated. Do not re-ask.

**Ruling 2026-11-06, route: "Route A".** `picco-a` / `picco-a-sdk` is the
branch to ship (via `cpship`) once the iOS-norm build passes review. `picco-b`
/ `picco-b-sdk` stay as unmerged reference branches; nothing is deleted. Do
not re-ask.

**Ruling 2026-11-06, frontlight: "Two Settings rows".** Brightness and Warmth
become plain rows in the Picco's device Settings list, stepped with the
rocker, applied live. Picco only, existing row style, no new screen. Queued
behind the iOS-norm build (same worktree).

**iOS-norm build (workflow `wf_ac1d5d54-fe9`), `picco-a` 1d623d68c:**
`src/components/PiccoInput.h` (rocker table + GestureDecoder),
`PiccoInputTest` 16/16 with mutation checks, KeyboardTouchTest 26/26. Default
RAM/Flash +0, `nm -S` identical. Picco bin 4,775,264 B, sha256 `5ba6ef17…`,
upload_plan OK. The review found 5 minors: hold 500 ms vs iOS 750 ms and no
tap time limit (iOS ≤400 ms / 28 px); after 1 s idle, touch is sampled once
per 50 ms light-sleep slice, so short taps can be missed; a lost lift-off
turns a tap into Confirm; a gesture made while the same rocker end is held
releases it early; the worktree's doc copy is stale.
**Not yet flashed:** at the flash attempt the Picco had deep-slept on idle.
The port node stayed listed, but both `default-reset` and `usb-reset` got "No
serial data received", so a sleeping CrossPoint cannot be reflashed over USB.
Next build: stay awake (or keep USB-Serial/JTAG alive) while a USB host is
connected.
**Flashed 2026-11-06** after the owner woke it with Power (otadata re-read:
still ota_0): wrote 4,775,264 B at 0xE0000, hash verified; boots to Home,
heap free 247,940. Rocker and gestures UNCONFIRMED on device.

**Ruling 2026-11-06, gesture timing: "Match iOS exactly".** Hold = 750 ms
(`ios/ZenHoldRouting.h:38`). A tap counts only if it lasts ≤400 ms and moves
≤28 px; anything longer or driftier does nothing. Plus the idle-sampling fix,
so short taps after idle are not missed.

**Ruling 2026-11-06, text-field tap: "Keep it".** In the full-screen keyboard,
a tap in the text field moves the cursor there (upstream behavior,
KeyboardEntryActivity.cpp:638-651). No code change.

**Ruling 2026-11-06, touch hold on Del: "Keep the guard".** A touch hold on
Del deletes one character (81e63a160); clear-all stays on held Select.

**Ruling 2026-11-06, build gates: "Add all three".** (1) `picco-gh_release`
joins `scripts/check_editor_faces.py` `RELEASE_ENVS`; (2) a post-build check
fails any device env unless firmware.bin has exactly one
`CROSSPOINT-BOARD-V1:<env>;`; (3) an SDK host test asserts the SSD1677 40 MHz
default. Queued for the build after the one now running.

**Settings + timing build (workflow `wf_d9b851c5-9fd`), `picco-a` 893709214;
details in the branch's `docs/picco-input-settings-2026-11-06.md`:** iOS
timing (tap ≤400 ms and ≤28 px, hold 750 ms); stale-contact guard; a gesture
never releases a held rocker end; 100 Hz poll for 1 s after any report, and
the TMA525C INT (GPIO11) wakes light sleep; stays awake while
`isUsbConnectedCached()` (VBUS or SOF); Brightness/Warmth rows (Confirm starts
stepping, rocker ±5, Confirm/Back saves) and Slide Switch / Restore Light on
Wake rows. PiccoInputTest 31/31. X4: RAM +0, Flash +8 B (two `__TIME__`
strings a second apart no longer merge). Picco bin 4,776,672 B, sha256
`9bde8d0c…`, upload_plan OK. Open: if the TMA525C stops reporting a resting
finger (SDK f8274f1), the 200 ms freshness rule kills hold = Confirm; measure
with TOUCH_PROBE_DEBUG. Minors: Restore Light on Wake does nothing by default
(upstream hides it); binding the switch to Dark Mode strands the frontlight;
flipping the Slide Switch row moves the selection; stay-awake keys on VBUS,
so a wall charger keeps it awake.

**Ruling 2026-11-06, Restore Light on Wake row: "Hide it then".** Shown only
when the slide switch is bound to Dark Mode (`!slideSwitchOwnsLight()`,
upstream's rule). Queued for the next build.
**Ruling 2026-11-06, Frontlight on/off row: "Add the row then".** When the
switch is bound to Dark Mode, a Frontlight on/off row appears beside
Brightness and Warmth; hidden while the switch owns the light. Picco only.
Queued for the next build.
**Ruling 2026-11-06, stay awake: "Any USB power".** Keep the build's rule
(`isUsbConnectedCached()`: VBUS or SOF). A wall charger also keeps the Picco
awake, with the light as left and no sleep screen. Do not re-ask.
**Not yet flashed:** the Picco was asleep on the old firmware; a short Power
press did not wake it (USB session unbroken for 140 min; the port carries
wake-on-held-button). The owner chose "Later".

**Rows + gates build (workflow `wf_811915c1-a9f`), `picco-a` 8ddbaef65;
details in the branch's `docs/picco-light-rows-gates-2026-11-06.md`:**
Restore Light on Wake hidden unless the switch is bound to Dark Mode (upstream's
full rule, `!Frontlight.present() || slideSwitchOwnsLight()`); Frontlight
on/off row under the same rule; selection kept across a Slide Switch flip;
gates: `picco-gh_release` in `RELEASE_ENVS`, `scripts/check_board_tag.py`
post-action (expected tag parsed from FirmwareBoardTag.cpp's `#if` chain;
deletes a failing bin; proven by a forced-wrong-tag run that FAILED and then
passed after restore), SDK `expectSsd1677SpiDefault()` (a 20 MHz mutation
aborts). X4: RAM/Flash +0 against 1d623d68c. Picco bin 4,777,120 B, sha256
`2dd576bb…`.
**Flashed 2026-11-06** (owner: "picco is up", then "go"; parked in the ROM
bootloader first so it could not sleep; map/elf same second, `esp_psram_init`
placed, octal only, tag once): boots to Home, heap 247,068. Its boot log
showed `FIXED_ENTRY_COUNT is 21 but the fixed block pushes 22`. Cause: main's
188a3fa2a (2026-10-08, Screen Margin picker back) added a 17th fixed row
without bumping the count, so every board logs it. Only a `reserve()` hint.
Fixed in d0a905214 (not yet built).
**Owner, 2026-11-06, after trying it on the device: "all works".** The rocker,
gestures (including hold = Confirm, so the stale-contact rule does not kill
real holds on this unit), the light and slide-switch rows, touch typing in
keyboards, and staying awake on USB are confirmed on the device. The
TOUCH_PROBE_DEBUG measurement is no longer needed.

**Main build, flashed 2026-11-07** (owner: "go"). `main` 6a01d23a2 (the
merge; its tree is `picco-a` plus d0a905214) built in the PRIMARY checkout
from clean PlatformIO caches, the Sep-7 generated `sdkconfig.defaults` set
aside first (build trap above). Bin 5,207,936 B, sha256 `7cbdf2cb…`; map has
`esp_psram_init` from `esp32-hal-psram.c.o`; board tag once; `upload_plan` OK
(one image at 0xe0000). Flash 4,776,618 → 5,207,426 B (+430,808) against the
picco-a image: all of it DROM (segment 0x293cf4 → 0x2fce44), IROM flat. Cause:
the licensed editor faces (`lib/EpdFont/builtinFonts/pragmatapro_*` and
`nitti*`, 24 headers each, gitignored, `.gitignore:46`) exist only in the
primary checkout, so every worktree-built Picco image so far lacked them;
`scripts/check_editor_faces.py` requires them in release images. Build Picco
release images from the primary (or copy those headers into the worktree).
Boot after flashing (app-only at 0xE0000, then reset): Home, `OTA state=2;
already committed`, heap 243,656 free (was 247,068, −3,412 B), and **no
`FIXED_ENTRY_COUNT` error** (the check runs on the first settings load, which
happens at boot; the 2026-11-06 boot log showed it).

**Ruling 2026-11-06, slide switch: "Add both rows".** "Slide switch"
(Frontlight / Dark Mode) and "Restore light on wake" join the Picco's device
Settings list next to the frontlight rows. Picco only.

### Open questions for the owner (from the port)

Frontlight brightness/warmth has no on-device UI (web settings only); touch is
out of scope by fork ruling but the Picco lacks Back/Left/Right; Slide Switch
and Restore Light on Wake rows are web-only; the switch's Touch Controls
binding is left out; `picco-gh_release` is not in `check_editor_faces.py`
`RELEASE_ENVS`; no fork CI entry; the OTA boot confirmation is kept on the
Picco.

## Rulings

- **2026-11-05, no factory backup.** Asked whether to read the full 16 MB flash
  before flashing (download mode, read-only); the owner chose **"Skip the
  backup"**. Consequence accepted: flashes stay app-only at 0xE0000, but there is
  no image to restore the stock app or NVS from if they are damaged. Do not
  re-ask.
- Upstream release `2.0.0` was **not** flashed: the owner's next instruction was
  to build this fork for the Picco instead.
