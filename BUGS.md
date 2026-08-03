# Known bugs and open defects

Running list for this fork. Newest first within each section. A bug leaves
OPEN only when there is evidence it is fixed — a passing build is not evidence
for anything you cannot observe headlessly (see the device-feel rule in the
project guide).

Format: `**[id] Title** — severity · where · status`, then what breaks, how it
was found, and what closing it requires.

---

## OPEN


### [B-006] X4 running firmware carries an empty version stamp
**severity: low · scope: device provisioning · found 2026-08-02**

The X4 runs a build stamped `1.5.0-BNY-rc+` — empty suffix. `gh_release_rc`
composes its version as `1.5.0-BNY-rc+${sysenv.CROSSPOINT_RC_HASH}`
(`platformio.ini:186`), and the flash was run without that variable set. The
code is identical to `crosspoint-880ba0f9.bin`; only the stamp is wrong. It
feeds the OTA version comparison, and it makes the running build
unidentifiable after the fact.

**Close by:** reflashing with the variable set, or SD Firmware Update from the
card (`SdFirmwareUpdateActivity` is a plain file picker with no version gate,
so a same-code reflash is accepted):
```bash
CROSSPOINT_RC_HASH=880ba0f9 pio run -e gh_release_rc -t upload --upload-port /dev/cu.usbmodem2401
```

### [B-005] The two SD cards hold different bytes under the same bin filename
**severity: low · scope: device provisioning · found 2026-08-02**

`crosspoint-880ba0f9.bin` is md5 `262f1d51…` on OWEN_BNF (X4) and `930747eb…`
on REDACTED-SSID (X3). Same size, same `1.5.0-BNY-rc+880ba0f9` version stamp;
they differ only in embedded `__TIME__`/`__DATE__` strings, because the build
was relinked between the two copies (root cause is B-004). Identical filenames
with different content defeats later verification.

**Close by:** mounting OWEN_BNF and re-copying from
`.pio/build/gh_release_rc/firmware.bin` so both cards match. Requires the X4
card mounted.

### [B-004] Toggling CROSSPOINT_RC_HASH silently wipes every build directory
**severity: medium · scope: build tooling · found 2026-08-03**

`[env:gh_release_rc]` interpolates `${sysenv.CROSSPOINT_RC_HASH}` into
`build_flags`, so setting or unsetting it changes the resolved config, which
changes `.pio/build/project.checksum`, which makes the next `pio run` clean
**all** env build dirs — not just the target env.

Observed: a stamped `pio run -e gh_release_rc` deleted
`.pio/build/simulator/program`, and a later headless simulator run died with
`no such file or directory`, exit 127. It also caused B-005.

**Close by:** either documenting it in the project guide next to the existing
version-override section, or removing the sysenv interpolation in favour of a
mechanism that does not perturb the checksum. Currently recorded only in
agent memory, not in the repo. Workaround: hold the variable constant across
every `pio run` in a session, including simulator builds.

### [B-003] Exploded `.epub` directories are probably unreadable on device
**severity: low · scope: content · found 2026-08-03**

The X3 card carried 10 entries named `*.epub` that are DIRECTORIES
(`META-INF/`, `mimetype`, `OEBPS/`) rather than zip containers. Miniz is the
firmware's only container library and `lib/Epub/Epub/Section.cpp` unzips at
runtime, so these almost certainly do not open — but this was inferred from
the container path, **not** confirmed by opening one on device or in the
simulator.

They are preserved at `~/crosspoint-books/_exploded/`. Also note
`ls *.epub | wc -l` on such a card reports a wildly inflated count because it
recurses into the directories (reported 510 for a real 76).

**Close by:** opening one in the simulator to confirm the failure mode, then
either re-zipping them as proper EPUBs (mimetype stored first, uncompressed)
or discarding them.

### [B-002] Two upstream commits unmerged, and unmergeable as-is
**severity: low · scope: fork sync · by design, tracked not fixed**

`9c48609f` (bookmarks survive re-pagination) and `0f747b82` (content-based
EPUB sync positions) remain unmerged. `git merge upstream/develop` produces 18
conflicts, six `modify/delete`, because both commits straddle live Epub engine
code and subsystems this fork deleted on purpose.

Not a defect so much as a standing cost. See [docs/fork-sync.md](docs/fork-sync.md).

**Close by:** cherry-picking the live hunks only — the `Section`, `ParsedText`,
`ChapterHtmlSlimParser`, `EpubReaderUtils.h` changes — and bumping the cache
format version if layout output changes.

---

## FIXED

### [B-008] iOS app offers WiFi and web-server menus that cannot work
**severity: medium · scope: iOS app · FIXED + VERIFIED 2026-08-03**

Fixed by `CROSSPOINT_NO_NETWORK` guards (firmware `5bce63bf`) plus iOS TU
exclusions (simulator `ac8cdef`).

