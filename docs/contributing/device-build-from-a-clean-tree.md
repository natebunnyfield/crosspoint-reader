# Device builds from a clean tree

Written 2026-08-06 after a device build from a fresh worktree failed five
different ways in a row. Everything here is reproducible; each item is a
separate fault with its own unrelated error message, which is why they are
worth writing down together.

`platformio.ini:76-78` says the `custom_sdkconfig` core rebuild "needs the
CMake pin in platformio.local.ini on macOS" and never says what the pin is.
That file is gitignored and has never been committed — `git log --all --
platformio.local.ini` is empty, and the comment arrived with **upstream**
`f0a50557`, describing an upstream contributor's machine. So the pin is not
recoverable from this repo's history. What follows is reconstructed from the
failures themselves.

## The headline: the core rebuild drops Bluetooth

**`custom_sdkconfig` and `src/notes/BleHidHost.cpp` are mutually exclusive as
currently configured.** Measured on a clean worktree at `397105fd`:

| | rebuilt core | stock package |
|---|---|---|
| libs produced | 50 | — |
| `libbt.a` / NimBLE | **0** | `libbt.a` present |

`BleHidHost.cpp:112` includes `host/ble_gap.h`, so a completed core rebuild
gives:

```
src/notes/BleHidHost.cpp:112:10: fatal error: host/ble_gap.h: No such file or directory
```

This is not a config problem. `CONFIG_BT_ENABLED=y`, `CONFIG_BT_NIMBLE_ENABLED=y`
and `CONFIG_BT_CONTROLLER_ENABLED=y` are all present in `sdkconfig.defaults`
**and** in the generated `sdkconfig.<env>`. The libs are simply never built, so
the BT include directories never make it onto `CPPPATH` — a compiled TU's `.d`
file contains zero `bt/` or `nimble` paths, while the stock package's
`esp32c3/flags/includes` does list `bt/host/nimble/nimble/nimble/host/include`.

### The corollary nobody has noticed

A build that *succeeds* today is a build where the rebuild **silently did not
run** (see "stale scaffold" below) and the stock prebuilt core was used
instead. That core has Bluetooth, which is why `BleHidHost.cpp` compiles there.

It also means those builds never got the `custom_sdkconfig` heap reclamation
that `platformio.ini:76` credits with ~32-37 KB. **The MEMFIX saving is
probably not in any binary built this way.** Do not cite it as active without
checking that the rebuild actually ran.

Resolving this is a decision, not a fix: add the BT/NimBLE component to the
rebuild, drop `custom_sdkconfig` and give up the heap saving, or gate
`BleHidHost` behind a build flag. Left open deliberately.

## The four setup faults, in the order they bite

### 1. Stale scaffold makes the rebuild skip in silence

Leftover `.dummy/`, `CMakeLists.txt`, `sdkconfig.default`, `sdkconfig.defaults`
from an interrupted rebuild cause the rebuild to be skipped entirely. The build
then falls through to whatever include set those leftovers imply, and dies on
`host/ble_gap.h` — the *same* error as the Bluetooth problem above, from a
completely different cause. Symptom that distinguishes them: a skipped rebuild
finishes suspiciously fast (~45 s, mostly cache hits).

Cleanup is already documented at `platformio.ini:80-82`:

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
`"owner": null` in `.piopm`. Those two specs never match, so the lookup returns
`None` even with 243 MB of CMake sitting in `packages/tool-cmake`.

Installing through the platform's own spec registers the owner correctly:

```bash
/Users/<you>/.platformio/penv/bin/python -c "from platformio.platform.factory import PlatformFactory; p=PlatformFactory.new('espressif32'); p.pm.install(p.get_package_spec('tool-cmake')); print(p.get_package_dir('tool-cmake'))"
```

It prints a real path when it works. **This is almost certainly what the
mystery "CMake pin" was for.**

Do **not** substitute `platformio/tool-cmake` — a different owner, which
unpacks over the same `packages/tool-cmake` directory and leaves the lookup
broken in a new way. CMake 4.x is *not* the problem; 4.0.3 is what the platform
declares and wants.

### 3. A fresh worktree has no `.cache`

```
*** [.dummy/sketch.cpp] <worktree>/.cache/.sconsign313.dblite: No such file or directory
```

`build_cache_dir = .cache` and SCons will not create the directory itself.

```bash
mkdir -p .cache
```

### 4. `.pio/libdeps` and `fs_` are per-checkout

ArduinoJson lives under `.pio/libdeps` (written per checkout by PlatformIO) and
`fs_` is gitignored, so a worktree has neither. For the host test tree this is
handled — `test/CMakeLists.txt` falls back to the primary checkout for both,
and logs which it used. A worktree also needs
`git submodule update --init --recursive` for `freeink-sdk`.

## `tool-cmake` is shared state, and it ping-pongs

`~/.platformio` is global. Because `tool-cmake` is `optional: true`, a build
from a project that does not request it can prune it, breaking a concurrent
build that does. Observed directly: installed and verified resolving at 21:52,
gone by 21:53, reinstalled with a non-matching owner by another checkout's
build.

If two agents or two checkouts build firmware at once, expect this. Check for
running builds before touching the package store:

```bash
ps aux | grep -E "platformio|scons" | grep -v grep
```

Full isolation via a private `PLATFORMIO_CORE_DIR` costs ~4.9 GB (riscv
toolchain 2.3 G, arduino-libs 1.8 G, espidf 469 M, cmake 243 M), which is not
always affordable — see below.

## Disk

`build_cache_dir = .cache` is capped at 3 GB by `scripts/build_cache_policy.py`
**per checkout**, with no global budget. With eight worktrees that is 24 GB by
design; measured 2026-08-06 it was ~10 GB across eight checkouts on a volume at
97 % (7.0 GiB free). Worth a look before starting anything that needs room:

```bash
du -sh ~/src/crosspoint-reader/.cache ~/src/crosspoint-reader/.claude/worktrees/*/.cache 2>/dev/null | sort -rh
```
