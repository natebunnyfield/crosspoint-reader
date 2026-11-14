# USB Drive on the BOOX Picco: the foundation (2026-11-08)

Status: **code landed gated OFF; both build options compile- and link-proven;
nothing flashed; no entry point.** Every device behavior below is
**UNCONFIRMED on device**.

**Ruling needed (owner): A or B′.** Turning USB Drive on for the Picco needs a
build config, and the two that work cost about the same in flash and static
RAM but differ in boot heap and in how much pioarduino machinery they lean on
(table and detail under "Build config"):

- **A**: the Picco leaves the core rebuild and uses the prebuilt `dio_opi`
  core, like upstream's X4 Pro. About 4.4 KB less free internal heap at boot
  (static estimate: two system task stacks), no extra scripts.
- **B′**: the Picco keeps the fork's core rebuild, plus
  `scripts/picco_tinyusb_config.py`, a build middleware that depends on five
  pioarduino/PlatformIO internals.

Until then `main` builds the Picco exactly as before: the code is under
`#if FREEINK_CAP_USB_MSC`, which no env sets, the Picco envs in
`platformio.ini` are unchanged (one comment), the script is committed but not
wired into any env, and the firmware's SDK pointer stays at `1dc07f6`.

Owner ruling 2026-11-08 on upstream `218dc6aaf` ("feat: add usb mass storage
mode for x4 pro (#3203)"): *"build"*, for the Picco (`docs/fork-sync.md`, "USB
mass storage, `218dc6aaf`"). Only the Picco can have it: its SD is native 4-bit
SDMMC (`[env:picco]`, `-DUSE_BLOCK_DEVICE_INTERFACE=1`), which
`HalStorage::beginUsbDrive()` requires (`#error` otherwise), and the ESP32-C3 in
the X4/X3 has no USB-OTG.

**Not here, by instruction:** any entry point (a Home row, a Settings row, a
File Transfer choice) and any decision about the screen's look. Both go to the
owner as a rendered visual first. `NetworkModeSelectionActivity`, upstream's
entry, stays deleted. The one call the entry point makes is
`activityManager.goToUsbDrive()` (`src/activities/ActivityManager.h`, USB Drive
builds only).

Surveyed at fork `main` `a88d91fa1`, upstream `218dc6aaf`, fork SDK `1dc07f6`,
upstream SDK `3e4d651` (the SDK pointer at `218dc6aaf`).

## Design (upstream's, kept)

- **USB Serial/JTAG stays the normal USB personality** (flashing and logs;
  `ARDUINO_USB_MODE=1` from `[base]`). MSC runs on the USB-OTG PHY only while
  `UsbDriveActivity` is open: `Storage.beginUsbDrive()` calls the SDK's
  `freeink::UsbMassStorage::begin()`, whose `USB.begin()` moves the PHY to OTG.
- **Leaving always reboots.** `UsbDriveActivity::restartToHome()` ->
  `Storage.endUsbDrive()` -> `restartToHomeAfterStorageHandoff()` (main.cpp):
  Loading popup, `handoffUsbOtgToSerialJtag()` (stops OTG, reselects the
  Serial/JTAG PHY, forces a host-visible disconnect and waits up to 1 s for the
  bus reset), RTC silent-reboot target Home, `ESP.restart()`. The card is
  remounted by the normal boot, never in place.
- **Ways out.** Host eject (SCSI START STOP UNIT with eject) or cable removal
  after a host mounted it; Back, Power or the Home gesture only while no host
  has it (WaitingForHost) or after a start failure; 5 min with no host; 30 s
  after a start failure; an I/O error asks the host to disconnect
  (`tud_disconnect`) and reboots 1 s later. While a host has the card, input
  does nothing: the device side cannot pull the card out from under a mount.
- **Exclusive loop.** `Activity::requiresExclusiveStorageLoop()` (true for this
  activity only). While it is current, `ActivityManager::loop()` runs only the
  activity (no pending transitions, no Home gesture) and `loop()` in main.cpp
  returns right after `mappedInputManager.update()`: no slide-switch settings
  save, no serial screenshot, no idle, power-button or low-battery sleep, no
  main-loop light sleep (the e-ink BUSY slice still can: Safety 7). 10 ms loop
  while a host has the card, 50 ms otherwise, with the CPU at `LOW_POWER_FREQ`
  (80 MHz with `BOARD_HAS_PSRAM`, as upstream's `HalPowerManager.h` has it at
  `218dc6aaf`).

## What was taken from upstream `218dc6aaf`, and what was not

Taken (adapted):

| Upstream | Here |
|---|---|
| `lib/hal/HalStorage.{h,cpp}` USB Drive API | same API and state mapping, all under `#if FREEINK_CAP_USB_MSC`; plus the flush and the refusal guard (Safety) |
| `src/platform/UsbSerialJtagHandoff.{h,cpp}` | register sequence unchanged; the no-op fallback body dropped (its only caller is MSC-only) |
| `src/SilentRestart.h`, `main.cpp` `restartToHomeAfterStorageHandoff()` | same, MSC-only |
| `main.cpp` exclusive loop branch | same, placed BEFORE the fork's Picco-only `handleSlideSwitch()` (an SD write); MSC-only |
| `Activity.h` / `ActivityManager.{h,cpp}` hooks and `goToUsbDrive()` | same, MSC-only, so X4/X3 vtables and loop are untouched |
| `src/activities/network/UsbDriveActivity.{h,cpp}` | state machine unchanged; screen is upstream's layout through `GUI` (Lyra Six header and hints) and FreeInkUI text (UI_10/UI_12, built-in faces) |
| English and Spanish strings | 8 keys; Spanish gains `STR_USB_DRIVE_CONNECT_DELAY`, which upstream left to the English fallback |
| `platformio.ini` USB flags | NOT committed (waits for the A/B′ ruling). The Picco lines either option needs: `-DFREEINK_CAP_USB_MSC=1`, the `UsbMassStorage` lib_dep, `-DUSB_PRODUCT=\"CrossPoint_Picco\"`, `-DUSB_MANUFACTURER=\"CrossPoint\"`, in both `[env:picco]` and `[env:picco-gh_release]` (which restates build_flags) |

Not taken:

- `NetworkModeSelectionActivity` and `CrossPointWebServerActivity` hunks
  (upstream's entry point; deleted subsystem here), the `UIIcon::Usb` icon and
  its `listIcons` entry and `UiAppHelpers` case (a menu row's icon belongs to
  the entry point), `STR_USB_DRIVE_DESC` (the menu row's subtitle, same
  reason).
- The 30 other languages; the X4 Pro and Paper Mono envs.
- Upstream's `[firmware_tuned]` split, which takes the X4 Pro OFF the core
  rebuild to get the prebuilt TinyUSB. Whether the Picco does the same is the
  open A/B′ ruling (Build config below).
- Upstream's `AfterUSBPower` hunk: the fork's Picco already stays awake after a
  USB-powered cold boot (main.cpp, `case HalGPIO::WakeupReason::AfterUSBPower`).

## SDK

Correction to the brief: **the fork SDK pin `1dc07f6` already has
`SDCardManager::detachFilesystemForRawAccess()`**
(`libs/hardware/SDCardManager/src/SDCardManager.cpp:78`, declared at
`include/SDCardManager.h:87`), from upstream `d123567` ("feat: add USB storage
and serial transport for X4 pro"), which `git merge-base --is-ancestor d123567
1dc07f6` confirms. What `1dc07f6` lacked is in `UsbMassStorage` only:

```
git -C freeink-sdk remote add upstream https://github.com/Free-Ink/freeink-sdk.git
git -C freeink-sdk fetch upstream
git -C freeink-sdk log --oneline 1dc07f6..3e4d651 -- libs/hardware/SDCardManager libs/hardware/UsbMassStorage
#   19fb1a0 fix: recover USB mass storage after I/O errors        <- taken
#   e9f8711 fix: report USB disconnect after storage errors       <- taken
#   3b6226e Merge ... 43b621e drop redundant exists() check        <- not USB, not taken
```

Branch `usb-drive` in the submodule, from `1dc07f6`, local only (not pushed):

| SDK commit | from upstream | what |
|---|---|---|
| `22af513` | `e9f8711` | `state()` reports a cable removal after an I/O error instead of latching IoError |
| `7ec5a91` | `19fb1a0` | `disconnectHost()` (`tud_disconnect`), and an I/O error latches host-seen |

Both applied clean. After them `git diff 3e4d651 usb-drive --
libs/hardware/UsbMassStorage` is empty. The rest of the
`1dc07f6..3e4d651` difference in `SDCardManager` is the fork's own Picco work
(`shutdown()`, the long-name `removeDir` fix, the SD power-cycle flag) and is
untouched. **The submodule branch has not been pushed, and the committed
firmware tree still points at `1dc07f6`**: with the cap off nothing compiles
`UsbMassStorage` (it is not even a lib_dep), so the pin does not have to move
yet. Turning the cap on needs `disconnectHost()` (`7ec5a91`): push `usb-drive`
to `natebunnyfield/freeink-sdk` and move the pointer in the same change that
sets the cap.

## Build config: A vs B′ (measured)

The fork's `[base]` has `custom_sdkconfig` (heap reclamation, ~32-37 KB on the
C3; on the Picco's S3 it is worth about 4.3 KB, see "A vs B′ boot heap"), so
pioarduino rebuilds the ESP-IDF libraries for the Picco's memory variant
(`esp32s3/dio_opi`). Upstream's X4 Pro deliberately skips that rebuild because
"the custom SDK rebuild omits" TinyUSB. Option A (do the same for the Picco)
is an architecture choice for the owner; option B (keep the rebuild, add
TinyUSB to it) was built first, as briefed, and both were then measured.

### Option B as briefed: FAILS

`[env:picco]` gets its own `custom_sdkconfig = ${base.custom_sdkconfig}` plus
`CONFIG_TINYUSB_ENABLED=y`, `CONFIG_TINYUSB_MSC_ENABLED=y`,
`CONFIG_TINYUSB_MSC_BUFSIZE=4096` (the values the stock
`qio_qspi/include/sdkconfig.h:500-508` carries), the USB flags and the
`UsbMassStorage` lib_dep. `pio run -e picco`, 2026-11-08, fresh worktree, no
`sdkconfig.defaults`:

- `*** Reinstall Arduino framework ***`, `*** Compile Arduino IDF libs for
  picco ***`; the generated `sdkconfig.defaults` carries all three lines
  (`Replace: CONFIG_TINYUSB_ENABLED=y with: CONFIG_TINYUSB_ENABLED=y`, and
  31 `TINYUSB` lines in total, because the stock template the rebuild starts
  from, `esp32s3/sdkconfig`, already has them).
- **Kconfig drops every one.** The rebuild's own config,
  `.pio/build/picco/config/sdkconfig.h`, and its `sdkconfig.picco` have **0**
  `TINYUSB` lines. Nothing in that build defines the symbols: they belong to the
  lib-builder's `arduino_tinyusb` component, which the hybrid build does not
  have (`build_lib/idf_component.yml` pulls only `fb_gfx` from the lib-builder;
  `grep -rl "config TINYUSB_ENABLED\|config TINYUSB_MSC_ENABLED"` over
  `framework-espidf/components` and `framework-arduinoespressif32` finds
  nothing). The same drop was already visible in the primary checkout's
  2026-11-07 Picco rebuild (`sdkconfig.defaults` 31 lines in,
  `sdkconfig.picco` 0 out).
- **The build fails inside the core rebuild itself**, before it copies any
  library: pioarduino compiles the project's `lib_deps` in that pass too, with
  the project's `-D` flags (PlatformIO turns `BUILD_FLAGS` into `CPPDEFINES`
  before any framework script runs, `platformio/builder/tools/piobuild.py`
  `ProcessProgramDeps`, lines 118-122), so `-DFREEINK_CAP_USB_MSC=1` reaches
  `UsbMassStorage.cpp` against a config with no TinyUSB:
  `UsbMassStorage.cpp:19:1: error: 'USBMSC' does not name a type` (and
  `'gMsc'`, `'USB' was not declared`), `[FAILED] Took 81.63 seconds`.
- So the measured answers: links with MSC: **no, does not compile**; rebuilt
  `dio_opi/include/sdkconfig.h` carries `CONFIG_TINYUSB_MSC_ENABLED`: **no**
  (the rebuild's output has none; the package kept its reinstalled stock
  header only because the failed pass never copied).

### Option B′: B + `scripts/picco_tinyusb_config.py`

Still the core rebuild, still `ARDUINO_USB_MODE=1`. Two facts make it work:

1. **TinyUSB itself survives the rebuild.** The rebuild replaces only the
   archives it compiles; `esp32s3/lib/libarduino_tinyusb.a` (2,016,150 B,
   stock, dated with the package) stays, its headers stay
   (`include/arduino_tinyusb/`), and `pioarduino-build.py` still links
   `-larduino_tinyusb`. Only the configuration the Arduino core and
   `tusb_config.h` read is missing.
2. **That configuration can be supplied from the package's own stock header**,
   the same lib-builder run that compiled the archive. The stock and rebuilt
   `sdkconfig.h` differ, for anything this touches, only in the 31
   `CONFIG_TINYUSB_*` values and two task stack sizes,
   `CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH` (3120 stock, 2560 rebuilt) and
   `CONFIG_ESP_TIMER_TASK_STACK_SIZE` (8192 stock, 4096 rebuilt), neither a
   struct layout (`diff` of the stock config against the rebuilt
   `sdkconfig.picco`, filtered on STACK, TASK, HEAP, IRAM, SPIRAM, MALLOC,
   POOL, BUF; the rest of the difference is components the rebuild removes:
   rainmaker, insights, matter, camera, BLE mesh).

The script is a `pre:` script that registers a PlatformIO build middleware
(`env.AddBuildMiddleware`), so it acts per source file AFTER the framework
script has said which pass this is (`ARDUINO_LIB_COMPILE_FLAG`, `"Build"` in
pioarduino's core rebuild). It edits the `CPPDEFINES` of the env that compiles
the file and hands the same File node on; returning `env.Object(...)` (the
first version) broke pioarduino's own middleware in the rebuild pass
(`espidf.py:2439-2450` `_skip_prj_source_files` calls `node.srcnode()`:
`AttributeError: 'NodeList' object has no attribute 'srcnode'`).

- normal pass: every C/C++ file PlatformIO compiles (Arduino core, libraries,
  `src/`) gets the stock header's numeric `CONFIG_TINYUSB_*` values (23 of the
  31, `qio_qspi/include/sdkconfig.h:500-530`; the 8 `CONFIG_TINYUSB_DESC_*`
  descriptor-name strings are not ABI and no compiled path uses them), except
  names the active variant's own `sdkconfig.h` already defines;
- core-rebuild pass: library files are compiled WITHOUT
  `FREEINK_CAP_USB_MSC` and `CONFIG_TINYUSB_*`. Those objects are thrown away
  (espidf.py deletes the build dir after copying the libraries), and the
  rebuild's ESP-IDF components never pass through the middleware at all.

It fails the build when the stock header or the archive is missing, or when
the stock values do not enable TinyUSB with MSC, and does nothing without
`-DFREEINK_CAP_USB_MSC=1`. `FREEINK_CAP_USB_MSC=1` itself stays in
`platformio.ini` build_flags, so every normal-pass file sees the same value
(it changes the `Activity` vtable and `HalStorage`'s layout, so a per-file
difference would be an ODR break).

B′ keeps the TinyUSB lines in the Picco's `custom_sdkconfig` although they are
inert for the config: they make the Picco's rebuild hash (MD5 of the config
text + MCU, `arduino.py` `matching_custom_sdkconfig()`) differ from the
Sticky's, which closes the picco/sticky build trap in
`docs/picco-port-2026-11-05.md` (switching envs now forces a rebuild instead of
linking the other memory type's libraries).

The B′ lines exactly as measured, not committed (the A rows used the same USB
lines without `custom_sdkconfig` or the middleware):

```ini
[env:picco]
extra_scripts =
  ${base.extra_scripts}
  pre:scripts/picco_tinyusb_config.py
  post:scripts/app_only_upload.py
lib_deps =
  ${base.lib_deps}
  FrontlightManager=symlink://freeink-sdk/libs/hardware/FrontlightManager
  UsbMassStorage=symlink://freeink-sdk/libs/hardware/UsbMassStorage
custom_sdkconfig =
  ${base.custom_sdkconfig}
  CONFIG_TINYUSB_ENABLED=y
  CONFIG_TINYUSB_MSC_ENABLED=y
  CONFIG_TINYUSB_MSC_BUFSIZE=4096
build_flags =
  ; ...the existing Picco flags, then:
  -DFREEINK_CAP_USB_MSC=1
  -DUSB_PRODUCT=\"CrossPoint_Picco\"
  -DUSB_MANUFACTURER=\"CrossPoint\"

[env:picco-gh_release]
build_flags =
  ; ...the existing flags, then the same three USB lines
```

### Option A: the prebuilt `dio_opi` core

Measured from a scratch copy of `platformio.ini` (`pio run -c <copy> -e
picco`), never committed. It has upstream's layout: `[base]`'s
`custom_sdkconfig` and `custom_component_remove` move into a
`[firmware_tuned]` section; `default`, `gh_release`, `gh_release_rc`, `slim`
and `sticky` extend `base, firmware_tuned` (their resolved configs come out
identical to today's apart from the `extends` key, checked with
`pio project config --json-output`); `[env:picco]` extends `base` only and gets
the USB lines above, with no `custom_sdkconfig` and no middleware.
pioarduino then reinstalls the stock framework and compiles no core libraries.
The stock `dio_opi/include/sdkconfig.h` carries all 31 `CONFIG_TINYUSB_*`
values itself, so nothing has to be injected. PSRAM is initialized by ESP-IDF
at boot (`esp_psram_init` referenced from `libesp_system.a(cpu_start.c.obj)`)
instead of by Arduino's `psramInit()` (`esp32-hal-psram.c.o`); the
`app_only_upload` gate accepts both. The only device evidence for A: the
`~/src/boox-picco/hello/` image that booted on this unit on 2026-11-05 used the
same platform's stock core (no `custom_sdkconfig`, IDF PSRAM boot init) and
found its 8 MB of PSRAM (`docs/picco-port-2026-11-05.md`).

### Results

Every row but the first: `pio run -e picco -t upload_plan` in this worktree,
base `a88d91fa1`, SDK `usb-drive` `7ec5a91` (cap on) or `1dc07f6` (cap off), no
gitignored commercial editor faces (same as the baseline). RAM and Flash are
the build summary's (`.dram0.data+.dram0.bss+.noinit`, and
`.iram0.*+.dram0.data+.flash.text+.flash.rodata`). "Rooted" rows add
`PLATFORMIO_BUILD_FLAGS='-Wl,-u,_ZN15ActivityManager12goToUsbDriveEv'`: the
linker keeps `goToUsbDrive()` as a GC root, so the whole USB Drive path links
the way it will once an entry point calls it. Unrooted, `--gc-sections` drops
all of it (0 `tud_msc*` symbols), and those rows show only what turning the cap
on costs before anything calls it. (`__attribute__((retain))` would do the
same in source, but this Xtensa toolchain prints `'retain' attribute ignored`
and drops it anyway.)

| Picco build | links `tud_msc*` | RAM (static) | Flash | internal heap starts at |
|---|---|---|---|---|
| stock, cap off (batch-3 gate, worktree `probe`, read only) | no | 70,872 | 4,659,342 | 0x3fcae2d8 |
| B′ cap on, unrooted | no (gc'd) | 71,792 (+920) | 4,664,090 (+4,748) | |
| **B′ cap on, rooted** | **yes**, 21 `tud_msc*`/`mscd_*`, `USBMSC::begin`, `deinit_usb_hal` | **93,832 (+22,960)** | **4,722,214 (+62,872)** | 0x3fcb3d88 (+23,216) |
| A cap on, unrooted | no (gc'd) | 71,744 (+872) | 4,655,874 (−3,468) | |
| **A cap on, rooted** | **yes**, the same 21 | **93,784 (+22,912)** | **4,714,122 (+54,780)** | 0x3fcb3c58 (+22,912) |
| committed tree, cap off (this branch's final build, SDK `1dc07f6`) | no, 0 USB symbols | 70,872 (+0) | 4,660,250 (+908) | |

The committed cap-off image's +908 B is the 8 new strings in the i18n tables
(+723 B; `gen_i18n.py` keeps the keys it finds by text, gated code included)
and the sleep flush (`HalStorage::prepareForDeepSleep()` +80 B,
`SDCardManager::ready()` +8 B). The two unrooted cap-on rows' ~900 B of RAM is
the Arduino core's own USB globals, linked once its TinyUSB code compiles (the
`USB` object, `msc_luns`, the descriptor strings; `nm -S` against the stock
image). Both rooted images pass `app_only_upload: OK` (one image at 0xe0000,
PSRAM init and the octal driver placed) and `check_board_tag` (tag once).

**Where the ~23 KB of static RAM goes (both options alike).** `nm -S` of
unrooted vs rooted B′: 90 new `.bss`/`.data` symbols, 23,089 B. The largest:
`ncm_epbuf` 6,416, `_transfer_buf` 4,096, `_mscd_epbuf` 4,096,
`_usbh_devices` 846, `_cdcd_itf` 776, `gSectorScratch` 512 (the SDK's), and the
USB host stack (`_hcd_data`, `cdch_data`, `_msch_itf`, ...). They come from the
PREBUILT `libarduino_tinyusb.a`, compiled with every class on (CDC x2, MSC,
HID, MIDI, video, DFU, vendor, NCM, and host): its driver table references all
of them, so linking MSC links all their buffers. Neither option can shed them
without a TinyUSB rebuilt for MSC only (Espressif's lib-builder, out of scope).
On the Picco that is internal DRAM, paid at boot whenever the entry point
exists, used or not. For scale: the Picco has booted with 243,656 to 247,972 B
of free heap on device (2026-11-06/07, `docs/picco-port-2026-11-05.md`).

**A vs B′ boot heap: static estimate, not measured.**

- Static image: A's internal heap starts 304 B LOWER than B′'s (the
  `.dram0.heap_start` addresses above): A wins by 304 B.
- Task stacks the core rebuild shrinks, allocated from internal heap at boot:
  `esp_timer` 8,192 (A, stock) vs 4,096 (B′); FreeRTOS timer service 3,120 (A)
  vs 2,560 (B′). A pays 4,656 B more.
- WiFi IRAM: nothing to gain on the S3. The rebuild's `CONFIG_ESP_WIFI_IRAM_OPT=n`
  and `CONFIG_ESP_WIFI_RX_IRAM_OPT=n` are already the S3 defaults (`# ... is
  not set` in the stock `esp32s3/sdkconfig`), so the C3's ~25-30 KB from those
  lines does not apply here. `.iram0.text` is 100,791 (A) vs 100,891 (B′).
- Removed cloud components (`custom_component_remove`, selective compilation):
  not checked; expected to cost nothing unless referenced (A's unrooted image
  is 3,468 B SMALLER in flash than stock B).
- **Net: A has about 4,352 B (~4.3 KB) less free internal heap at boot than
  B′.** To measure it: boot both images and compare the `MEM Free:` line.

**B′'s fragility: what it leans on** (pioarduino 55.03.37, PlatformIO 6.1.19):

1. `ARDUINO_LIB_COMPILE_FLAG` is `"Build"` during the core rebuild
   (`platform-espressif32/builder/frameworks/espidf.py:821`) and `"Inactive"`
   otherwise (`builder/main.py:867`), the value `arduino.py:917-920` branches on.
2. The core rebuild compiles the project's lib_deps with the project's `-D`
   flags, because PlatformIO processes BUILD_FLAGS before any framework script
   (`platformio/builder/tools/piobuild.py:118-122`). This is why the cap breaks
   the rebuild without the middleware.
3. The rebuild copies back only the archives it compiled
   (`espidf.py:2623-2678`, `idf_lib_copy`), so the stock
   `libarduino_tinyusb.a` and `include/arduino_tinyusb/` survive, and
   `pioarduino-build.py:535-536,549` still put them on the include path and the
   link line.
4. PlatformIO's middleware API: `CollectBuildFiles` runs every middleware on
   every file it collects (`piobuild.py:307-326`, `AddBuildMiddleware` 329-330),
   and every firmware C/C++ file is compiled through it (Arduino core,
   lib_deps, `lib/`, `src/`). A file compiled another way would miss the
   TinyUSB values; `FREEINK_CAP_USB_MSC` itself is a plain build flag, so the
   vtable- and layout-changing define cannot diverge.
5. The stock `qio_qspi` variant header stays stock (no env in this fork
   rebuilds `qio_qspi`), and its values match what `libarduino_tinyusb.a` was
   compiled with. They come from the same lib-builder run in the same package;
   a platform upgrade that changes the classes or the buffer sizes changes
   both, and the script reads them fresh on every build.

The script fails the build closed when (5) does not hold or the archive is
missing. It cannot detect (1)-(4) silently changing; those surface as the
rebuild failing again (`'USBMSC' does not name a type`) or as MSC no longer
linking. A rooted build plus the `tud_msc` symbol check catches the second.

**A's cost in maintenance:** none of the above; it is upstream's own shape.
But the Picco loses the core rebuild's other effects too: the picco/sticky
hash separation, and any future custom_sdkconfig tuning (A would need a second
mechanism to apply one to the Picco).

**Ruling question for the owner:** *"USB Drive on the Picco: the prebuilt core
(A, ~4.3 KB less boot heap, upstream's shape) or keep the core rebuild with the
TinyUSB middleware (B′, five pioarduino internals to keep true)?"* Either way,
about 23 KB of static DRAM and 55-63 KB of flash once an entry point exists.

### X4 `default`: unaffected

`pio project config --json-output` of `HEAD`'s and the committed
`platformio.ini` (one comment changed): all 14 sections identical. With the B′
lines applied, only `env:picco` and `env:picco-gh_release` differ
(`build_flags`, `custom_sdkconfig`, `lib_deps`).

`pio run -e default`: SUCCESS, **RAM 58,724 B (+0), Flash 4,854,487 B
(+852)** against the batch-3 gate image at the same base (`a88d91fa1`, worktree
`probe`, read only: RAM 58,724, Flash 4,853,635). `nm -S` diff of the two ELFs:
the i18n tables (`OFFSETS_EN/ES` +16 B each, `STRINGS_EN_DATA` +314 B,
`STRINGS_ES_DATA` +345 B: the 8 new strings, which `gen_i18n.py` keeps on every
board because it finds keys by text, not by preprocessor), plus 2-10 B in five
functions that reference the version string or `__LINE__`
(`HalStorage::HalStorage()`'s assert moved past line 31, beyond RISC-V's
compressed `c.li` range; `OtaUpdater::isUpdateNewer`,
`CrossPointWebServer::handleStatus`, `FontUpdateActivity::writeFailureLog`,
the settings list: the version string differs by branch name). **0 symbols**
matching `UsbDrive|UsbMassStorage|requiresExclusiveStorageLoop|handoffUsbOtg|
tud_|refuseWhileUsbDrive`. Built without the gitignored commercial editor faces,
like the baseline. This X4 build predates the last edits to the tree (the
shared `syncSdFatCaches()`, a `ready()` check in `beginUsbDrive()`, comments):
all of it inside `FREEINK_DEVICE_PICCO` / `FREEINK_CAP_USB_MSC` blocks, so for
the X4 they only shift `__LINE__`; not rebuilt for that.

## Safety (B-073 in mind)

Read from source; none of it is device-tested.

1. **Flush before the host gets the card: FOUND MISSING, FIXED.**
   `detachFilesystemForRawAccess()` ends the volume with `FsVolume::end()`,
   which in SdFat 2.3.1 only nulls the volume pointers
   (`FsLib/FsVolume.h:120-125`); the partition-level `end()` is
   `cacheClear()`, an invalidate, not a write (`FatLib/FatPartition.h:92-95`,
   `ExFatLib/ExFatPartition.h:85-88`). A dirty cached FAT or directory sector
   would be dropped and the host would mount a FAT that disagrees with its
   directory entries: the lost-allocation shape of B-073. Upstream has the same
   gap. `HalStorage::beginUsbDrive()` now syncs through `/` first
   (`syncSdFatCaches()`):
   `FatFile::sync()` (`FatFile.cpp:1269-1307`) and `ExFatFile::sync()`
   (`ExFatFileWrite.cpp:367-385`) both end in the volume's `cacheSync()`, which
   writes the data and FAT caches and syncs the device (`FatPartition.h:205-215`;
   `FsCache::sync()` is a no-op returning true when nothing is dirty,
   `FsCache.cpp:56-75`). A failed sync refuses the handoff with the volume
   still mounted. In practice the cache should already be clean (SdFat's
   metadata operations end in `cacheSync()`), so this is a guarantee, not a
   measured fix.
   **Same flaw on the Picco's deep-sleep path: fixed in its own commit**
   (`fix(picco): flush SdFat's FAT cache before the SD powers down for
   sleep`). `HalStorage::prepareForDeepSleep()` called `SDCard.shutdown()`,
   whose `_vol.end()` drops the cache the same way, behind an SDK comment
   (`SDCardManager.cpp:90-91`) that says it flushes. It now runs the same
   root-directory sync first (`syncSdFatCaches()` in `lib/hal/HalStorage.cpp`,
   shared with `beginUsbDrive()`), and logs a failed sync without blocking the
   sleep. The SDK comment is not edited (separate repo; What remains 5).
2. **No open handles at entry.** `goToUsbDrive()` uses `replaceActivity()`, so
   the current activity and every stacked one run `onExit()` and are destroyed
   (closing their files) before `UsbDriveActivity::onEnter()`
   (`ActivityManager.cpp`, the Replace branch of `processPendingTransitions()`).
   Every long-lived `HalFile`/`FsFile` member in `src/` and `lib/` belongs to an
   activity-owned object (grep of headers: `TextViewerActivity`, the web server
   and WebDAV handler, `Section`, `BookMetadataCache`, `ZipFile`, the parsers,
   `PixelCache`, `FontDeletionList`); SD fonts open a file per call; the render
   task is the only other task. Nothing outside HalStorage calls
   `SDCardManager` (grep: comments only).
3. **Nothing touches the card while the host has it.** From a successful
   detach until the reboot, `HalStorage::usbDriveOwnsCard()` is true and every
   `HalStorage` call (`HAL_STORAGE_WRAPPED_CALL`, `open`, `openFileForRead/
   Write`) and every `HalFile` call (`HAL_FILE_WRAPPED_CALL`, `getName`,
   `openNextFile`) takes the mutex, then refuses with `LOG_ERR("SD", "Refused
   <op> ...")` without reaching SdFat. Path calls would fail harmlessly anyway
   (the detached `FsVolume` has no volume), but a handle opened before the
   detach still points at the live block device; the guard stops it and makes
   the attempt visible in the log. The block device's one DMA bounce buffer
   (`SdmmcBlockDevice.cpp:74`) is used by the TinyUSB task's MSC callbacks
   without HalStorage's mutex: correct only because nothing else may reach the
   device. The guard and the extra API exist only in USB Drive builds.
   **Gap:** `HalFile::Impl`'s destructor still closes through SdFat; a DIRTY
   write handle destroyed during the mode would write its directory entry.
   None can exist (item 2), and the mode leaves by `ESP.restart()`, which runs
   no destructors.
4. **Main-loop writers are off.** The exclusive branch returns before
   `handleSlideSwitch()` (saves settings to the card), the serial
   `CMD:SCREENSHOT`, both sleep paths (the sleep frame is written to the card)
   and the low-battery guard (`src/main.cpp`).
5. **Leaving reboots; one remount-in-place.** Every exit after a host could
   have the card reboots. The only remount is upstream's: MSC or `USB.begin()`
   failing inside `beginUsbDrive()`, before any host can see the card, so the
   card is exactly as the flush left it.
6. **Accepted risk: a cable pulled mid-write.** The owner's ruling was asked
   with "skip" recommended for exactly this. The device writes each MSC block
   synchronously, but the host writes lazily: pulling the cable before ejecting
   can leave the filesystem as the host left it, and nothing on the device
   checks or repairs it. The screen says to eject first.
7. **Light sleep during the activity's own refreshes (UNCONFIRMED effect).**
   The e-ink BUSY slice (`HalPowerManager::onEinkBusyWaitSlice`) light-sleeps
   unless WiFi or `gpio.isUsbSleepGuardActive()`. With a cable in, VBUS keeps
   the guard on. On battery before a cable is plugged, a refresh can
   light-sleep with the OTG controller idle; worst case expected is a plug-in
   noticed one slice late. Not changed.
8. **Not checked:** whether a book replaced in place under the same path from
   the computer is detected by its `.crosspoint` cache (a web upload over an
   existing book has the same exposure). The reboot lands on Home, not the
   reader, so no open book is resumed against changed bytes.

## What remains

In order:

1. **Owner: A or B′** (the question under Build config). Then, in one change:
   the Picco envs' USB lines (and, for A, upstream's `[firmware_tuned]`
   split; for B′, the `custom_sdkconfig` TinyUSB lines plus
   `pre:scripts/picco_tinyusb_config.py` in `[env:picco]` extra_scripts),
   `usb-drive` pushed to `natebunnyfield/freeink-sdk`, and the submodule
   pointer moved to `7ec5a91`.
2. **Owner: the entry point and the screen**, as a rendered visual first
   (visual gate). Candidates, none chosen: a Home row, a row in File
   Transfer, a System row in Settings. Upstream's was a fourth choice on the
   deleted NetworkModeSelection screen, with a `Usb` list icon and the
   `STR_USB_DRIVE_DESC` subtitle ("Manage the SD card from your computer"),
   neither ported. The screen is upstream's text layout under the Lyra Six
   header.
3. **Device test** (below), after 1 and 2.
4. **The simulator's iOS source list**: this change adds two translation units
   (`src/activities/network/UsbDriveActivity.cpp`,
   `src/platform/UsbSerialJtagHandoff.cpp`; both compile empty off the Picco).
   After it is merged, regenerate from the checkout the iOS build uses:
   `pio run -e simulator -t compiledb && python3
   ../crosspoint-simulator/tools/gen_cmake_sources.py --firmware-dir .
   --compile-db compile_commands.json`. Not run from this worktree: the
   generator writes the shared simulator repo's list, which would then name
   files the other checkouts do not have.
5. **The SDK comment** in `SDCardManager::shutdown()`
   (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:90-91`)
   still says `FsVolume::end()` flushes. The firmware now flushes before it
   calls `shutdown()` (`HalStorage::prepareForDeepSleep()`), but the comment
   should be corrected in the SDK repo.
6. **CI**: `.github/workflows/ci.yml`'s `build-picco` job will exercise
   whichever config lands; with B′ it also runs the middleware on a fresh
   runner (the stock `qio_qspi` header is present after install). Not run
   here.

Not done and not asked: an MSC-only TinyUSB build to win back most of the
23 KB (lib-builder work); a device-side filesystem check after a cable pull.

## Device test (everything here UNCONFIRMED until run)

Needs the build ruling (A or B′, above) applied and an entry point that calls
`activityManager.goToUsbDrive()`. Until the owner picks one, a throwaway local
hook is enough (for example a `CMD:USBDRIVE` case in main.cpp's serial command
handler); do not commit it. Flash app-only as usual
(`pio run -e picco -t upload_plan` must print `app_only_upload: OK`; the write
is `esptool --chip esp32s3 -p <port> write-flash --flash-mode keep --flash-freq
keep --flash-size keep 0xE0000 firmware.bin`, `docs/picco-port-2026-11-05.md`).

Before: note the card's volume name and free space on the Mac; keep a test
file with a known sha256 (a 50-200 MB epub or zip is good for timing).

1. **On battery, no cable.** Enter USB Drive. Expect "Preparing USB Drive..."
   then "Connect this reader to your computer" with a Back hint. The last
   serial lines before the PHY switches are the `[USB]` ones from
   `beginUsbDrive()`; after that the Serial/JTAG port disappears (expected:
   the PHY now belongs to USB-OTG).
2. **Back (swipe up or the left-edge swipe) or Power while waiting.** Expect
   the Loading popup and a reboot to Home; on a Mac with a cable in, the
   `USB JTAG/serial debug unit` (`/dev/cu.usbmodem*`) reappears.
3. **Enter again, then plug the cable into the Mac.** Within ~30 s: the screen
   shows "USB Drive Connected"; Finder mounts the card's volume;
   `system_profiler SPUSBHostDataType` lists product `CrossPoint_Picco`,
   manufacturer `CrossPoint` (VID `0x303a`, PID `0x0002` are the Arduino
   defaults), with a mass-storage interface. Record how long the mount took.
4. **Copy the test file to the card, then Eject in Finder.** Expect the Loading
   popup and a reboot to Home, and the Serial/JTAG port back. Time the copy
   (MB/s). On Home, the book should be listed and open.
5. **Integrity.** Enter USB Drive again, copy the file back, compare sha256;
   eject. Then `diskutil verifyVolume <disk>` (read-only) before ejecting the
   last time, or `fsck_msdos -n` on the unmounted device node.
6. **Cable pulled with no transfer running (no eject).** Expect a reboot to
   Home (Disconnected state). Run the read-only check again.
7. **Timeouts.** Enter and do nothing: reboot to Home after 5 min. A
   charge-only cable must behave like no cable (it waits, then times out).
8. **While mounted, press Power and Back.** Expect nothing to happen (input
   cannot exit while a host has the card).
9. **After each reboot:** `[OTA] ... already committed`, Home, and the boot
   heap line (`MEM Free:`); compare it with a stock Picco boot (B′/A static
   cost: see the table above). A `Refused <op>` log line would mean something
   tried to use the card during the mode, but during the mode the Serial/JTAG
   port is gone, so such lines are not seen on the Mac; not checked whether
   they reach the fork's RTC crash-log ring.

Do NOT test a cable pull mid-copy on a card whose contents matter: that is the
accepted-risk case (Safety 6), and the host is the one that leaves it dirty.