**Verified by driving the iOS Simulator, not by a clean compile.** Fresh
install on crosspoint-x3-air, iOS 26.5:
- Home menu shows exactly Browse Files / Recent Books / Settings. **File
  Transfer is gone**, nothing dangles.
- Settings > System shows Time to Sleep, Quick Resume, the three Sleep Screen
  rows, Keep Screen Awake, the three Clock rows, Clear Reading Cache,
  Language, Device owner. **Wi-Fi Networks and SD Card Firmware Update are
  gone**, nothing dangles.
- App launches, a book opens, pages turn, images render.
- Font picker lists exactly the four S-tier families with live previews.

Also verified inert on DEVICE firmware: `CROSSPOINT_NO_NETWORK` is undefined
in platformio.ini and gh_release_rc builds identically at 3,658,031 bytes
flash before and after the guards.

Original report below.

The iOS build compiles and ships the whole firmware network stack, and exposes
it in the UI, but none of it can function on a phone. `WiFi.scanNetworks()`
returns a synthetic list (`crosspoint-simulator/src/WiFi.h:244`), and
`CrossPointWebServerActivity` shows the user `WiFi.localIP()`, which is
hardcoded to **127.0.0.1** (`WiFi.h:196`) — so the app renders a URL and QR
code pointing at loopback that nothing can reach. OTA is stubbed to always
report NO_UPDATE and to fail install with `INTERNAL_UPDATE_ERROR`
(`simulator_ota.cpp:19`). SD Firmware Update offers to flash a `.bin` from an
SD card the device does not have.

This is the lying-control class of defect: the control exists, is reachable,
and silently does nothing useful. `Info.plist.in` also carries no
`NSLocalNetworkUsageDescription`.

**Close by:** hiding these entries on the iOS target (menu surgery in
`SettingsActivity` / `NetworkModeSelectionActivity`), ideally alongside
compiling the ~16 dead TUs out. Note this is capability *removal* from a
surface where the capability never worked — flag it as such when doing it.

### [B-007] iOS seed fonts are stored twice on device
**severity: low · scope: iOS app · FIXED + VERIFIED 2026-08-03**

Fixed by symlinking rather than copying (simulator `ac8cdef`). Verified from
the app's own launch log on a fresh install: `[harness] symlinked
fonts/TeXGyreSchola -> bundle SeedFonts` and the same for Rosarivo,
Coelacanth and Edgar. The font picker then listed all four and text rendered,
so the symlinks resolve for reading. Saves ~54.8 MB of duplicated storage.

Original report below.

`seedOneFontDirectory` hard-copies every bundled `.cpfont` into
`Documents/fonts/`, including the `2x/` subdirectory
(`crosspoint-simulator/ios/CrossPointFsPrep.cpp:193,245`). The 54.8 MB seed
set therefore exists in both the app bundle and Documents, so a 19.8 MB
download presents as roughly **113 MB** in iOS Storage settings — the number
users actually see.

**Close by:** symlinking rather than copying, provided the installer and prune
paths never write through the link. Would halve the visible footprint to
~58 MB with no capability change.

### [B-001] Quick Resume pin made the sleep-screen setting a lying control
**severity: high · fixed 2026-08-03 · `6bb7efc8`, `780982ed`**

`normalizeRetiredSettings()` pinned `quickResumeSleepScreen` to ON on every
load (`CrossPointSettings.cpp:136`), and the whole sleep group lived in the
Display settings category, which the device UI drops
(`SettingsActivity.cpp:48`). Net effect: an owner could set a custom sleep
image and never see it, because while Quick Resume is ON `SleepActivity::onEnter`
returns before it ever reads `sleepScreen` — and the inactivity timeout is the
common way a reader sleeps. The only control was the web UI, and a reload
reverted even that.

Found by tracing the sleep path from the report "sleep.bmp never shows"
rather than trusting the settings file, which already read `0`.

Fixed by moving the whole sleep group (Sleep Screen, both cover options, Quick
Resume on Timeout) to System and dropping the pin. Verified on the simulator:
the row loads OFF from disk where it would previously have read ON, and a 60s
idle produces `Auto-sleep triggered` then `Loading: /sleep.bmp`.

### [B-000] install-sim-fonts.py silently reinstalled all 15 font families
**severity: medium · fixed 2026-08-03 · `4c0571aa`**

The installer defaulted to "every curated family `sd-fonts.yaml` can build".
Safe while several families lacked sources; once all 15 became buildable
(2026-08-01) that default became "install all 15", so a routine re-run of the
documented command broke the four-family S-tier parity with both SD cards and
the iOS seed bundle. The ruling was written in three places and enforced in
none.

Fixed with `installed_families:` in `sd-fonts.yaml` as the single source of
truth; `--all-curated` opts back in and warns. See
[docs/sd-card-fonts.md](docs/sd-card-fonts.md).
