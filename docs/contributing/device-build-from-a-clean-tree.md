# Device builds from a clean tree

Written 2026-08-06 after a device build from a fresh worktree failed five
different ways in a row, then rewritten the same night when the headline
diagnosis turned out to be wrong. Each item below is a separate fault with its
own unrelated error message, which is why they are worth recording together.

## The one that actually blocked everything: `lib_ignore = BLE`

**Fixed 2026-08-06.** Kept here because the symptom is so misleading.

```
src/notes/BleHidHost.cpp:112:10: fatal error: host/ble_gap.h: No such file or directory
```

`lib_ignore = BLE` in `[base]` reads as "skip the Arduino BLE library, which we
do not use". pioarduino does not treat it as a library name.
`component_manager.py:899-903` maps the token `ble` onto the ESP-IDF
**component** `bt`, and the lib_ignore handler then regex-deletes every `bt`
CPPPATH entry out of the framework package's `pioarduino-build.py` — 55 of
them, including line 300:

```
join(FRAMEWORK_SDK_DIR, "esp32c3", "include", "bt", "host", "nimble", "nimble", "nimble", "host", "include")
```

That is the only `-I` that resolves `<host/ble_gap.h>`. `-lbt` survives the
strip, so there is no link error to point at the cause — it surfaces as a
missing header, which is why it reads as a broken ESP-IDF core.

It applied to every device env (all extend `[base]`); the simulator envs carry
their own `lib_ignore` and were never affected. Removing it costs nothing:
nothing in `src/`, `lib/` or `freeink-sdk/` includes `BLEDevice.h`,
`BLEUtils.h`, `BluetoothSerial.h` or `SimpleBLE.h`, and device envs use the
default LDF chain mode, so the Arduino BLE library was never in the graph.

`spike/ble-editor` had already hit this and worked around it in
`spike-build.sh:6-9`, injecting the ten nimble `-I` paths through
`PLATFORMIO_BUILD_FLAGS` — "325 -I flags reach a project TU and none of them is
nimble". That workaround is superseded.

### Corrections to the first version of this document

The first version of this file blamed `custom_sdkconfig`. That was wrong, and
the wrong reasoning is recorded here because it was superficially convincing:

* **"The rebuilt core produces 50 libs and zero Bluetooth ones."** False. Those
  50 `lib*/` directories in `.pio/build/<env>/` are PlatformIO **LDF library**
  directories (`lib000/GfxRenderer`, `lib10e/SPI`), not ESP-IDF component
  archives. `framework-arduinoespressif32-libs/esp32c3/lib/libbt.a` is present,
  11.8 MB, and contains `ble_gap.c.o`.
* **"`custom_sdkconfig` and `BleHidHost` are mutually exclusive."** False. The
  include strip runs on *every* device build via `arduino.py:917-925`, whether
  or not a core rebuild happens.
* **"The ~32-37 KB MEMFIX heap saving was never active in any binary."**
  Unfounded — it followed only from the two errors above. Nothing here says
  anything about whether that saving is present.

The lesson worth keeping: `.pio/build/<env>/lib*/` are LDF dirs. Counting them
tells you nothing about which IDF components were compiled.

## Fresh-worktree setup faults

These are real and still apply.

### 1. Stale scaffold makes the core rebuild skip in silence

Leftover `.dummy/`, `CMakeLists.txt`, `sdkconfig.default`, `sdkconfig.defaults`
from an interrupted rebuild cause it to be skipped entirely. Symptom: the build
finishes suspiciously fast (~45 s, mostly cache hits). Cleanup is documented at
`platformio.ini:80-82`:

```bash
rm -rf .dummy CMakeLists.txt sdkconfig.default sdkconfig.defaults .pio/build/<env>
```

All four are untracked and gitignored. Do **not** use `git clean -fdX` — it
deletes `platformio.local.ini`.

### 2. `tool-cmake` resolves to None

```
Error: Missing CMake package directory 'None'
```

`espidf.py:904` calls `platform.get_package_dir("tool-cmake")` and exits if it
is missing. `platform.json` declares that package `optional: true` with
`owner: pioarduino` plus a release URL — but the platform installs it through
its own `~/.platformio/tools/` staging with a `file://` URI, which registers
`"owner": null` in `.piopm`. Those specs never match, so the lookup returns
`None` even with 243 MB of CMake sitting in `packages/tool-cmake`.

Installing through the platform's own spec registers the owner correctly:

```bash
~/.platformio/penv/bin/python -c "from platformio.platform.factory import PlatformFactory; p=PlatformFactory.new('espressif32'); p.pm.install(p.get_package_spec('tool-cmake')); print(p.get_package_dir('tool-cmake'))"
```

It prints a real path when it works. This is most likely what the "CMake pin in
platformio.local.ini" at `platformio.ini:76-78` referred to — that file is
gitignored, has never been committed, and the comment arrived with **upstream**
`f0a50557`, so its contents are not recoverable from this repo.

Do **not** substitute `platformio/tool-cmake` — a different owner, which
unpacks over the same directory and leaves the lookup broken in a new way.
CMake 4.x is not the problem; 4.0.3 is what the platform declares.

### 3. A fresh worktree has no `.cache`

```
*** [.dummy/sketch.cpp] <worktree>/.cache/.sconsign313.dblite: No such file or directory
```

`build_cache_dir = .cache` and SCons will not create the directory.

```bash
mkdir -p .cache
```

### 4. `.pio/libdeps` and `fs_` are per-checkout

ArduinoJson lives under `.pio/libdeps` (written per checkout) and `fs_` is
gitignored, so a worktree has neither. The host test tree handles this —
`test/CMakeLists.txt` falls back to the primary checkout for both and logs
which it used. A worktree also needs
`git submodule update --init --recursive` for `freeink-sdk`.

## `tool-cmake` is shared state, and it ping-pongs

`~/.platformio` is global. Because `tool-cmake` is `optional: true`, a build
from a project that does not request it can prune it, breaking a concurrent
build that does. Observed directly: installed and verified resolving at 21:52,
gone by 21:53, reinstalled with a non-matching owner by another checkout's
build. Check before touching the package store:

```bash
ps aux | grep -E "platformio|scons" | grep -v grep
```

Full isolation via a private `PLATFORMIO_CORE_DIR` costs ~4.9 GB (riscv
toolchain 2.3 G, arduino-libs 1.8 G, espidf 469 M, cmake 243 M).

## Disk

`build_cache_dir = .cache` is capped at 3 GB by `scripts/build_cache_policy.py`
**per checkout**, with no global budget. Eight worktrees is 24 GB by design;
measured 2026-08-06 it was ~10 GB across eight checkouts on a volume at 97 %
(7.0 GiB free).

```bash
du -sh ~/src/crosspoint-reader/.cache ~/src/crosspoint-reader/.claude/worktrees/*/.cache 2>/dev/null | sort -rh
```

## Verifying a device build actually worked

A green build is not proof the BLE path compiled — check the object and its
include trail:

```bash
ls .pio/build/gh_release/src/notes/BleHidHost.cpp.o
grep -o "bt/host/nimble[^ ]*ble_gap.h" .pio/build/gh_release/src/notes/BleHidHost.cpp.d
```

Reference point, `gh_release` at `7fee9a8c` (2026-08-06): builds clean,
`firmware.bin` 4,492,096 bytes, RAM 16.5 %, Flash 68.3 %,
`BleHidHost.cpp.o` 510,140 bytes with 46 nimble include paths in its `.d`.
