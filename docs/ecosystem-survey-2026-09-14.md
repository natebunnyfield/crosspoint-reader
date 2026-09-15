# Ecosystem survey: what else runs on this hardware, and what is worth taking

Surveyed **2026-09-14**. Scope: projects that are **NOT CrossPoint forks** (or are
far enough diverged to count as independent), running on the Xteink X3/X4 or on
the same ESP32-C3 + e-ink combination. Fork-to-fork borrowing is a different
question and already has a home: [fork-ecosystem.md](fork-ecosystem.md)
(surveyed 2026-08-15) and [fork-sync.md](fork-sync.md).

Owner's priority order, and the order of the candidate table below:

1. Bluetooth keyboard interaction
2. WiFi handling
3. The display driver

**Reading rule for this file.** Every claim about *our* code carries a
`file:line`. Every claim about *someone else's* code is labelled **VERIFIED**
(I read the source or the API returned it) or **CLAIMED** (the project says so
and I did not check) or **UNVERIFIED**. Marketing numbers are marked as such and
are not treated as measurements.

---

## 1. What was surveyed

| Thing | At | Date |
|---|---|---|
| This fork (`natebunnyfield/crosspoint-reader`, branch `main`) | `1c1207cd58e8083f5554eae262abdaa61ededb28` | 2026-09-14 17:55 -0500 |
| Its `freeink-sdk` submodule (`natebunnyfield/freeink-sdk`, `main`) | `352098e79ef7eda181ecfc5c0ef9685acf0e2ebb` | 2026-08-15 18:04 -0500 |
| Upstream SDK (`Free-Ink/freeink-sdk`, `main`) | `2cca22fe44862215e029a416d5ff6fddcb3e593e` | 2026-09-14 14:51 -0400 |
| `ngxson/pluspoint-reader` (local checkout) | `60e9739` | 2026-06-08 |
| Web sources | see each entry | all accessed 2026-09-14 |

**Our license is MIT** (`LICENSE:1`, "Copyright (c) 2025 Dave Allie"). That is
the constraint every candidate below is judged against: MIT / BSD / Apache-2.0
source can be lifted with attribution; **GPL/LGPL cannot**; a repo with **no
LICENSE file is all-rights-reserved and cannot be lifted at all**, however
public it looks.

---

## 2. The SnailOS question, answered

**Short answer: SnailOS is real, it targets exactly this hardware, it is a
closed-source commercial competitor, and there is nothing to cherry-pick from it
because there is no source to cherry-pick.**

### What it is — VERIFIED from the vendor's own site

**Snail OS**, <https://snailos.org> (accessed 2026-09-14). Version 2.2.1 at time
of access.

- **Who**: Monday Draft, LLC; founder/author **Joey Smith** (Joseph D. Smith,
  <https://joeydsmith.com>, GitHub `joey-io`). Contact `joey@snailos.org`.
  Footer states *"Not affiliated with XTEINK."*
- **How new**: `whois snailos.org` gives `Creation Date: 2026-08-05T22:08:39Z`.
  Six weeks old.
- **Hardware**: its `/xteink` page states the target as
  *"an ESP32-C3, an 800 × 480 e-ink panel, Wi-Fi, Bluetooth, microSD and
  physical buttons"*, and its `llms.txt` gets the geometry right —
  *"The X3 panel is 528 × 792 pixels; the X4 is 480 × 800."* X3 primary, X4
  supported, X4 Pro "coming soon".
- **Business model**: $139 for a pre-flashed X3 (limited to 1,000 devices), $9
  for a signed firmware installer for a device you own, $4.99/mo "Snail Connect"
  subscription for WiFi sync / iMessage relay through an always-on Mac.
- **It names us.** `/xteink` carries a three-column comparison table —
  "Snail OS / OEM firmware / **CrossPoint**" — citing the CrossPoint GitHub repo
  as a source. This is a direct competitor in the same niche.

### Is there source? — NO, VERIFIED

From <https://snailos.org/terms>, verbatim:

> **"The source is not published. Buying a copy does not grant a right to
> redistribute, resell or republish it."**

There is no GitHub org `snailos` (`api.github.com/orgs/snailos` → 404).
`joey-io` has two public repos, neither firmware. `/license`, `/eula` and
`/legal` all 404. **License: proprietary, per-person key.** Nothing can be
lifted.

The only open layer is the **app** layer: card apps are plain Lua 5.4 files with
sources readable at <https://snailos.org/apps>, and the developer API is
published at `/dev`, `/dev/reference`, `/dev/limits` and `/dev/SKILL.md`. That is
documentation and example scripts, not firmware source.

There are several unrelated things called "SnailOS" — a browser WASM shell
(`attilaolah/snailos`, MIT, 0 stars), an itch.io DOS-styled toy
(`AscentrixLLC/SnailOS`, no license), a .NET user account, and a Brazilian
sandal brand. **None of them is this.** GitHub repo search for `snailos`
returned 5 repos, none the firmware; `snail+eink`, `snail+e-ink+firmware` and
`snail+esp32+epaper` returned zero.

A Chinese-language search (`蜗牛系统 墨水屏 阅读器 固件 ESP32`) found nothing —
Snail OS is US-based and English-language; the snail is a calm-computing
metaphor, not a translation.

### "How did they achieve what they did" — the claims, and what they are worth

Everything below is **CLAIMED** by the vendor on its own site. None of it is
independently verified, and I found **no third-party coverage of Snail OS
anywhere** — not Hacker News, Reddit, MobileRead, Hackaday, the-ebook-reader,
Liliputing, or the most complete third-party map of this ecosystem
(<https://productimpossible.com/articles/xteink-firmware-ecosystem/>, which
catalogs ~25 forks and 7 independents and **does not list Snail OS at all**,
presumably because that survey is GitHub-based).

**Display — the headline, and the one thing genuinely worth thinking about.**
The comparison table claims:

| | Snail OS | OEM | CrossPoint (their words) |
|---|---|---|---|
| Driver | *"Snap Display, our own driver, with a factory-waveform fallback"* | Vendor waveform | *"Vendor waveform via GxEPD2"* |
| Partial refresh | *"123 ms"* (Snap) / *"499 ms"* (factory) | ~499 ms | *"Not timed by us"* |

and `/xteink` says *"Joey Smith reverse engineered the boards and wrote the
display driver from the panel datasheet."*

Two things to note before anyone gets excited.

1. **Their description of CrossPoint's driver is factually wrong.** We do not
   use GxEPD2 and never have. `grep -ri gxepd platformio.ini dependencies.lock
   src lib` returns **zero** hits in firmware code; the only two matches in the
   whole tree are comments in an unrelated UC8179 driver
   (`freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8179Driver.cpp:144` and
   `.../Uc8179Driver.h:52`). Our waveforms are hand-tuned per-controller LUT banks —
   `freeink-sdk/libs/display/FreeInkDisplay/src/lut/Uc8253X3Luts.h` is 162 lines
   of banks — `_normal`, `_half`, `_fast`, `_full`, `_gc` named in its header
   (`:5-10`) plus `_aa_pre_bw_mid` at `:133` — with provenance stated in that
   header and attributed in `freeink-sdk/NOTICE`. A table that gets a competitor's architecture wrong
   is not a table to trust on its own numbers.
2. **The comparison is apples to oranges, and that is the real lesson.** Their
   123 ms is a *partial* (windowed) refresh. Our measured 570 ms
   (`docs/ble-editor-spike.md:42`, five samples, 570–573 ms) is a *whole-panel*
   FAST refresh, because **on the X3 we have no windowed path at all** — see
   candidate D-2 below. Their 499 ms "factory" figure and our 570 ms FAST are
   the same order; the 4x is the window, not the waveform.

Also, "Fast Display Beta is off by default" (`/install`), and elsewhere the same
site says a refresh takes *"about half a second"* (`/dev/limits`) — so the fast
path appears to be beta, not shipped.

**Greyscale: none.** The panel is treated as 1-bit throughout; their card-drawing
API says so explicitly. We ship 4-level grayscale planes with a dedicated `_gc`
LUT bank and a tiled streaming path (`lib/hal/HalDisplay.h:126-130`). **We are
ahead here.**

**De-ghosting**: two mechanisms claimed — a manual *"Hold BACK for a second to
redraw a ghosted screen"*, and *"The engine defers a de-ghosting full refresh
until the keys go quiet."* We have both shapes already: a user-configurable
pages-between-full-refresh setting (`src/CrossPointSettings.h:136-143`,
1/5/10/15/30), a FORCE_REFRESH power-button action
(`src/CrossPointSettings.h:147`), and per-activity ghost-cleanup paths
(`src/activities/reader/XtcReaderActivity.cpp:257`,
`src/activities/reader/EpubReaderActivity.cpp:1099`).

**Bluetooth: NOT a keyboard host.** This is the important negative for priority
area 1. Snail OS's BLE is *phone-notification sync* via an Android companion app
— *"Receive phone notifications, reply to messages and dismiss them"*. **No HID,
no keyboard, no page-turner.** If the owner remembered "SnailOS did something
clever with Bluetooth keyboards", that is not this project. **We are ahead
here** — see §4.

**WiFi**: scan/join/remember, per-device-encrypted password, plus one genuinely
interesting engineering note (`/dev/limits`): *"The chip has about 64 KB of
usable heap, and a TLS handshake wants 18 KB of it contiguous… The extractor
streams a response instead of holding it… Each choice exists to keep an 18 KB
contiguous block findable at the moment a handshake wants it."* That contiguity
discipline is exactly the lesson our own BLE path already learned the hard way
(`src/notes/BleHidHost.cpp:643-658` — gating on
`heap_caps_get_largest_free_block`, not free heap). And we already stream HTTP
bodies in chunks rather than buffering them
(`src/network/HttpDownloader.cpp:153-156`). **Nothing new to take.**

**Battery**: *"rated for up to two weeks"*, unmeasured.

**Architecturally novel, and the only thing here I would actually study**: a
Lua 5.4 app layer with an on-device store (`snailos.org/apps/index.json`), one
`.lua` file per app in `/apps`, no reflash. Their `/dev` page reasons it out —
the C3 executes from flash through the MMU instruction cache and SD sits behind
SPI outside the address space, so machine code can never run from the card, but
a Lua *source* file can. They record a measured decision (9 Aug 2026) replacing
two hand-rolled bounded interpreters with Lua: `+114 KB flash`, peak RAM 23–31 KB
vs 43,472 B, largest single allocation 1.5–2 KB vs 10,240 B —
*"On this device contiguity fails before capacity."* That is a real finding and
it agrees with ours. It is also **out of scope** for this fork, which is a
dedicated reader.

**What I could not establish**: whether Snail OS derives from CrossPoint code in
any part. CrossPoint is MIT (`api.github.com/repos/crosspoint-reader/crosspoint-reader`
→ `"spdx_id":"MIT"`), so a closed derivative would be legal with attribution.
Their "reverse engineered from the datasheet" claim implies independent work.
**Lineage is not established in either direction and I am not asserting one.**

Also unestablished: the 123 ms figure, the two-week battery figure, and whether
the published-images table is real — at my access `/verify` said *"0 images on
this server"* while claiming 2.2.1 was current.

---

## 3. Candidates, ranked by value per effort

Every row states the license explicitly. **A blank or "none" license column is a
hard stop, not a caveat.**

### The two best candidates are in a repo we already depend on

Before anything external: **`Free-Ink/freeink-sdk` is not a CrossPoint fork.**
It is a separate hardware-abstraction SDK for Xteink and other e-ink boards,
authored largely by Justin Mitchell, **MIT** (`freeink-sdk/LICENSE:1`,
"Copyright (c) 2026 FreeInk"). We consume it as a submodule via
`platformio.ini:245-257`. It qualifies for this survey in its own right, and it
holds the two highest value-per-effort items found anywhere in it.

---

### B-0 — `esp_hid_parse_report_map()` is ALREADY LINKED INTO OUR BUILD

**The single best find in this survey, and it costs one `#include`.**

| | |
|---|---|
| **Project** | ESP-IDF `components/esp_hid`, as shipped precompiled in `framework-arduinoespressif32-libs` |
| **URL** | <https://github.com/espressif/esp-idf/tree/master/components/esp_hid> |
| **License** | **Apache-2.0** — verified first-hand in the header installed on this machine: `.../esp32c3/include/esp_hid/include/esp_hid_common.h:4`, `SPDX-License-Identifier: Apache-2.0`, Espressif 2017-2025. Compatible with MIT; keep the notice. |
| **Status** | **VERIFIED on this machine** against the exact package our `platformio.ini:75` platform pin installs |
| **Port effort** | **Near zero.** No vendoring, no new `lib_dep`, no `platformio.ini` change — which is the constraint that produced our hand-rolled client in the first place (`src/notes/BleHidHost.h:3-7`). |
| **Touches** | `src/notes/BleHidHost.cpp` only |

Both the include path and the library are **already on our command line**:

```
~/.platformio/packages/framework-arduinoespressif32-libs/esp32c3/pioarduino-build.py:425
    join(FRAMEWORK_SDK_DIR, "esp32c3", "include", "esp_hid", "include")
~/.platformio/packages/framework-arduinoespressif32-libs/esp32c3/pioarduino-build.py:515
    ... "-lesp_hid" ...
```

and the symbols are **defined**, not stubbed. Reproduce:

```bash
NM=~/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-nm
P=~/.platformio/packages/framework-arduinoespressif32-libs/esp32c3/lib/libesp_hid.a
$NM --defined-only -A $P | awk -F: '{print $2}' | sort | uniq -c
#   23 esp_hid_common.c.o      <-- the parser
#   11 esp_hidd.c.o
#   56 esp_hidh.c.o
```

`esp_hid_common.c.o` defines `esp_hid_parse_report_map`, `esp_hid_free_report_map`,
`esp_hid_usage_from_appearance`, `esp_hid_usage_str` and friends, and its only
undefined references are `malloc`/`free`/`calloc`/`memcpy`/`memset`/`esp_log`
— **it is self-contained.** The API
(`.../esp32c3/include/esp_hid/include/esp_hid_common.h:209`) is:

```c
esp_hid_report_map_t *esp_hid_parse_report_map(const uint8_t *hid_rm, size_t hid_rm_len);
void                  esp_hid_free_report_map(esp_hid_report_map_t *map);
```

and each `esp_hid_report_item_t` (`:151-157`) carries `report_id`,
`report_type`, `usage` and `value_len`.

**Why that is exactly what we are missing.** `src/notes/BleHidHost.cpp` never
reads characteristic 0x2A4B at all (`grep -c 0x2A4B` = 0). It subscribes to
*every* notifiable 0x2A4D indiscriminately (`:331-337`) and then guesses the
layout by length — *"a 9-byte report carries a leading report ID, so decode from
the last 8 bytes"* (`:423-434`). That guess is why we are confined to
boot-protocol keyboards. With the parser we would know, per report handle, what
the report ID is, how long it is, and whether it is a **keyboard** or a
**Consumer Control** report — which is the whole of what stands between us and
BLE page-turner remotes.

**Read this next entry before acting**, because the rest of `esp_hid` is a trap.

### B-0b — ...but ESP-IDF's HID *host* itself will NOT link. VERIFIED NEGATIVE.

Both web passes recommended adopting ESP-IDF's `esp_hidh` HID host wholesale.
**It cannot be done from the stock precompiled libraries**, and I nearly shipped
that recommendation before checking.

```bash
ar t $P                              # nimble_hidh.c.o IS in the archive
$NM --defined-only -A $P | grep -c nimble_hidh
# 0                                  # ...and defines ZERO symbols
$NM --undefined-only -A $P | grep -o 'esp_ble_hidh_[a-z_]*' | sort -u
# esp_ble_hidh_deinit
# esp_ble_hidh_dev_open
# esp_ble_hidh_init
```

`nimble_hidh.c.o` compiled to nothing — the transport is `#if`-ed out in the
sdkconfig Espressif built these libs with — and no other object in the archive
defines the three `esp_ble_hidh_*` symbols `esp_hidh.c.o` needs. So calling
`esp_hidh_init()` is an undefined-reference link error, not a working host.
Turning it on means rebuilding ESP-IDF, which this project has already
established it cannot do (`freeink-sdk/libs/network/SecureNet/include/SecureClient.h:11-13`,
*"a custom_sdkconfig rebuild fails on managed-component dependencies"* — the same
wall that forced wolfSSL).

**This retroactively justifies the hand-rolled NimBLE client.** Keep our GATT
client; take only the parser from B-0.

---

### B-1 — `freeink-sdk`'s own `BleKeyboardHost`, which we do not use

| | |
|---|---|
| **Project** | `Free-Ink/freeink-sdk`, `libs/network/BleKeyboardHost/` |
| **URL** | <https://github.com/Free-Ink/freeink-sdk> |
| **License** | **MIT** — `freeink-sdk/LICENSE:1`. No barrier. |
| **Status** | VERIFIED by reading the source in our own checkout |
| **Port effort** | Medium. It is 1,073 lines and depends on **NimBLE-Arduino** as a `lib_dep`; ours deliberately uses the raw ESP-IDF NimBLE C API to avoid adding a library (`src/notes/BleHidHost.h:3-7` — *"any platformio.ini change wipes every env's build dir"*). So this is **port the behaviours, not the file**. |
| **Touches** | `src/notes/BleHidHost.cpp`, `src/notes/HidKeymap.h`, `src/activities/util/NoteEditorActivity.cpp`, `src/activities/settings/SettingsActivity.cpp` |

We wrote our own BLE HID host during the 2026-08-05 spike
(`docs/ble-editor-spike.md`) and never looked at the one sitting in the
submodule. It is ahead of ours in **six** measurable ways, every one of which is
a gap in `src/notes/BleHidHost.cpp`:

| Capability | Theirs | Ours |
|---|---|---|
| **HID Report Map parsing** (0x2A4B) | `BleKeyboardHost.cpp:86` `parseReportMapHints()` — detects usage page 0x07 (keyboard) vs 0x0C (consumer) and picks the code byte accordingly | **absent** — `grep -c 0x2A4B src/notes/BleHidHost.cpp` = 0 |
| **BLE page-turner remotes** | `BleKeyboardHost.cpp:757-812` — a generic fallback for Consumer-Control-page remotes and even gamepad-style axis-coded ones, with press/release edge detection | **absent**. Only boot-protocol keyboards work (`src/notes/HidKeymap.h:78-110`) |
| **Connection parameters** | `BleKeyboardHost.cpp:418` — `setConnectionParams(12, 24, latency 0, timeout 800)` = 15–30 ms interval, **8 s supervision timeout**, with peripheral-initiated updates rejected (`:323`). The comment at `:408-417` explains why: a long chapter render blocks the single RISC-V core past NimBLE's 2.56 s default and the controller drops the link | **absent** — `grep -c update_params src/notes/BleHidHost.cpp` = 0. We take NimBLE's defaults, including the 2.56 s supervision timeout. **This is a latency AND a reliability gap.** |
| **Directed reconnect to a bond** | `BleKeyboardHost.cpp:516-523` — round-robins the stored bond addresses with a 4 s backoff (`:48`) | **absent**. On every disconnect we restart a full 30 s active scan (`src/notes/BleHidHost.cpp:576-581` → `:607-621`) |
| **Key auto-repeat** | tracked in `poll()` from `heldUsage_`/`heldSince_` (`BleKeyboardHost.h:173-179`) | **absent by design** (`src/notes/HidKeymap.h:78-80`) — holding Backspace or an arrow deletes/moves exactly one |
| **Multiple bonds + a device picker** | `kMaxBonds = 4`, a `DiscoveredDevice` list with RSSI and a passkey channel (`BleKeyboardHost.h:76-127`) | one implicit bond, and a **hardcoded name match for the author's test keyboard** — `src/notes/BleHidHost.cpp:451`, `strstr(name, "eonix")` |

**Recommendation.** Take the four cheap ones first — connection parameters,
directed reconnect, auto-repeat, and deleting the `"eonix"` name hack — into our
existing raw-NimBLE file. The connection-parameter change is the single highest
value line in this document: `ble_gap_update_params()` with an 8 s supervision
timeout addresses a failure mode (`HCI 0x08` link drop during a long render)
that our implementation is fully exposed to and has no defence against. Report
Map parsing and the consumer-page fallback are a larger second step, and they
unlock a user-visible feature we do not have at all (below).

**Adjacent product note, not a code finding**: BLE input is wired only into the
notes editor, Claude chat and Settings — `grep -l "blekbd::" src/activities/*/*.cpp`
returns exactly `settings/SettingsActivity.cpp`, `util/ClaudeChatActivity.cpp`,
`util/NoteEditorActivity.cpp`. **No reader activity consumes it**, so a BLE
page-turner remote cannot turn a page even if it connected. The readers take
page turns only from `ReaderUtils::detectPageTurn(mappedInput)`
(`src/activities/reader/XtcReaderActivity.cpp:100`,
`src/activities/reader/TxtReaderActivity.cpp:98`). Wiring B-1's consumer-page
support into that is the feature, not the plumbing.

---

### D-1 — 44 upstream `freeink-sdk` display commits we do not have

| | |
|---|---|
| **Project** | `Free-Ink/freeink-sdk`, `libs/display/` |
| **URL** | <https://github.com/Free-Ink/freeink-sdk> |
| **License** | **MIT**. No barrier. |
| **Status** | VERIFIED — `git log HEAD..upstream/main` in the submodule |
| **Port effort** | Low per commit, high in aggregate. It is a submodule bump plus conflict work, not a port. |
| **Touches** | `freeink-sdk/` submodule pointer, `lib/hal/HalDisplay.cpp`, and anything that depends on `EInkDisplay`'s surface |

Our submodule sits at `352098e7` (2026-08-15). Upstream `main` is at `2cca22fe`
(2026-09-14). We are **10 ahead / 151 behind**; our 10 are all local
`KeyboardPanel` icon work. By area, behind by: **display 44**, hardware 51,
ui 42, book 4, network 1.

The display 44 are directly on the owner's priority-3 list. Selected subjects,
verbatim from `git log`:

- `Improve e-ink display initialization and ghosting`
- `Fix ghosting by forcing power-off after AA refresh`
- `Add anti-aliasing LUT support for UC8279 variants`
- `Add Direct grayscale mode for e-paper display`
- `Split dark gray LUT for absolute grayscale images`
- `fix: display SSD absolute grayscale in one pass`
- `Fix e-ink display buffer tracking and optimize SPI writes`
- `fix(x3): skip POWER_ON on full sync when the UC8253 panel is already powered`
- `fix(uc8279): exchange WW/WB registers when loading the X3 AA tables`
- `Add PaperMono window refresh support`
- `feat: expose grayscale base refresh support`
- `Fix UC8279 grayscale plane inversion bug`

Two of those name **ghosting** outright and one is an X3 UC8253 power-sequence
fix — our exact panel. This is the cheapest real display work available: no
licence question, no porting, no new dependency, and `fork-sync.md`'s per-commit
discipline already covers how to take it.

**Caveat, stated plainly**: I read commit *subjects*, not diffs. That is good
evidence a problem was found and weak evidence about the fix. 151 commits is
also not a small bump, and the `hardware` and `ui` halves will move surface we
depend on.

---

### D-2 — windowed (partial) refresh, which exists for X4 and not for X3

| | |
|---|---|
| **Project** | `Free-Ink/freeink-sdk` (already ours) |
| **License** | **MIT** |
| **Status** | VERIFIED by reading the source |
| **Port effort** | Small to expose on X4. **Large and uncertain on X3** — it needs a UC8253 partial-window LUT and register sequence that does not exist in the tree. |
| **Touches** | `lib/hal/HalDisplay.h/.cpp`, `freeink-sdk/.../Uc8253X3Driver.cpp`, the note editor |

`FreeInkDisplay::displayWindow(x, y, w, h)` exists
(`freeink-sdk/libs/display/FreeInkDisplay/include/FreeInkDisplay.h:221-222`,
marked EXPERIMENTAL) and has a real implementation **only for SSD1677**
(`freeink-sdk/libs/display/FreeInkDisplay/src/driver/Ssd1677Driver.cpp:506`).
`PanelDriver`'s default falls back to a whole-panel Fast refresh
(`freeink-sdk/libs/display/FreeInkDisplay/src/driver/PanelDriver.h:53-56`), and
`Uc8253X3Driver` does **not** override it — `grep -c displayWindow` on both the
X3 driver's `.h` and `.cpp` returns 0.

And we do not expose it at all: `grep -c displayWindow lib/hal/HalDisplay.h
lib/hal/HalDisplay.cpp` returns **0 and 0**. Our `RefreshMode` is three values
(`lib/hal/HalDisplay.h:28-32`), all whole-panel.

**This is the mechanism behind SnailOS's 123 ms claim**, and the honest reading
of that claim: they have a windowed path on a panel where we have none. It is
also the only thing that would move the note editor's typing latency, whose
floor is *"350 ms debounce + ~570 ms e-ink refresh = ~920 ms"*
(`docs/ble-editor-spike.md:88`) — the refresh is 62% of it, and no amount of
debounce tuning (`src/CrossPointSettings.h:285-300`) touches that half.

---

### D-3 — `bigbag/papyrix-reader`, the only MIT peer on our exact controllers

| | |
|---|---|
| **Project** | papyrix-reader |
| **URL** | <https://github.com/bigbag/papyrix-reader> |
| **License** | **MIT** (GitHub API `license.spdx_id`, 2026-09-14). Liftable with its copyright notice. |
| **Status** | 469★, C, pushed 2026-09-13. **Soft fork** — its own README says *"a fork of CrossPoint Reader by Dave Allie"*, so the GitHub API reports `fork: false` and `fork-ecosystem.md`'s fork list misses it. Included here because it is functionally independent and because its driver documentation is the most useful artifact found anywhere. |
| **Port effort** | Reading is free. Lifting a LUT is a measurement job, not a copy. |
| **Touches** | `freeink-sdk/libs/display/.../Ssd1677Driver.cpp`, the LUT headers |

Its `docs/ssd1677-driver.md` documents the X4 LUT layout at byte granularity:
111 bytes = 0–49 VS waveforms (5 groups × 10), 50–99 TP/RP timing (10 × 5),
100–104 frame-rate control, 105–109 voltages (VGH, VSH1, VSH2, VSL, VCOM). It
also names one concrete anti-ghosting technique we do not obviously use: **on a
partial update, write the image to command `0x24` AND command `0x26`** (current
and previous RAM) so the next update has a correct differential baseline.
Quoted timings: full ~1600 ms, partial ~600 ms. It also claims *"turbo LUTs with
cache for faster page turns on X3"*.

**Two hard caveats.**

1. **These are their documented numbers, read from a doc, not their source.** A
   LUT correct for their X4 batch may be wrong for ours — which is precisely why
   `BoardConfig.h:373-374` carries per-batch UC8179/UC8279 substitutions in the
   first place. Validate on glass before trusting a voltage.
2. **GPL contamination risk in two named subsystems.** Their README credits the
   CSS parser to `CidVonHighwind/microreader` (**GPL-2.0**) and the X4 hardware
   data to `bitbank2/bb_epaper` (**GPL-3.0**). A repo-level MIT statement does
   not launder an upstream GPL file. **Do not lift their CSS parser or their X4
   panel data.** The SSD1677 *documentation* is prose and is safe to read; any
   *code* taken from that area needs provenance checked file by file.

---

### D-4 — `hansmrtn/pulp-os`: strip rendering and a 3-phase DU refresh

**The best genuinely-external display find, and it is MIT.**

| | |
|---|---|
| **Project** | pulp-os |
| **URL** | <https://github.com/hansmrtn/pulp-os> |
| **License** | **MIT** (LICENSE fetched by the survey agent) |
| **Status** | 129★, Rust, last commit 2026-03-05. **From scratch** — zero `crosspoint` occurrences, verified by the agent against a clone. |
| **Port effort** | **Ideas only — it is Rust, nothing lifts as source.** The DU sequencing is a days-scale change in `Ssd1677Driver`; strip rendering is an architectural rewrite we almost certainly do not want. |
| **Touches** | `freeink-sdk/.../Ssd1677Driver.cpp`, potentially `lib/hal/HalDisplay` |

Two ideas, only one of which is worth acting on.

1. **A 3-phase partial refresh with a `RED = !BW` trick** (`ssd1677.rs:4-10`):
   `phase1_bw` → `start_du` (*"caller polls input while BUSY"*) → `phase3_sync`.
   On rapid navigation phase 3 is **skipped**, and `phase1_bw_inv_red` writes
   `RED = !BW` *"so DU drives every pixel to the correct BW target without a full
   GC."* **Measured** at `scheduler.rs:274-275`: **DU partial ~400 ms, full GC
   ~1.6 s.** This is the same family as papyrix's `0x24`+`0x26` double-write
   (D-3) — two independent projects arriving at "write both RAMs so the
   differential baseline is right" — which is the strongest signal in this
   survey that the technique is real.
2. **No framebuffer at all** (`drivers/strip.rs:2`): *"4 KB strip instead of
   48 KB framebuffer; display split into horizontal bands"* — 40-row strips ×
   12. Interesting against our 380 KB ceiling, and **not recommended**: our
   whole rendering stack assumes a resident framebuffer, including the
   lend/return loan mechanism (`lib/hal/HalDisplay.h:96-102`) that exists
   precisely so the 48 KB can be borrowed without being freed.

Its WiFi is STA-only with mDNS, **no portal and no TLS** — behind us. No BLE.

### B-2 — `Josh-writes/microslate-firmware`, a second BLE HID host

| | |
|---|---|
| **URL** | <https://github.com/Josh-writes/microslate-firmware> |
| **License** | **MIT** — and it is **CrossPoint-derived** (the agent diffed its `GfxRenderer.h` against our local copy: identical Bayer-dither comment and `enum Color`), so this is MIT-on-MIT and liftable. |
| **Status** | 184★, C. Presents as standalone; lineage established by diff, not by the GitHub `fork` field. |
| **Value** | A second independent BLE HID keyboard-host implementation (`ble_keyboard.cpp`, ~32 KB) to compare against B-1's, plus a claimed **~430 ms** refresh. |

Worth reading **alongside** B-1 rather than instead of it — two implementations
disagreeing about connection parameters or reconnect strategy is more
informative than either alone. Source not read by me.

### W-1 — WITHDRAWN 2026-09-14, on reading our own power manager

**Do not implement this as written.** The candidate below survives only as a
description of what Espressif measured; the recommendation it carried is wrong
for this firmware, and the reason is two lines in our own tree.

`HalPowerManager::setPowerSaving()` already does manual frequency scaling
(`lib/hal/HalPowerManager.cpp:51,59`, `setCpuFrequencyMhz` between
`LOW_POWER_FREQ` = 10 MHz and the boot frequency) — and at
`lib/hal/HalPowerManager.cpp:39-43` it **force-disables power saving whenever
WiFi is in any mode but `WIFI_MODE_NULL`**:

```
auto wifiMode = WiFi.getMode();
if (wifiMode != WIFI_MODE_NULL) {
  // Wifi is active, force disabling power saving
  enabled = false;
}
```

So the window Espressif's 20.82 → 10.71 mA DFS figure describes — *while
associated* — is precisely the window this firmware already refuses to scale in,
deliberately. The candidate's own §4 caveat said the addressable window was only
the seconds we are online; the truth is narrower still: it is zero.

Worse, the two mechanisms are mutually exclusive. `CONFIG_PM_ENABLE=y` makes
`setCpuFrequencyMhz()` the wrong API — under the power-management framework the
frequency is governed by `esp_pm_configure()` and lock acquisition, not by
direct calls — so switching it on without rewriting `HalPowerManager` would give
us two systems fighting over the same clock.

**What is actually behind this, and it is a bigger question than a Kconfig
flag**: the *reason* WiFi forces full speed is presumably that hand-scaling the
CPU breaks radio timing, which is the exact problem `esp_pm`'s locks exist to
solve properly. Replacing our manual scaling with the PM framework could make
scaling-while-associated safe for the first time. That is a rewrite of
`HalPowerManager` on a device the owner reads on, with the failure mode "the
radio or the panel SPI mistimes", and it wants a bench and a current meter — not
a config line. **Filed as a real candidate, not started.**

### W-1 — DFS and auto-light-sleep, a Kconfig question with a 17x answer

| | |
|---|---|
| **Source** | Espressif's own measured ESP32-C3 table, <https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-guides/low-power-mode/low-power-mode-wifi.html> |
| **License** | n/a — documentation, and the change is configuration, not code |
| **Status** | Figures VERIFIED as published by Espressif; **NOT measured on our hardware** |
| **Port effort** | Small to try, real to validate |
| **Touches** | `sdkconfig.defaults`, `src/network/OtaUpdater.cpp:167,213` |

Espressif's measured averages while associated, esp32c3:

| Mode | DTIM | Avg current |
|---|---|---|
| Min modem-sleep, **DFS off** | 3 | 20.82 mA |
| Min modem-sleep, **DFS on** | 3 | 10.71 mA |
| **Auto light-sleep + WiFi** | 3 | **0.62 mA** |

**Before anyone acts on this, read the negative result in §4 that half-kills
it**: our WiFi is never resident, so the addressable window is only the seconds
we are actually online. The honest framing is *"this makes an OTA or a library
sync cheaper"*, not *"this extends standby"*. That is still worth having, and it
is a config flag rather than code.

---

### W-2 — Wi-Fi Easy Connect (DPP), the one provisioning idea that suits a device with a screen

| | |
|---|---|
| **Source** | ESP-IDF `examples/wifi/wifi_easy_connect/dpp-enrollee`; <https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/network/esp_dpp.html> |
| **License** | **Apache-2.0** |
| **Status** | ESP32-C3 enrollee support stated on Espressif's own C3 docs page. **Not built or tried here.** |
| **Port effort** | Medium — new activity, QR render, `esp_supp_dpp_bootstrap_gen()` |
| **Touches** | `src/activities/network/`, and we already have `ricmoo/QRCode` in `platformio.ini:257` |

The C3 DPP doc says a display is required to show the bootstrap QR code. Most
ESP32 devices do not have one; **this one is a 792×528 e-ink panel**, and it has
no keyboard, which is the other half of the argument — today a WPA2 passphrase
is pecked out of an on-screen grid. DPP replaces that with: render QR, scan with
an Android 10+ phone, done.

**Android 10+ only. iOS has no enrollee support.** So this is strictly an
*addition* beside the existing SoftAP captive portal
(`src/activities/network/CrossPointWebServerActivity.cpp:288-298`), never a
replacement, and that halves its value.

---

### K-1 — non-US keyboard layouts: `keylayouts.h`, pointed backwards

| | |
|---|---|
| **Project** | Teensyduino core, `teensy4/keylayouts.h` |
| **URL** | <https://github.com/PaulStoffregen/cores/blob/master/teensy4/keylayouts.h> |
| **License** | **MIT** (standard MIT text in the file header, PJRC.COM LLC) |
| **Status** | VERIFIED by the agent from the file header; contents not independently re-read by me |
| **Port effort** | **Medium-to-large, and larger than it looks** |
| **Touches** | `src/notes/HidKeymap.h`, `test/ble_keymap` |

~20 layouts (US, US-Intl, UK, DE, DE-Mac, DE-CH, FR, FR-BE, FR-CH, CA-FR,
CA-Multi, DK, FI, NO, SE, IS, IT, PT, PT-BR, ES, ES-LA, TR), with dead keys
(`DEADKEYS_MASK`) and AltGr (`ALTGR_MASK`).

**The catch, stated up front so nobody budgets this as a copy-paste**: Teensy is
a keyboard *device*, so its tables map **character → keycode+modifiers**. We are
a *host* and need **keycode+modifiers → character**. Inverting the simple
layouts is mechanical (a generator script emitting a reverse table); inverting
the **dead keys** is not — a dead key becomes a two-keystroke state machine in
the decoder that our `hidkeymap::decodeReport()` (`src/notes/HidKeymap.h:84-115`)
has no shape for at all.

Our current state is one US table, `src/notes/HidKeymap.h:12-25`.

Rejected layout sources, recorded so they are not re-proposed: **QMK** (GPL-2.0),
**Linux `hid-input.c`** (GPL-2.0), **hidrd** (GPL-2.0) — all blockers.
**`T-vK/ESP32-BLE-Keyboard`** has **no LICENSE file at all** (API returns
`license: null`), which is worse than GPL — no grant means no right to copy, and
it is the device direction anyway. **`xkeyboard-config`/`libxkbcommon`** are
permissively licensed and would cover layouts Teensy lacks (Czech, Polish, Greek,
Cyrillic), but compiling a usable subset of the XKB data model into flash is a
larger project than the inversion above.

---

### Others found, priced, and NOT recommended

| Project | License | Why not |
|---|---|---|
| `t0mg/eenk` — <https://github.com/t0mg/eenk> | **MIT** (API) | Independent X3/X4 firmware in C, pushed 2026-09-14. Its **`esp_partition_mmap` trick** — copy data from SD into a flash partition, then map it into the MCU address space for zero-copy reads — is the most interesting idea found for the C3's no-PSRAM ceiling, and it is applicable to book and font data. **Not recommended yet** because it is an architectural change to `HalStorage`/`SdCardFont` with no measured benefit here, and because I read its README, not its source. Worth a spike, not a port. Also ships an SDL2 desktop simulator, which is an independent take on our own. |
| `Jon-Vii/marigold-os` — <https://github.com/Jon-Vii/marigold-os> | **MIT** (API) | Bare-metal `no_std` Rust on C3. **Nothing lifts into C++.** Its WASM browser emulator is an independent solution to what crosspoint-simulator solves — of interest to that repo, not this one. |
| `Rukkaitto/encre` — <https://github.com/Rukkaitto/encre> | **MIT** (API) | From-scratch X3/X4 EPUB reader on freeink-sdk. **0 stars, 42 open issues, created 2026-08-20.** A second implementation to read, not a source to lift. |
| `martinberlin/cale-idf` (CalEPD) | **Apache-2.0** | The best-licensed general e-paper driver naming C3 support. Irrelevant to us: we already have per-controller drivers with hand-tuned LUT banks, and its grayscale is thinner than FreeInk's. |
| `robertkist/libdither` | **BSD-2-Clause** (LICENSE fetched; API said NOASSERTION) | Blue noise, Ostromoukhov, DBS etc. Desktop-shaped — linear color space, floats, undocumented allocation. Would need per-algorithm extraction. Only relevant if we ever want better-than-Bayer, and Bayer here is a deliberate device-fidelity choice. |
| `deeptronix/dithering_halftoning` | **MIT** | Floyd–Steinberg / Atkinson, already embedded-shaped, but **dormant since 2023-11-05**. Pair with the above if dithering ever becomes a question. |
| `Modos-Labs/Glider` | **CERN-OHL-S v2** | Strongly reciprocal — **cannot be used** in an MIT tree. But its **IWF (Interchangeable Waveform Format)** is the best public explanation of how E Ink waveform tables are actually structured. **Read the format, reimplement, vendor nothing.** |
| `pasztorpisti/hid-report-parser` | **MIT** | A second report-descriptor parser. Superseded by B-0, which is already linked and already ESP-targeted. |
| TinyUSB `src/class/hid/hid.h` | **MIT** | Clean `HID_KEY_*` and Consumer Control usage enums, if we ever want them as named constants instead of the hex literals in `src/notes/HidKeymap.h:47-67`. Cosmetic. |
| `esp32beans/BLE_HID_Client` | **MIT** | A worked BLE-HID-central example — **last push 2022-09-19**, will not build against current NimBLE. Reference only. |
| `open-x4-epaper/community-sdk` | **MIT** | 129★. The ancestor of freeink-sdk's register sequences and LUTs, including X3-specific ones at our exact 792×528. **We already have them** — see N-19. Historical source-of-truth only. |
| `andrewjiang/flowe-os` | **MIT** | 48★, C. Claims *"~450 ms on the X3"* for its fast waveform vs *"~3 s"* full flash, driver-native partial windows with periodic full scrubs, and Bluetooth **ANCS** (phone notifications, not HID). Timings are its own claims, unverified. |
| `Jon-Vii/marigold-os` (again) | **MIT** | Its WiFi captive portal does **QR-code zero-config onboarding** — the same insight as W-2's DPP, reached without DPP. Claimed 473 ms page turn. |
| `dcherrera/CrossLuaReader` | **MIT** | 38★, most active of the "independents". **Not independent** — its LICENSE reads *"Derived from CrossPoint Reader, Copyright (c) 2025 Dave Allie."* Lua-on-SD app model, i.e. SnailOS's architecture in the open. Out of scope for a dedicated reader. |
| `maddiedreese/xteink-terminal` (detail) | **MIT** | The only one of the named seven with a **WiFiManager captive portal**. Claims full refresh *"roughly 1.7 seconds"*, partial refresh of changed line bands. Publishes an X4 EPD pinout (SCLK 8, MOSI 10, MISO 9, CS 21, DC 4, RST 5, BUSY 6) — useful only if ours is ever in doubt. |
| `zocs/eink-quick-flasher` | **MIT** | A flashing tool, not firmware. Its author's own caveat is the useful part: **X4 support is inferred from binwalk and was never tested on real X4 hardware.** |
| `maddiedreese/xteink-terminal` | **MIT** (API) | X4 as a WiFi terminal with a tmux bridge. Out of scope for a reader; source not read. |

### Rejected outright on license — recorded so they are not re-proposed

Every one of these is technically interesting and **none of it can be used**.

| Project | License (verified 2026-09-14) | Note |
|---|---|---|
| `ZinggJM/GxEPD2` | **GPL-3.0** (LICENSE fetched) | The de-facto Arduino e-paper library. Hard blocker. **And we do not use it** — see §4. |
| `bitbank2/bb_epaper` | **GPL-3.0** (API) | Includes X4 panel data. Hard blocker, and the contamination source for papyrix's X4 data. |
| `vroland/epdiy` | **LGPL-3.0** (API) | Doubly wrong: LGPL is effectively copyleft in a statically linked flash image, **and** it drives parallel-interface panels, not SPI controllers. Does not mention SSD1677 or UC8253 at all. |
| `jjwbruijn/OpenEPaperLink` | **CC-BY-NC-SA-4.0** (LICENSE fetched) | **NonCommercial *and* ShareAlike.** Untouchable, not merely inconvenient. |
| `Modos-Labs/Glider` | **CERN-OHL-S v2** | Strongly reciprocal. |
| `joeycastillo/The-Open-Book` | **CC-BY-SA-4.0** (API) | ShareAlike, SAMD51 not ESP32, **abandoned 2023-12-05**. |
| `SolderedElectronics/Inkplate-Arduino-library` | **LGPL-3.0** (API) | |
| `NiLuJe/FBInk` | **GPL-3.0** | Also a Linux framebuffer, wrong platform. |
| `fread-ink/inkwave` | **GPL-2.0** | WBF waveform container decoder, derived from Kindle GPL kernel code. Documentation value only. |
| `CidVonHighwind/microreader` | **GPL-2.0** (API) | |
| `azw413/TernOS`, `HookedBehemoth/TrustyReader` | **GPL-2.0** (API) | Rust, Xteink X4. TernOS quiet since 2026-03-19, and `README.md:245` says *"This repo was originally cloned from TrustyReader"* despite the API reporting `fork: false`. |
| `aimindseye/rustmix-x4-firmware` | **No LICENSE = all rights reserved** | 12★. Its SSD1677 driver is **pulp-os's 682 lines verbatim + 11 added** (agent diffed it). Take from pulp-os (MIT) instead; this adds nothing and cannot be copied. |
| `h0rv/rust-xteink-x4` (ox4) | **No LICENSE** | 6★. Has the best WiFi provisioning of the named independents (`wifi_manager.rs`, 423 lines, AP/STA) and it is **unusable**. BLE is roadmap-only. OTP LUTs only. |
| `X4Term` | **No LICENSE** | Unusable. |

**THE GRAYSCALE-LUT LINEAGE IS GPL-2.0 AND IS CLOSED TO US.**
`CidVonHighwind/microreader` (GPL-2.0, from scratch) is the root; TrustyReader
credits it at `x4/src/eink_display.rs:5`; TernOS cloned TrustyReader. The prize
in there is TrustyReader's second grayscale LUT — two 110-byte tables written to
command `0x32`, with LUT2 commented ***"~60% faster than LUT1, slightly lower
quality"*** (same VS waveforms, faster TP/RP timing). **That is exactly the
"turbo bank" idea we would want and it may not be copied.** Note the tension
worth being careful about: `freeink-sdk/NOTICE` credits *"Original e-paper
driver authorship ... to CidVonHighwind"* while the community-sdk it derives
from is released MIT. The MIT grant is what we rely on; recorded here as an
observation, not an allegation, and a reason not to go reaching into
`microreader` itself for anything.
| ESPHome (incl. its `epaper_spi` SSD1677 grayscale PR #19213 and its WiFi fast-connect) | **GPLv3 for all C/C++** (LICENSE fetched — the Python half is MIT, the firmware half is not) | Read for design, write our own. Its RTC-vs-flash split for cached BSSIDs is the idea worth stealing conceptually. |
| `CrazyCoder/cr2xt` | **No LICENSE file** (API `license: null`) | Based on crengine (GPL-2.0). Treat as GPL. |
| `chegewara/esp32-hid-keyboard-client` | **No LICENSE at all**, last push 2019-12-22 | Widely cited in forum answers as *the* HID client example. **All rights reserved — do not copy.** |
| `waveshareteam/e-Paper` | **Unresolved** — no repo-level license found | Treat as unlicensed until someone reads the per-file headers. |
| `ngxson/esphome-component-xteink` | **No license** (API) | Unusable. |

---


---

## 4. NEGATIVE RESULTS — things checked that we already have

This is the half that stops a candidate being re-proposed. Each entry is a
thing another project does that looked liftable until it was checked against
our source.

### N-1 — "Use ESP-IDF's `esp_hidh` HID host instead of hand-rolling one"

**Proposed by both web passes. It does not link.** Full evidence in B-0b above:
`nimble_hidh.c.o` is in the precompiled C3 archive and defines **zero** symbols,
and nothing else defines the `esp_ble_hidh_init` / `_dev_open` / `_deinit` that
`esp_hidh.c.o` needs. Enabling it means rebuilding ESP-IDF, which this project
has already proven it cannot do
(`freeink-sdk/libs/network/SecureNet/include/SecureClient.h:11-13`). The parser
half (`esp_hid_common.c.o`, 23 symbols) **is** usable — that is B-0, and it is
the only part to take.

### N-2 — "CrossPoint uses GxEPD2, so swap in a better e-paper library"

Asserted by SnailOS's own comparison table. **False for this tree.**
`grep -ri gxepd platformio.ini dependencies.lock src lib` returns **zero** hits.
The only two matches anywhere are historical comments in an unrelated driver
(`freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8179Driver.cpp:144`,
`.../Uc8179Driver.h:52`). Our display comes from `freeink-sdk` via
`platformio.ini:248` (`EInkDisplay=symlink://freeink-sdk/libs/display/FreeInkDisplay`),
with per-controller drivers and hand-tuned LUT banks
(`freeink-sdk/libs/display/FreeInkDisplay/src/lut/Uc8253X3Luts.h`,
`Ssd1677Luts.h`, `Uc8279X3Luts.h`, `Uc8253MurphyLuts.h`). GxEPD2 is GPL-3.0
anyway and could never have been used.

### N-3 — "Add custom LUTs / waveform banks for faster or cleaner refresh"

**We already have six banks per controller, and they are documented.**
`Uc8253X3Luts.h:1-11` names them: `_normal` (differential BW page turn),
`_half` (scrub bank to reset after AA grayscale), `_fast` (turbo — full
voltages, shortened timing), `_full` (OEM stock quality image write), `_gc`
(4-level grayscale). 43 bytes each, five tables per bank (VCOM + ww/bw/wb/bb).
A proposal here needs to say *which bank* and *what measurement*, or it is not a
proposal.

### N-4 — "Add 4-level grayscale"

**Shipped.** `lib/hal/HalDisplay.h:110-130` — `preconditionGrayscale()` (the
OEM X3 "AA-pre-BW(mid)" settle pass, windowable to the gray region),
`displayGrayscaleBase()`, `copyGrayscaleBuffers()` / `...Lsb...` / `...Msb...`,
`cleanupGrayscaleBuffers()`, and a **tiled streaming** path
(`writeGrayscalePlaneStrip()` `:129`, `supportsStripGrayscale()` `:130`) so a plane need not be
resident. SnailOS has **no** grayscale at all; we are ahead here.

### N-5 — "Add a de-ghosting / periodic full-refresh policy"

**Shipped, and user-configurable.** `src/CrossPointSettings.h:136-143` —
`REFRESH_1/5/10/15/30`, pages between full refreshes. A `FORCE_REFRESH` power
button action (`src/CrossPointSettings.h:147`). Per-activity ghost cleanup at
`src/activities/reader/XtcReaderActivity.cpp:257` and
`src/activities/reader/EpubReaderActivity.cpp:232,1099,1284`. A HALF-refresh
promotion after an inverted frame at `lib/hal/HalDisplay.cpp:67-68,150`.

### N-6 — "Add async / non-blocking refresh so the CPU can work during the waveform"

**Shipped.** `lib/hal/HalDisplay.h:80-85` — `displayBufferAsync()`,
`waitRefreshComplete()`, `supportsAsyncRefresh()`, with a documented
blocking fallback for drivers that cannot defer, and a busy-wait slice hook
(`:93`) so long BUSY polls do useful work.

### N-7 — "Add a captive portal for WiFi setup"

**Shipped.** `src/activities/network/CrossPointWebServerActivity.cpp:288-298`
starts a `DNSServer` wildcarding every query to the SoftAP IP (allocated with
`new (std::nothrow)` at `:294` because the portal is a convenience, not a
requirement), and `src/network/CrossPointWebServer.cpp:437-444` 302-redirects
unmatched non-`/api/` requests so the OS opens its browser. mDNS at
`:73-74`. **tzapu/WiFiManager (MIT) and ESPAsync_WiFiManager (MIT, archived
2023-01-16) both offer strictly less than this** — neither has a multi-network
store.

### N-8 — "Add a multi-network credential store / remember more than one SSID"

**Shipped.** `src/WifiCredentialStore.h:35` — `MAX_NETWORKS = 8`
(`MAX_PASSWORD_LENGTH = 64` at `:36`), with a last-connected SSID, XOR
obfuscation against the device MAC, base64, and a mutex that never waits on SD
I/O (`:29-33`). Hidden-SSID join at
`src/activities/network/WifiSelectionActivity.cpp:207`; async scan at `:129`.

### N-9 — "Pin the BSSID and channel to make reconnect faster"

**Deliberately not done, and there is a comment saying why.**
`src/activities/network/WifiSelectionActivity.cpp:445-446`: *"Scan all channels
so networks with multiple APs use the strongest matching BSSID instead of the
first match found by the framework's default fast scan."* We already read back
the associated BSSID and channel for diagnostics (`:494-498`). Re-proposing a
pinned BSSID means arguing against that trade — in a home with one AP it is free
speed; in a mesh it picks a weaker radio — and it must keep a full-scan fallback
or an AP that changed channel becomes unreachable.

### N-10 — "Turn on WiFi power save / DTIM tuning to extend battery"

**Mostly not addressable, because WiFi is never resident.** `src/main.cpp:627-631`
tears the radio down (`WiFi.disconnect(true); WiFi.mode(WIFI_OFF)`) before deep
sleep so the modem power domain is not held, and every network feature brings
the radio up and puts it back down for the duration (`src/notes/ClaudeChat.cpp:182-242`,
`src/activities/network/CrossPointWebServerActivity.cpp:179,229`). We already
toggle power save around the one long transfer that matters —
`esp_wifi_set_ps(WIFI_PS_NONE)` for the OTA download
(`src/network/OtaUpdater.cpp:167`) and back to `WIFI_PS_MIN_MODEM` after
(`:213`). So W-1's 17x figure applies only to the seconds we are online, not to
standby. The CPU side is already handled too:
`lib/hal/HalPowerManager.h:48-57` downclocks after 500 ms idle to
`LOW_POWER_FREQ` — **10 MHz on the C3 boards**, 80 MHz where `BOARD_HAS_PSRAM`, and
`lib/hal/HalPowerManager.cpp:158,224` uses `esp_light_sleep_start()`.

### N-11 — "Stream HTTP bodies instead of buffering, to survive the tiny heap"

**Shipped.** `src/network/HttpDownloader.cpp:153-156` streams a GET body through
`sink.write` in `READ_CHUNK` pieces, using the manual read loop specifically
because the event-callback API *"pushes the whole body through an event
callback"*. This is the same discipline SnailOS advertises; we got there first
and for the same reason.

### N-12 — "Bring our own TLS because the framework's mbedTLS is crippled"

**Shipped.** `freeink-sdk/libs/network/SecureNet/include/SecureClient.h:1-20`
documents it: the precompiled mbedTLS has TLS 1.3 stubbed out, a `-D` cannot
change a precompiled `.a`, so wolfSSL is compiled from source
(`platformio.ini:263`, `wolfssl/Arduino-wolfSSL @ 5.7.2`) and used through
`SecureHttpClient` (`src/network/HttpDownloader.cpp:12,61`).

### N-13 — "`ngxson/pluspoint-reader` has an OS rewrite worth mining"

**Checked, nothing to take for these three areas.** Local checkout at `60e9739`,
**2026-06-08 — three months quiet**. MIT (ngxson). A `grep -rl -i
"nimble|ble_hid|hogp"` across its C/C++ sources returns **nothing**: it has no
Bluetooth of any kind, so it is silent on priority 1.

### N-14 — "Read `cr2xt` / the CrazyCoder X3 EPD gist for the X3 LUTs"

**Two problems.** The gist has **no license** (reference only, do not copy), and
its central claim contradicts our tree: it describes the X3 as an **SSD1677**
part. Our `BoardConfig.h` enumerates the X3's controllers as **UC8253**
(original) and **UC8279d** (`BoardConfig.h:113` — *"UC8279d — X3 (792x528),
replaces the UC8253"*, and `:347` `XteinkX3Uc8279`), and SSD1677 as the **X4 /
X4 Pro / Sticky** part (`:93-97`, `:114`, `:348`). Either the gist is about a
different revision or it is wrong. **Unresolved — do not use its LUT dumps for
an X3 without first confirming which controller the board actually has.**

### N-15 — "SnailOS is doing something clever with Bluetooth keyboards"

**It has no HID support at all.** Its BLE is phone-notification sync via an
Android companion app. We have a working BLE HID keyboard host, device-confirmed
2026-08-05 against a real keyboard (`docs/ble-editor-spike.md:16-20`). **We are
ahead of the competitor on the owner's top-priority axis**, which is worth
knowing before spending anything to catch up on it.

### N-16 — ">4 grey levels on a mono SPI controller by stacking LUTs"

**Searched for and not found, anywhere, under any license.** Every 16-level
implementation located runs on an **IT8951**, which is an active controller with
its own processor and image memory doing the waveform work in hardware — not
transferable to a UC8253 or SSD1677. As of 2026-09-14 this appears to be
unexplored in open source. Recorded so it is not re-searched; the route in, if
anyone wants it, is understanding the waveform format properly
(Glider/IWF, CERN-OHL-S, read-only) rather than looking for code to copy.

### N-17 — "There must be a published ghosting-compensation algorithm to adopt"

**Also searched for and not found.** No open firmware with a citable
partial-update accumulation counter or historical-frame compensation scheme
turned up — only vendor marketing and issue threads. The single concrete,
licence-clean technique found in the whole survey is papyrix's `0x24`+`0x26`
double-write (D-3).

### N-19 — "`open-x4-epaper/community-sdk` has X3-specific reverse-engineered LUTs at 792×528 — take them"

**We already have them, verbatim, with attribution.** VERIFIED in our own
checkout. `freeink-sdk/libs/display/FreeInkDisplay/src/lut/Uc8253X3Luts.h:3-4`
says it outright: *"relocated verbatim from the community-sdk `main` lineage
(the production X3 implementation crosspoint ships)"*, and that includes the
4-level grayscale bank the proposal is about (`_gc`, `:107-131`, commented
*"community 4-level grayscale (reader AA + cover rendering)"*). Attribution is
carried at `freeink-sdk/NOTICE:10-17` — *"the SSD1677 and UC8253 panel
initialization sequences and waveform LUTs are derived from that work."*
The community-sdk's last push was 2026-04-25; `freeink-sdk` is its live
successor and is what we consume (`platformio.ini:248`).

### N-20 — "One of the independent Xteink firmwares has a BLE keyboard host to copy"

**None of the seven does.** Checked individually: `pulp-os` no BLE; `TernOS` no
BLE; `TrustyReader` no BLE; `Rustmix`'s ring remote is an explicit stub
(`r10_ble_transport.rs:3`, *"This is not the hardware BLE implementation yet"*)
and unlicensed anyway; `ox4` has BLE as a roadmap item at priority Low;
`CrossLuaReader` and `xteink-terminal` have none. The only two working BLE HID
hosts in this whole ecosystem are **`freeink-sdk`'s** (B-1, already in our
submodule) and **`microslate-firmware`'s** (B-2). `flowe-os` has Bluetooth ANCS,
which is phone notifications, not HID — the same thing SnailOS has.

### N-21 — Checked and found CLEAN / not applicable

- **NimBLE-Arduino** (Apache-2.0, active): enumerated — it ships
  `NimBLEHIDDevice` and **no HID client of any kind**. Since we are already on
  the raw NimBLE C API, it offers the host role nothing. Skip.
- **LILYGO T-Deck keyboard firmwares**, `s-term`, `T-REX-FIRMWARE`: all either
  I²C keyboard-matrix MCUs or BLE HID *peripherals*. Wrong direction.
- **Improv Wi-Fi** (Apache-2.0, active, tiny): solves the problem our captive
  portal already solves, and would run BLE in a second concurrent role beside
  the HID central. Not a gap-filler.
- **ESP-IDF `wifi_provisioning`** (Apache-2.0): same BLE-concurrency objection,
  plus it needs a vendor phone app. DPP (W-2) is the better trade.
- **`Watchy`, `atomic14/diy-esp32-epub-reader`**: MIT but dormant (2025-08-19 and
  2022-11-29) and both assume classic ESP32 with PSRAM.
- **`h0rv/ssd1677`** (MIT, 2★, stub), **`melastmohican/epdsi`** (Apache-2.0, 5★,
  new), **`xiaobaoaaaaaa/ESPaperPlay`** (MIT, 0★, S3-only): too thin to rely on.
- **`turgu1/EPub-InkPlate`**: mixed license — the application layer is MIT, the
  InkPlate drivers are **LGPL-3.0**. Its EPUB/pagination layer is the mature part
  and is liftable; its drivers are not. Out of scope for these three areas.

---


---

## 5. What could not be verified

Stated plainly, because the difference between these and the rest of the file is
the whole point of the file.

**About SnailOS**

- Its **123 ms partial-refresh figure**, its 499 ms factory figure, and the
  two-week battery life. Vendor marketing, no methodology, no raw data, and the
  same site says elsewhere that a refresh is *"about half a second"*.
- Whether **any Snail OS code derives from CrossPoint**, in either direction.
  Not established. CrossPoint's MIT licence would permit a closed derivative
  with attribution; their "reverse engineered from the datasheet" claim implies
  otherwise. **No assertion is being made here.**
- Whether its signed-image pipeline actually publishes hashes — at access,
  `/verify` said *"0 images on this server"* while claiming 2.2.1 was current.
- **No third-party coverage of Snail OS exists** that I could find (HN, Reddit,
  MobileRead, Hackaday, Liliputing, the-ebook-reader). One HN thread
  (`item?id=49596629`) returned HTTP 429 and could not be read. Everything in §2
  comes from the vendor's own pages.

**About the candidates**

- **B-0's runtime behaviour.** I verified that `esp_hid_parse_report_map` is
  declared, defined, self-contained, and on our link line. I did **not** compile
  or run it. It `malloc`s, and contiguous heap is this device's scarce resource
  (`src/notes/BleHidHost.cpp:643-658`) — a report map is small, but that needs
  measuring, not assuming.
- **D-1's 44 display commits.** I read commit *subjects*, not diffs. Good
  evidence a problem was found; weak evidence about the fix. 151 commits is also
  not a small submodule bump, and the `hardware` (51) and `ui` (42) halves move
  surface we depend on.
- **D-2 on the X3.** That windowed refresh is absent for UC8253 is verified. That
  it is *achievable* on UC8253 is **not** — it needs a partial-window LUT and
  register sequence that does not exist in this tree, and I have no evidence the
  controller's OTP or the community's LUT lineage supports one.
- **D-3's LUT byte layout and voltages.** Read from papyrix's documentation, not
  from its source or from a panel. Validate before trusting a voltage.
- **W-1's power figures.** Espressif's published measurements, not ours, and on
  a reference board rather than this one.
- **W-2 DPP.** C3 enrollee support is stated by Espressif. Not built, not tried.
- **K-1.** The agent read `keylayouts.h`'s header for the licence; I did not
  independently re-read the file or count its layouts.
- **`t0mg/eenk`'s `esp_partition_mmap` trick**, and its vendored `inkcpp`
  submodule's licence. README-level only; the submodule's licence is unchecked
  and must be before anything from that subtree is touched.
- **`maddiedreese/xteink-terminal`**, **TernOS's embedded 68k emulator
  provenance**, and **`waveshareteam/e-Paper`'s licence**: unresolved.
- **The seven "independent" firmwares that article names WERE checked** in a
  later pass (see D-4, B-2, N-19, N-20 and the rejected table) — but note the
  article itself is wrong twice: `CrossLuaReader`'s own LICENSE says *"Derived
  from CrossPoint Reader"*, and `TernOS`'s `README.md:245` says it was cloned
  from TrustyReader. **The GitHub `fork` field is near-useless for lineage in
  this ecosystem**, because people copy source rather than forking; `papyrix`,
  `microslate` and `CrossLuaReader` all report `fork: false` and all three are
  CrossPoint-derived. Still unchecked from that article: `inx`, `Marginalia`,
  `Inkpoint`.
- **The measured page-turn figures quoted for other projects** — pulp-os
  400 ms / 1.6 s, microslate ~430 ms, flowe-os ~450 ms / ~3 s, marigold-os
  473 ms, TrustyReader/TernOS 1720 ms half, xteink-terminal ~1.7 s full — come
  from those projects' own source comments or READMEs. **None was measured here,
  and they are not measured the same way**, so they rank badly against each
  other and against our 570 ms (`docs/ble-editor-spike.md:42`), which is a
  whole-panel FAST refresh timed on an X4. Do not build an argument on this
  column.

**A methodological caveat about the whole file.** The three web passes were
subagents. Where their claims were load-bearing I re-checked them against this
machine — which is how N-1 and N-2 were caught, both of them recommendations
that would have been acted on and were wrong. Claims in this file that are
*only* attributed to a web pass (licences of projects we are not adopting,
star counts, last-push dates) have not been double-checked.

**And I tried to double-check the licences myself and could not.** By the time
this file was assembled the unauthenticated GitHub API quota was exhausted
(`curl https://api.github.com/repos/espressif/esp-idf` → HTTP 403,
*"API rate limit exceeded"*), and `gh api` returned `401 Bad credentials`. So
**every third-party licence in §3 rests on a subagent's report, not on my own
read.** Before any code is lifted from any project named here — papyrix (D-3)
and Teensy's `keylayouts.h` (K-1) are the two that would actually be copied —
re-fetch its LICENSE and confirm it first-hand. The two licences that matter
most and ARE first-hand are `freeink-sdk`'s (`freeink-sdk/LICENSE:1`, in our own
checkout) and ESP-IDF's `esp_hid` Apache-2.0 SPDX header, which ships in the
package installed on this machine.

---

## 6. If only three things get done

1. **B-0** — `#include "esp_hid_common.h"`, call `esp_hid_parse_report_map()` on
   characteristic 0x2A4B, and stop guessing report layouts by length. Apache-2.0,
   already linked, one file touched. It is the precondition for everything else
   in priority area 1.
2. **B-1's connection parameters** — `ble_gap_update_params()` with an 8 s
   supervision timeout. Our BLE link has no defence against a long chapter render
   blocking the single core past NimBLE's 2.56 s default, and `freeink-sdk`'s own
   host already documents that exact failure
   (`freeink-sdk/libs/network/BleKeyboardHost/src/BleKeyboardHost.cpp:408-418`).
   Directed reconnect and auto-repeat ride along cheaply.
3. **D-1** — start taking the 44 upstream `freeink-sdk` display commits, per the
   per-commit discipline in `fork-sync.md`. Two of them name ghosting and one is
   an X3 UC8253 power-sequence fix. No licence question, no port, no new
   dependency.

Everything else on this list is a project. These three are edits.

**The one to look at fourth** is D-4's `RED = !BW` DU sequencing, because two
independent MIT projects converged on the same "write both RAMs so the
differential baseline is correct" idea (pulp-os's phase1_bw_inv_red and
papyrix's `0x24`+`0x26`), and because it attacks the one number that actually
bounds the note editor (`docs/ble-editor-spike.md:88` — the refresh is 62% of
the ~920 ms floor). It is a driver change, not a copy, and it needs measuring on
our glass before it means anything.

---

## 7. One registry correction, noted in passing

`~/.claude/CLAUDE.md`'s row for `~/src/pluspoint-reader` says its submodule is
`crosspoint-reader/open-x4-sdk`. The checkout's `.gitmodules` names
**`crosspoint-reader`**. Not changed here — this file is read-only — but worth
fixing next time that registry is edited.

