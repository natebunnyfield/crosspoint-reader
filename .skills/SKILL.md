# CrossPoint Reader Development Guide

> **This file is the real one; `CLAUDE.md` is a symlink to it.** Edits made
> through the symlink land here, but `git add CLAUDE.md` stages only the
> (unchanged) symlink, so the edit is silently left behind and the commit looks
> clean. Stage `.skills/SKILL.md`. This swallowed two commits' worth of
> corrections on 2026-08-07 before anyone noticed.

Project: Open-source e-reader firmware for Xteink X4 (ESP32-C3)
Mission: Provide a lightweight, high-performance reading experience focused on EPUB rendering on constrained hardware.

## AI Agent Identity and Cognitive Rules
* Role: Senior Embedded Systems Engineer (ESP-IDF/Arduino-ESP32 specialized).
* Primary Constraint: 380KB RAM is the hard ceiling. Stability is non-negotiable.
* Evidence-Based Reasoning: Before proposing a change, you MUST cite the specific file path and line numbers that justify the modification.
* Anti-Hallucination: Do not assume the existence of libraries or ESP-IDF functions. If you are unsure of an API's availability for the ESP32-C3 RISC-V target, check the freeink-sdk source or the FreeInk SDK docs (https://freeink.org/llms.txt for an LLM-readable index) first.
* No Unfounded Claims: Do not claim performance gains or memory savings without explaining the technical mechanism (e.g., DRAM vs IRAM usage).
* Resource Justification: You must justify any new heap allocation (new, malloc, std::vector) or explain why a stack/static alternative was rejected.
* Verification: After suggesting a fix, instruct the user on how to verify it (e.g., monitoring heap via Serial or checking a specific cache file).
---

## Development Environment Awareness

**CRITICAL**: Detect the host platform at session start to choose appropriate tools and commands.

### Platform Detection
```bash
# Detect platform (run once per session)
uname -s
# Returns: MINGW64_NT-* (Windows Git Bash), Linux, Darwin (macOS)
```

**Detection Required**: Run `uname -s` at session start to determine platform

### Platform-Specific Behaviors
- **Windows (Git Bash)**: Unix commands, `C:\` paths in Windows but `/` in bash, limited glob (use `find`+`xargs`)
- **Linux/WSL**: Full bash, Unix paths, native glob support

**Cross-Platform Code Formatting**:
```bash
find src -name "*.cpp" -o -name "*.h" | xargs clang-format -i
```

**Do NOT run that across the whole tree — format only the files your change
touches.** Run as written on 2026-08-18 it reformatted **16 files nobody had
edited**: the tree is not clang-format-clean and has not been for a long time.
A blanket sweep is therefore a large whitespace diff that buries the real change,
and it collides with every other session holding uncommitted work in another
worktree. Owner ruling 2026-08-18: leave the tree as it is and format per-file.
CI is not failing on those 16, so nothing is broken — the command is simply
wider than it looks.

---

## Platform and Hardware Constraints

### Hardware Specs
* MCU: ESP32-C3 (Single-core RISC-V @ 160MHz)
* RAM: ~380KB usable (VERY LIMITED - primary project constraint)
  * **NO PSRAM**: ESP32-C3 has no PSRAM capability (unlike ESP32-S3)
  * **Single Buffer Mode**: Only ONE 48KB framebuffer (not double-buffered)
* Flash: 16MB (Instruction storage and static data)
* Display: 800x480 E-Ink (Slow refresh, monochrome, 1-2s full update)
  * Framebuffer: 48,000 bytes (800 × 480 ÷ 8)
* Storage: SD Card (Used for books and aggressive caching)

### The Resource Protocol
1. Stack Safety: Limit local function variables to < 256 bytes. The ESP32-C3 default stack is small; use std::unique_ptr or static pools for larger buffers.
2. Heap Fragmentation: Avoid repeated new/delete in loops. Allocate buffers once during onEnter() and reuse them.
3. Flash Persistence: Large constant data (UI strings, lookup tables) MUST be marked static const to stay in Flash (Instruction Bus), freeing DRAM.
4. String Policy: Prohibit std::string and Arduino String in hot paths. Use std::string_view for read-only access and snprintf with fixed char[] buffers for construction.
5. UI Strings: All user-facing text must use the `tr()` macro (e.g., `tr(STR_LOADING)`) for i18n support. Never hardcode UI strings directly. For the avoidance of doubt, logging messages (LOG_DBG/LOG_ERR) can be hardcoded, but user-facing text must use `tr()`.
6. `constexpr` First: Compile-time constants and lookup tables must be `constexpr`, not just `static const`. This moves computation to compile time, enables dead-branch elimination, and guarantees flash placement. Use `static constexpr` for class-level constants.
7. `std::vector` Pre-allocation: Always call `.reserve(N)` before any `push_back()` loop. Each growth event allocates a new block (2×), copies all elements, then frees the old one — three heap operations that fragment DRAM. When the final size is unknown, estimate conservatively.
8. SPIFFS Write Throttling: Never write a settings file on every user interaction. Guard all writes with a value-change check (`if (newVal == _current) return;`). Progress saves during reading must be debounced — write on activity exit or every N page turns, not on every turn. SPIFFS sectors have a finite erase cycle limit.
9. `new` is not nothrow on ESP32: With `-fno-exceptions`, bare `new` that fails calls `abort()` — it does NOT return `nullptr`. Always use `new (std::nothrow)` and null-check the result, or use `makeUniqueNoThrow<T>()` from `lib/Memory/Memory.h`. Never write bare `new` for any fallible allocation.

---

## Project Architecture

### Build System: PlatformIO

**PlatformIO is BOTH a VS Code extension AND a CLI tool**:

1. **VS Code Extension** (Recommended):
   * Extension ID: `platformio.platformio-ide` (see `.vscode/extensions.json`)
   * Provides: Toolbar buttons, IntelliSense, integrated build/upload/monitor
   * Configuration: `.vscode/c_cpp_properties.json`, `.vscode/tasks.json`
   * Usage: Click Build (✓), Upload (→), or Monitor (🔌) buttons

2. **CLI Tool** (`pio` command):
   * **Installation**: Python package (typically `pip install platformio`)
   * **Windows Location**: `C:\Users\<user>\AppData\Local\Programs\Python\Python3xx\Scripts\pio.exe`
   * **Verify**: `which pio` (Git Bash) or `where.exe pio` (cmd)
   * **Usage**: `pio run`, `pio run -t upload`, etc.

**Configuration Files**:
* `platformio.ini`: Main build configuration (committed to git)
* `platformio.local.ini`: Local overrides (gitignored, create if needed)
* `partitions.csv`: ESP32 flash partition layout

### Build Environment
* **Standard**: C++20 (`-std=c++2a`). No Exceptions, No RTTI.
* **Logging**: ALWAYS use `LOG_INF`, `LOG_DBG`, or `LOG_ERR` from `Logging.h`. Raw Serial output is deprecated.
* **Environments** (in `platformio.ini`):
  * `default`: Development (LOG_LEVEL=2, serial enabled)
  * `gh_release`: Production (LOG_LEVEL=0)
  * `gh_release_rc`: Release candidate (LOG_LEVEL=1)
  * `slim`: Minimal build (no serial logging)

**One C3 binary serves both X4 and X3.** The C3 envs set `-DFREEINK_DEVICE_X4=1`
and `-DFREEINK_DEVICE_X3=1` together, and `XteinkDetect` probes the panel at boot
to pick the right one (the dual path is `BoardConfig.h`'s
`#elif FREEINK_DEVICE_X3 && !FREEINK_DEVICE_X4`). Do not go looking for a
separate X3 target. `sticky` is ESP32-S3 — a different MCU family, not an
alternative X3 build.

**Overriding the version string.** `scripts/git_branch.py` force-injects
`CROSSPOINT_VERSION` for `-e default` only, so adding your own
`-DCROSSPOINT_VERSION` there collides ("macro redefined"), and the shell-escaped
quotes usually arrive mangled and break the build outright. Use the mechanism
that already exists instead:
```bash
CROSSPOINT_RC_HASH=mytag pio run -e gh_release_rc   # -> 1.5.0-rc+mytag
```
The dev version carries no dirty marker: a build from a modified working tree
reports the same string as a clean build of the same commit. If a binary has to
be identifiable later, stamp it this way or commit first — and build shipping
bins from a clean throwaway worktree, because the main checkout routinely carries
someone's uncommitted WIP that would land in the binary invisibly.

`CROSSPOINT_RC_HASH` also feeds `project.checksum`, so **toggling it between runs
wipes every environment's build directory** — the same blast radius as editing
`platformio.ini` (below), and easy to trigger by accident when you dip into
`gh_release_rc` once just to read a Flash number. Hold the value constant across
a session; a mid-session measurement otherwise costs you the `simulator` build
you were iterating on.

**Editing `platformio.ini` wipes every build directory.** Any change alters
`.pio/build/project.checksum`, and the next `pio run` cleans *all* env build
dirs, not only the one being built. Do not edit it mid-task to tweak a flag —
prefer an env var or an existing env — or finished device builds vanish and you
pay a full rebuild.

**Never add `lib_ignore = BLE` (or `bluetooth`, `bluetoothserial`, `simpleble`,
`esp-nimble-cpp`).** It reads as "skip an Arduino library we do not use", which
sounds free. pioarduino does not treat it as a library name:
`component_manager.py:899-903` maps any of those tokens onto the ESP-IDF
**component** `bt`, and the lib_ignore handler then regex-deletes all 55 `bt`
CPPPATH entries from the framework package's `pioarduino-build.py` — including
the one `-I` that resolves `<host/ble_gap.h>`. `src/notes/BleHidHost.cpp` then
cannot compile, and because `-lbt` survives the strip it fails as a **missing
header with no link error**, which reads as a broken ESP-IDF core rather than a
one-line config entry. This shipped as a real bug and cost a whole session
before `7fee9a8c` removed it; `platformio.ini` now carries a comment saying so.

**A device build from a fresh clone or worktree needs four things nobody
documented** — a cleared rebuild scaffold, a `tool-cmake` package that actually
resolves, a `mkdir -p .cache`, and the submodule. Each fails with its own
unrelated error message. See
[docs/contributing/device-build-from-a-clean-tree.md](docs/contributing/device-build-from-a-clean-tree.md)
before debugging one of them from scratch. That file also records the verify
step: a green build is not proof the BLE path compiled — check
`.pio/build/<env>/src/notes/BleHidHost.cpp.o` exists and that its `.d` carries
the nimble include trail.

**The build cache is capped now.** `build_cache_dir = .cache` is a
content-addressed SCons CacheDir with no eviction of its own — it reached
**11 GB in ten days** (19,781 entries) before `scripts/build_cache_policy.py`
was added. That script caps it at 3 GB, evicting least-recently-used first
(APFS updates atime on read here, so recency is a real signal), and calls
`env.NoCache()` on the link artifact: 63 unstripped 51–57 MB `.elf` files were
3.2 GB of that 11 GB, and re-linking is cheap next to a recompile. Override with
`CROSSPOINT_BUILD_CACHE_MAX_MB`; `0` disables the cap but still keeps `.elf`s
out of the cache.

**The cache is shared across worktrees, via the environment.** `build_cache_dir`
in the ini is a *relative* path, so every git worktree used to get its own
private `.cache` — each honouring the 3 GB cap independently. Measured
2026-08-06: 7.1 GB spread over seven worktree caches on top of the main
checkout's 3 GB. The machine now exports
`PLATFORMIO_BUILD_CACHE_DIR=$HOME/.platformio/build_cache` (in the personal
dotfiles, not committed here), which PlatformIO honors over the ini
(`platformio/project/options.py`, `sysenvvar`). Do **not** "fix" this by putting
an absolute path in `platformio.local.ini`: that file is gitignored, so a newly
created worktree would not have it and would silently fall back to a private
cache — the exact bug. Sharing also lifts hit rates, since worktrees of one repo
compile identical translation units.

**Adding or deleting a translation unit breaks the next iOS build in the
OTHER repo.** The simulator's iOS target compiles this firmware from a
generated source list (`crosspoint-simulator/cmake/CrossPointSources.cmake`),
and its configure gate refuses when the list goes stale — correctly, and
silently if nobody is watching the Terminal tab it dies in. It cost a
TestFlight cut on 2026-08-21 when the settings reduction deleted plumbing TUs.
After any TU add/delete/rename, regenerate from this repo:
`pio run -e simulator -t compiledb && python3 ../crosspoint-simulator/tools/gen_cmake_sources.py --firmware-dir . --compile-db compile_commands.json`
— the gate's own error prints the same commands.

**Adding or deleting a translation unit breaks the next iOS build in the
OTHER repo.** The simulator's iOS target compiles this firmware from a
generated source list (`crosspoint-simulator/cmake/CrossPointSources.cmake`),
and its configure gate refuses when the list goes stale -- correctly, and
silently if nobody is watching the Terminal tab it dies in. It cost a
TestFlight cut on 2026-08-21 when the settings reduction deleted plumbing TUs.
After any TU add/delete/rename, regenerate from this repo:
`pio run -e simulator -t compiledb && python3 ../crosspoint-simulator/tools/gen_cmake_sources.py --firmware-dir . --compile-db compile_commands.json`
-- the gate's own error prints the same commands.

Two things about how it is wired, both of which cost a debugging cycle to find:

* It is registered as **both `pre:` and `post:`**. During `pre:`, `PROGNAME` is
  still SCons's default `program`, so `$BUILD_DIR/${PROGNAME}.elf` resolves to
  `program.elf` — a node that never gets built, and `NoCache` silently does
  nothing. The espressif32 builder sets `PROGNAME=firmware` only by `post:`
  time. Pruning still runs in `pre:`, because on a full disk it has to happen
  before anything compiles. A per-`$PIOENV` marker in `os.environ` separates the
  phases — do not try to detect the phase from the program name, since native
  simulator envs genuinely link a binary called `program`.
* It is listed in **both `[base]` and `[env:simulator]`** `extra_scripts`.
  `[env:simulator]` does not `extends = base`, so a single entry silently misses
  all four simulator envs.

Two things make this cache grow superlinearly, worth knowing before touching
flags: a C3 object and a native arm64 object for the same translation unit are
different hashes (nothing is shared across the nine envs), and `[base]
build_flags` reaches every TU via `${base.build_flags}` — flip one define and
all ~150 TUs miss at once and re-store under fresh hashes while the previous
generation stays on disk.

### Critical Build Flags
These flags in `platformio.ini` fundamentally affect firmware behavior:

```cpp
-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1  // Single framebuffer (saves 48KB RAM!)
-DARDUINO_USB_MODE=1                 // Enable USB CDC
-DARDUINO_USB_CDC_ON_BOOT=1          // Serial available immediately at boot
-DXML_CONTEXT_BYTES=1024             // XML parser memory limit (EPUB parsing)
-DUSE_UTF8_LONG_NAMES=1              // SD card long filename support
-DMINIZ_NO_ZLIB_COMPATIBLE_NAMES=1   // Avoid zlib name conflicts
-DXML_GE=0                           // Disable XML general entities (security)
-DDESTRUCTOR_CLOSES_FILE=1           // FsFile destructor auto-closes (SdFat)
```

**DESTRUCTOR_CLOSES_FILE implications**:
- SdFat's `FsBaseFile` destructor calls `close()` automatically when the object goes out of scope
- **Do NOT add explicit `file.close()` calls** for local `FsFile` variables — the destructor handles it
- Explicit `close()` is still required in these cases:
  1. **Close before delete**: Must close before `Storage.remove()` on the same path
  2. **Close before reopen**: Must close before reopening the same `FsFile` variable (e.g., write then reopen for read, or rewrite the same path)
  3. **Member variables**: `FsFile` members persist beyond any single function scope, so close at the intended release point (e.g., in `onExit()`)

**SINGLE_BUFFER_MODE implications**:
- Only ONE framebuffer exists (not double-buffered)
- Grayscale rendering requires temporary buffer allocation (`renderer.storeBwBuffer()`)
- Must call `renderer.restoreBwBuffer()` to free temporary buffers
- See [lib/GfxRenderer/GfxRenderer.cpp:439-440](../lib/GfxRenderer/GfxRenderer.cpp) for malloc usage

### Directory Structure
* lib/: Internal libraries (Epub engine, GfxRenderer, UITheme, I18n)
  * lib/hal/: Hardware Abstraction Layer (HalDisplay, HalGPIO, HalStorage)
  * lib/I18n/: Internationalization (translations in `translations/*.yaml`, generated string tables)
* src/activities/: UI logic using the Activity Lifecycle (onEnter, loop, onExit)
* freeink-sdk/: Low-level SDK (EInkDisplay, InputManager, BatteryMonitor, SDCardManager)
* .crosspoint/: SD-based binary cache for EPUB metadata and pre-rendered layout sections

### Hardware Abstraction Layer (HAL)

**CRITICAL**: Always use HAL classes, NOT SDK classes directly.

| HAL Class | Wraps SDK Class | Purpose | Singleton Macro |
|-----------|----------------|---------|-----------------|
| `HalDisplay` | `EInkDisplay` | E-ink display control | *(none)* |
| `HalGPIO` | `InputManager` | Button input handling | *(none)* |
| `HalStorage` | `SDCardManager` | SD card file I/O | `Storage` |

**Location**: [lib/hal/](../lib/hal/)

**Why HAL?**
- Provides consistent error logging per module
- Abstracts SDK implementation details
- Centralizes resource management

**Example - HalStorage**:
```cpp
#include <HalStorage.h>

// Use Storage singleton (defined via macro)
HalFile file;
if (Storage.openFileForRead("MODULE", "/path/to/file.bin", file)) {
  // Read from file
  // No file.close() needed — DESTRUCTOR_CLOSES_FILE=1 handles it at scope exit
}
```

**Usage**: Use `HalFile` (the mutex-wrapping handle), NOT raw SdFat `FsFile` or Arduino `File`. Do NOT add `file.close()` for local variables (see DESTRUCTOR_CLOSES_FILE above).

**SdFat is not thread-safe; all SD access MUST go through HalStorage**:
- SdFat's `SdSpiCard` tracks SPI bus state with an unsynchronized `m_spiActive` bool. Two tasks calling SdFat concurrently can confuse that state machine and end with one task calling `SPIClass::endTransaction()` against a paramLock the *other* task is holding. That trips FreeRTOS's `xTaskPriorityDisinherit` assert (`tasks.c:5156, pxTCB == pxCurrentTCBs[0]`) and panics the system. See SdFat issue #518.
- `HalStorage` serializes everything via `storageMutex`. Downstream code uses `HalFile` (declared in `<HalStorage.h>`); every method call (read, write, seek, close) takes the mutex. `HalFile`'s destructor also takes the mutex before letting the underlying SdFat `FsFile` close.
- **Never** call into `SdFat` / `SdSpiCard` / `FsBaseFile` / `SDCardManager` / raw `FsFile` directly — that bypasses the mutex.

---

## Firmware binaries on the SD card

`SdFirmwareUpdateActivity` shows a file browser over every `.bin` on the card, so
whatever is there is a menu the owner picks from.

**Name them `<UTC-timestamp>-crosspoint-<sha>.bin`**, e.g.
`20260804T0210Z-crosspoint-53737d47.bin`. The sha alone was the old convention
and it is not enough: it says *which* commit but nothing about *when*, so a card
carrying two builds gives no way to tell which is newer without going back to
git — and the owner picking from that list has no git. The timestamp sorts, the
sha identifies. **Delete superseded bins** rather than leaving them; every one
left behind is a chance to flash the wrong firmware.

**Build shipping bins from a clean tree, and check that it is clean.** This is
the same rule as the version-string note above, but here it is worse, because a
`.bin` on a card outlives the session that built it and carries no dirty marker
at all:
```bash
git status --porcelain   # must be EMPTY before pio run -e gh_release
```
A `git worktree` at the intended sha is the belt-and-braces version. The main
checkout routinely carries another session's WIP — on 2026-08-04 it literally
did, mid-task, while a shipping binary was being built from it.

---

## Coding Standards

### Naming Conventions
* Classes: PascalCase (e.g., EpubReaderActivity)
* Methods/Variables: camelCase (e.g., renderPage())
* Constants: UPPER_SNAKE_CASE (e.g., MAX_BUFFER_SIZE)
* Private Members: memberVariable (no prefix)
* File Names: Match Class names (e.g., EpubReaderActivity.cpp)

### Header Guards
* Use #pragma once for all header files.

### Memory Safety and RAII
* Smart Pointers: Prefer std::unique_ptr. Avoid std::shared_ptr (unnecessary atomic overhead for a single-core RISC-V).
* RAII: Use destructors for cleanup. Call `vTaskDelete()` explicitly for deterministic task release. Do NOT call `file.close()` on local `FsFile` variables — `DESTRUCTOR_CLOSES_FILE=1` handles it at scope exit (see Critical Build Flags).

### ESP32-C3 Platform Pitfalls

#### `std::string_view` and Null Termination
`string_view` is *not* null-terminated. Passing `.data()` to any C-style API (`drawText`, `snprintf`, `strcmp`, SdFat file paths) is undefined behavior when the view is a substring or a view of a non-null-terminated buffer.

**Rule**: `string_view` is safe only when passing to C++ APIs that accept `string_view`. For any C API boundary, convert explicitly:
```cpp
// WRONG - undefined behavior if view is a substring:
renderer.drawText(font, x, y, myView.data(), true);

// CORRECT - guaranteed null-terminated:
renderer.drawText(font, x, y, std::string(myView).c_str(), true);

// CORRECT - for short strings, use a stack buffer:
char buf[64];
snprintf(buf, sizeof(buf), "%.*s", (int)myView.size(), myView.data());
```

#### `IRAM_ATTR` and Flash Cache Safety
All code runs from flash via the instruction cache. During SPI flash operations (OTA write, SPIFFS commit, NVS update) the cache is briefly suspended. Any code that can execute during this window — ISRs in particular — must reside in IRAM or it will crash silently.

```cpp
// ISR handler: must be in IRAM
void IRAM_ATTR gpioISR() { ... }

// Data accessed from IRAM_ATTR code: must be in DRAM, never a flash const
static DRAM_ATTR uint32_t isrEventFlags = 0;
```

**Rules**:
- All ISR handlers: `IRAM_ATTR`
- Data read by `IRAM_ATTR` code: `DRAM_ATTR` (a flash-resident `static const` will fault)
- Normal task code does **not** need `IRAM_ATTR`

#### ISR vs Task Shared State
`xSemaphoreTake()` (mutex) **cannot** be called from ISR context — it will crash. Use the correct primitive for each communication direction:

| Direction | Correct primitive |
|---|---|
| ISR → task (data) | `xQueueSendFromISR()` + `portYIELD_FROM_ISR()` |
| ISR → task (signal) | `xSemaphoreGiveFromISR()` + `portYIELD_FROM_ISR()` |
| Task → task | `xSemaphoreTake()` / mutex |
| Simple flag (single writer ISR) | `volatile bool` + `portENTER_CRITICAL_ISR()` |

#### RISC-V Alignment
ESP32-C3 faults on unaligned multi-byte loads. Never cast a `uint8_t*` buffer to a wider pointer type and dereference it directly. Use `memcpy` for any unaligned read:

```cpp
// WRONG — faults if buf is not 4-byte aligned:
uint32_t val = *reinterpret_cast<const uint32_t*>(buf);

// CORRECT:
uint32_t val;
memcpy(&val, buf, sizeof(val));
```

This applies to all cache deserialization code and any raw buffer-to-struct casting. `__attribute__((packed))` structs have the same hazard when accessed via member reference.

#### Template and `std::function` Bloat
Each template instantiation generates a separate binary copy. `std::function<void()>` adds ~2–4 KB per unique signature and heap-allocates its closure. Avoid both in library code and any path called from the render loop:

```cpp
// Avoid — heap-allocating, large binary footprint:
std::function<void()> callback;

// Prefer — zero overhead:
void (*callback)() = nullptr;

// For member function + context (common activity callback pattern):
struct Callback { void* ctx; void (*fn)(void*); };
```

When a template is necessary, limit instantiations: use explicit template instantiation in a `.cpp` file to prevent the compiler from generating duplicates across translation units.

---

### Error Handling Philosophy

**Source**: [src/main.cpp:132-143](../src/main.cpp), [lib/GfxRenderer/GfxRenderer.cpp:10](../lib/GfxRenderer/GfxRenderer.cpp)

**Pattern Hierarchy**:
1. **LOG_ERR + return false** (90%): `LOG_ERR("MOD", "Failed: %s", reason); return false;`
2. **LOG_ERR + fallback**: `LOG_ERR("MOD", "Unavailable"); useDefault();`
3. **assert(false)**: Only for fatal "impossible" states (framebuffer missing)
4. **ESP.restart()**: Only for recovery (OTA complete)

**Rules**: NO exceptions, NO abort(), ALWAYS log before error return

### Heap Buffer Allocation

**Prefer `makeUniqueNoThrow` over `malloc`.** Both are nothrow (return `nullptr` on OOM rather than calling `abort()`), but `malloc` requires a manual `free` on every return path — a common source of leaks. `makeUniqueNoThrow<uint8_t[]>(size)` from `lib/Memory/Memory.h` frees automatically when it goes out of scope.

**Preferred pattern**:
```cpp
#include <Memory.h>

auto buffer = makeUniqueNoThrow<uint8_t[]>(bufferSize);
if (!buffer) {
  LOG_ERR("MODULE", "OOM: %d bytes", bufferSize);
  return false;
}

processData(buffer.get(), bufferSize);
// freed automatically — no manual free needed, no leak on early return
```

**`malloc` or `new (std::nothrow)` are still acceptable** when the buffer must be passed to a C API that takes ownership and frees it itself (e.g., certain SDK callbacks). In that case follow the manual pattern:
```cpp
auto* buffer = static_cast<uint8_t*>(malloc(bufferSize));  // or new (std::nothrow) uint8_t[bufferSize]
if (!buffer) {
  LOG_ERR("MODULE", "OOM: %d bytes", bufferSize);
  return false;
}
sdkApiThatTakesOwnership(buffer, bufferSize);  // SDK calls free() / delete[]
```

**Rules**:
- **Prefer `makeUniqueNoThrow`** — automatic cleanup eliminates leak risk on error paths
- **ALWAYS check for nullptr** after any allocation and `LOG_ERR` before returning false
- **Raw allocation only** when a C API takes ownership; document why in a comment

**Examples in codebase**:
- Memory utilities: [Memory.h](../lib/Memory/Memory.h) (`makeUniqueNoThrow`)
- Cover image buffers: [HomeActivity.cpp:166](../src/activities/home/HomeActivity.cpp)
- Bitmap rendering: [GfxRenderer.cpp:439-440](../lib/GfxRenderer/GfxRenderer.cpp)

### Heap Allocation with `new`: Always Use `makeUniqueNoThrow`

**CRITICAL**: With `-fno-exceptions`, bare `new` on OOM calls `abort()` — it does NOT return `nullptr`. Always use `makeUniqueNoThrow` from `lib/Memory/Memory.h`, which wraps `new (std::nothrow)` and returns a `std::unique_ptr` that is null on OOM and automatically frees on scope exit.

**Preferred pattern**:
```cpp
#include <Memory.h>

auto obj = makeUniqueNoThrow<MyClass>(args);
if (!obj) { LOG_ERR("MOD", "OOM: MyClass"); return false; }

auto buf = makeUniqueNoThrow<uint8_t[]>(size);
if (!buf) { LOG_ERR("MOD", "OOM: %d bytes", size); return false; }

// Pass to C APIs via .get(); unique_ptr frees automatically on return
someApi(buf.get(), size);
```

**`new (std::nothrow)` directly is acceptable** when the object must be passed to a C API that takes ownership and calls `delete` itself:
```cpp
auto* obj = new (std::nothrow) MyClass(args);
if (!obj) { LOG_ERR("MOD", "OOM: MyClass"); return false; }
sdkApiThatTakesOwnership(obj);  // SDK calls delete
```

**Rules**:
- **Prefer `makeUniqueNoThrow`** — automatic cleanup eliminates leak risk on error paths
- **NEVER use bare `new`** — always `makeUniqueNoThrow` or `new (std::nothrow)`
- **ALWAYS `LOG_ERR` before returning false** on OOM
- **Use `.get()`** to pass the raw pointer to C-style APIs; ownership stays with the `unique_ptr`
- **`new (std::nothrow)` directly only** when a C API takes ownership; document why in a comment

**Examples in codebase**:
- Memory utilities: [Memory.h](../lib/Memory/Memory.h) (`makeUniqueNoThrow`)

---

## Common Patterns

### Singleton Access
**Available Singletons**:
```cpp
#define SETTINGS CrossPointSettings::getInstance()  // User settings
#define APP_STATE CrossPointState::getInstance()    // Runtime state
#define GUI UITheme::getInstance()                   // Current theme
#define Storage HalStorage::getInstance()            // SD card I/O
#define I18N I18n::getInstance()                     // Internationalization
```

### Activity Lifecycle and Memory Management

**Source**: [src/main.cpp:132-143](../src/main.cpp)

**CRITICAL**: Activities are **heap-allocated** and **destroyed on exit** — but
`ActivityManager` owns them through `unique_ptr`, and nothing calls `delete`.
This section previously showed a raw-pointer `delete currentActivity` pattern
attributed to `main.cpp`; that has not been the code for some time. The real
shape (`src/activities/ActivityManager.h:41-47`, `.cpp:128-178`):

```cpp
// ActivityManager owns every activity.
std::vector<std::unique_ptr<Activity>> stackActivities;
std::unique_ptr<Activity> currentActivity;
std::unique_ptr<Activity> pendingActivity;   // staged, swapped in at a safe point

// Teardown (ActivityManager.cpp:168-169)
currentActivity->onExit();
currentActivity.reset();                     // destructor runs here

// A transition is STAGED, not immediate (:178, :146)
pendingActivity = std::move(newActivity);
// ... later, at the top of the loop:
currentActivity = std::move(pendingActivity);
```

The staging matters: an activity that asks to leave is not destroyed inside its
own callback, which is what makes `finish()` safe to call from `loop()`.

**Memory Implications**:
- Activity navigation = `delete` old activity + `new` create next activity
- Any memory allocated in `onEnter()` MUST be freed in `onExit()`
- FreeRTOS tasks MUST be deleted in `onExit()` before activity destruction
- Member `FsFile` handles MUST be closed in `onExit()` (local `FsFile` variables auto-close via destructor)

**Activity Pattern**:
```cpp
void onEnter()  { Activity::onEnter(); /* alloc: buffer, tasks */ render(); }
void loop()     { mappedInput.update(); /* handle input */ }
void onExit()   { /* free: vTaskDelete, free buffer, close member FsFiles */ Activity::onExit(); }
```

**Critical**: Free resources in reverse order. Delete tasks BEFORE activity destruction.

### FreeRTOS Task Guidelines

**Source**: [src/activities/util/KeyboardEntryActivity.cpp:45-50](../src/activities/util/KeyboardEntryActivity.cpp)

**Pattern**: See Activity Lifecycle above. `xTaskCreate(&taskTrampoline, "Name", stackSize, this, 1, &handle)`

**Stack Sizing** (in BYTES, not words):
- **2048**: Simple rendering (most activities)
- **4096**: Network, EPUB parsing
- Monitor: `uxTaskGetStackHighWaterMark()` if crashes

**Rules**: Always `vTaskDelete()` in `onExit()` before destruction. Use mutex if shared state.

### Global Font Loading

**Source**: [src/main.cpp:40-115](../src/main.cpp)

**All fonts are loaded as global static objects** at firmware startup:
- Noto Serif: 12, 14, 16, 18pt (4 styles each: regular, bold, italic, bold-italic)
- Noto Sans: 12, 14, 16, 18pt (4 styles each)
- System-font matrix: Ubuntu, Noto Sans, Noto Serif and Libre Franklin at
  8/10/12pt, regular + bold, plus a `_2x` companion of each on a
  `CROSSPOINT_RENDER_SCALE > 1` host build. Regenerate with
  `lib/EpdFont/scripts/convert-builtin-fonts.sh`, which now scripts the whole
  matrix (it previously covered neither the 8pt nor any 2x cut).

**Which faces the UI chrome actually draws with.** Three ids carry all of it:
`SMALL_FONT_ID` (22 draw sites — battery readout, page counters), `UI_10_FONT_ID`
and `UI_12_FONT_ID` (27 sites — headers, list rows, button hints, popups). Which
*family* fills them is the **System font** setting (`SETTINGS.systemFont`,
System tab), and `applySystemFont()` in `main.cpp` binds all three at once from
a 3x2 matrix — 8/10/12 pt, regular and bold. Offered: Ubuntu, Noto Sans, Noto
Serif, **Libre Franklin (the default since 2026-08-03)**. Before that the three
were hardcoded to Noto Sans 8 + Ubuntu 10/12. The Noto Sans/Serif 12–18 families
still appear only in `getReaderFontId()`'s built-in fallback and the calendar
sleep screen. Confirm with:
```bash
grep -rn "NOTOSERIF_\|NOTOSANS_" --include=*.cpp --include=*.h src/ lib/ | grep -v fontIds.h | grep -v main.cpp
```

Two traps in that path, both of which shipped as silent wrong-pixel bugs:

* **`insertFont()` refuses to overwrite** — it logs `Font ID N already
  registered, ignoring duplicate` and returns. `applySystemFont()` runs at boot
  *and* on every change of the setting, so it must use **`replaceFont()`**.
  Going through `insertFont` made every change after boot a no-op: the settings
  row read "Ubuntu" while every pixel stayed on the old face until reboot.
* **Built-in hi-res companions are not SD fonts.** `registerHiResBuiltinFont()`
  puts them in `hiResFontMap_` only. `clearSdCardFonts()` must therefore clear
  *only* the SD-backed entries (`clearSdCardHiResFonts()`); clearing the map
  outright wiped the chrome's 2x bitmaps on the first reader font/size change,
  since every one of those routes through `SdCardFontManager::unloadAll()`.
* **Every text-draw entry point needs its own hi-res branch.** `drawText` looked
  up `hiResFontMap_`; `drawTextRotated90CW` did not, so rotated labels (the
  reader side-button hints, keyboard chevrons) blitted pixel-doubled 1x glyphs
  next to crisp 2x text (fixed 2026-08-04; `test/system_font`
  `RotatedHiRes.RotatedTextBlitsTheCompanionFace` covers it). When adding any
  new glyph-blitting path, wire the companion lookup or it will ship mixed-res.

Both are covered by `test/system_font/`, built at `CROSSPOINT_RENDER_SCALE=2` —
at scale 1 the hi-res path compiles out and those cases assert nothing.

**Commercial faces are gated on the LARGEST size the block includes.** The
editor's PragmataPro and Nitti Typewriter headers are gitignored and built
locally from `lib/EpdFont/local_fonts/`, so `main.cpp` guards them with
`#if __has_include(<builtinFonts/pragmatapro_14_regular.h>)`. Gating on the
SMALLEST asserts that a tree holding the 12 pt headers holds the 14 pt ones
too, which is false for any tree generated before 14 pt existed — and the
failure is inverted from the usual one: a clone with no licensed TTFs builds
fine, while the machines that HAVE the fonts fail on a missing header. Always
test the strictest requirement, so a stale tree degrades down the no-TTF path
`editorfonts::resolve()` already handles. Re-run
`lib/EpdFont/scripts/convert-builtin-fonts.sh` to restore the face.

**Total**: ~80+ global `EpdFont` and `EpdFontFamily` objects

**Compilation Flag**:
```cpp
#ifndef OMIT_FONTS
  // Most fonts loaded here
#endif
```

**Implications**:
- Fonts stored in **Flash** (marked as `static const` in `lib/EpdFont/builtinFonts/`)
- Font rendering data cached in **DRAM** when first used
- `OMIT_FONTS` can reduce binary size for minimal builds
- Font IDs defined in [src/fontIds.h](../src/fontIds.h)

**Usage**:
```cpp
#include "fontIds.h"

renderer.insertFont(FONT_UI_MEDIUM, ui12FontFamily);
renderer.drawText(FONT_UI_MEDIUM, x, y, "Hello", true);
```

---

## Testing and Debugging

### Build Commands

**Via CLI**:
```bash
# Build firmware (default environment)
pio run

# Build and upload to device
pio run -t upload

# Build specific environment
pio run -e gh_release

# Clean build artifacts
pio run -t clean

# Upload filesystem data (if using SPIFFS/LittleFS)
pio run -t uploadfs
```

**Via VS Code**:
* Use PlatformIO toolbar: Build (✓), Upload (→), Clean (🗑️)
* Or Command Palette: `PlatformIO: Build`, `PlatformIO: Upload`, etc.

### Monitoring and Debugging

```bash
# Enhanced monitor with color/logging (recommended)
python3 scripts/debugging_monitor.py

# Standard PlatformIO monitor
pio device monitor

# Combined upload + monitor
pio run -t upload && pio device monitor
```

**Via VS Code**: Click Monitor (🔌) button in PlatformIO toolbar

### Code Quality

```bash
# Static analysis (cppcheck)
pio check

# Format code (clang-format) - Windows Git Bash
find src -name "*.cpp" -o -name "*.h" | xargs clang-format -i

# Format code (clang-format) - Linux
clang-format -i src/**/*.cpp src/**/*.h
```

### Debugging Crashes

**Common Crash Causes**:

1. **Out of Memory** (Most common):
   ```cpp
   LOG_DBG("MEM", "Free heap: %d bytes", ESP.getFreeHeap());
   ```
   - Monitor heap usage throughout activity lifecycle
   - Check if large allocations (>10KB) occur before crash
   - Verify buffers are freed in `onExit()`

2. **Stack Overflow**:
   ```cpp
   LOG_DBG("TASK", "Stack high water: %d", uxTaskGetStackHighWaterMark(taskHandle));
   ```
   - Occurs during deep recursion or large local variables
   - Increase task stack size in `xTaskCreate()` (2048 → 4096)
   - Move large buffers to heap with malloc

3. **Use-After-Free**:
   - Activity deleted but task still running
   - Always `vTaskDelete()` in `onExit()` BEFORE activity destruction
   - Set pointers to `nullptr` after `free()`

4. **Corrupt Cache Files**:
   - Delete `.crosspoint/` directory on SD card
   - Forces clean re-parse of all EPUBs
   - Check file format versions in [docs/file-formats.md](../docs/file-formats.md)

5. **Watchdog Timeout**:
   - Loop/task blocked for >5 seconds
   - Add `vTaskDelay(1)` in tight loops
   - Check for blocking I/O operations

**Verification Steps**:
1. Check serial output for stack traces
2. Monitor heap with `ESP.getFreeHeap()` before/after operations
3. Verify task deletion with task list (`vTaskList()`)
4. Test with `LOG_LEVEL=2` (debug logging enabled)

---

## Git Workflow and Repository Awareness

### Fork sync: never blind-merge upstream

Read [docs/fork-sync.md](docs/fork-sync.md) before any upstream merge, and run
`scripts/repo-status.sh` to see where things stand (it reports; it never
merges).

**Branch names are asymmetric: this fork's is `main`, upstream's is `develop`**
(and upstream also has a `master`). Renamed 2026-08-04, owner convention. So
`upstream/develop` throughout these docs is deliberate, not a stale name — there
is no `upstream/main` to "correct" it to. `git push origin main` is the push.

This fork is **selectively divergent**, not a tracking mirror — 255 ahead / 13
behind as of 2026-08-07 — and it has DELETED whole subsystems upstream still
develops: KOReader sync, Calibre and the status bar (`08d5bdee`), bookmarks
and auto page turn (`e0509aef`), the reader menu (`9494d88e`), the tabbed Text
Settings editor (`9fdd7dfe`), and **four of the five UI themes plus the setting
that chose between them** (2026-08-04 — Classic, Lyra, Lyra Extended and
RoundedRaff; `src/components/themes/roundedraff/` is gone outright, and
`CrossPointSettings::UI_THEME`, the `uiTheme` field and the six `STR_THEME_*`
strings with it). Anything upstream touching a theme is `N/A` unless it lands
in `BaseTheme`, `LyraTheme`, `Lyra3CoversTheme` or `LyraSixTheme`, which are
still live as the layers Lyra Six is composed from.
`git merge upstream/develop` therefore resurrects
them: attempted 2026-08-03, it produced 18 conflicts, six of them
`modify/delete`. Take upstream changes per commit, live hunks only.

`scripts/repo-status.sh` classifies each unmerged upstream commit `N/A` (touches
only removed subsystems — skip) or `REVIEW` (straddles live code — read it).

**Other clones are not working copies.** `~/src/crosspoint` and
`~/src/xteink/crosspoint-reader` point `origin` straight at *upstream*, so a
push from either targets the upstream project, not the fork. Check any clone
for uncommitted work before consolidating it — on 2026-08-03 the xteink clone
held 451 lines of ring-clock work on no branch and no remote, now rescued as
`rescue/ring-clock` on the fork.

### Repository Detection Protocol

**CRITICAL**: ALWAYS verify repository context before git operations. This could be:
- A **fork** with `origin` pointing to personal repo, `upstream` to main repo
- A **direct clone** with `origin` pointing to main repo
- Multiple collaborator remotes

**Verification Commands** (run at session start):
```bash
# Check current branch
git branch --show-current

# Check all remotes
git remote -v

# Identify main branch name (could be 'main' or 'master')
git symbolic-ref refs/remotes/origin/HEAD 2>/dev/null | sed 's@^refs/remotes/origin/@@'

# Check working tree status
git status --short
```

**Example Output** (forked repository):
```text
origin      https://github.com/<your-username>/crosspoint-reader.git (fetch/push)
upstream    https://github.com/crosspoint-reader/crosspoint-reader.git (fetch/push)
```

### Git Operation Rules

1. **Never assume branch names**:
   ```bash
   # Bad: git push origin main
   # Good: git push origin $(git branch --show-current)
   ```

2. **Never assume remote names or write permissions**:
   - **Forked repos**: Push to `origin` (your fork), submit PR to `upstream`
   - **Direct contributors**: May push feature branches to `upstream`
   - **Always ask**: "Should I push to origin or create a PR?"

3. **Check for upstream changes before starting work**:
   ```bash
   # Sync fork with upstream (if applicable)
   git fetch upstream
   git merge upstream/main  # or upstream/master
   ```

4. **Use explicit remote and branch names**:
   ```bash
   # Check remotes first
   git remote -v

   # Use explicit syntax
   git push <remote> <branch>
   ```

### Branch Naming Convention

**For feature/fix branches**:
```text
feature/<short-description>       # New features
fix/<issue-number>-<description>  # Bug fixes
refactor/<component-name>         # Code refactoring
docs/<topic>                      # Documentation updates
```

**Examples**:
- `feature/sd-download-progress`
- `fix/123-orientation-crash`
- `refactor/hal-storage`

### Commit Message Format

**Pattern**:
```text
<type>: <short summary (50 chars max)>

<optional detailed description>

```

**Types**: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `perf`

**Example**:
```text
feat: add real-time SD download progress bar

Implements progress tracking for book downloads using
UITheme progress bar component with heap-safe updates.

Tested with 5MB+ files.
```

### When to Commit

**DO commit when**:
- User explicitly requests: "commit these changes"
- Feature is complete and tested on device
- Bug fix is verified working
- Refactoring preserves all functionality
- All tests pass (`pio run` succeeds)

**DO NOT commit when**:
- Changes are untested on actual hardware
- Build fails or has warnings
- Experimenting or debugging in progress
- User hasn't explicitly requested commit
- Files excluded by `.gitignore` would be included — always run `git status` and cross-check against `.gitignore` before staging (e.g., `*.generated.h`, `.pio/`, `compile_commands.json`, `platformio.local.ini`)

**Rule**: **If uncertain, ASK before committing.**

---

## Generated Files and Build Artifacts

### Files Generated by Build Scripts

**NEVER manually edit these files** - they are regenerated automatically:

1. **HTML Headers** (generated by `scripts/build_html.py`):
   - `src/network/html/*.generated.h`
   - **Source**: the `.html` file sitting NEXT TO each generated header, e.g.
     `src/network/html/FilesPage.html` -> `FilesPageHtml.generated.h`.
     `scripts/build_html.py` walks `src/` (`SRC_DIR = "src"`, `:5`, `:95`) —
     there is no `data/html/`, and there never has been in this fork.
   - **Triggered**: During PlatformIO `pre:` build step
   - **To modify**: Edit the sibling `.html`, not the generated header

2. **I18n Headers** (generated by `scripts/gen_i18n.py`):
   - `lib/I18n/I18nKeys.h`, `lib/I18n/I18nStrings.h`, `lib/I18n/I18nStrings.cpp`
   - **Source**: YAML translation files in `lib/I18n/translations/` (one per language)
   - **To modify**: Edit source YAML files, then run `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/`
   - **Commit**: Source YAML files only. All three generated files (`I18nKeys.h`, `I18nStrings.h`, `I18nStrings.cpp`) are in `.gitignore` and regenerated at build time.

3. **Build Artifacts** (in `.gitignore`):
   - `.pio/` - PlatformIO build output
   - `build/` - Compiled binaries
   - `*.generated.h` - Any auto-generated headers
   - `compile_commands.json` - LSP/IDE metadata

### Modifying Generated Content Workflow

**To change HTML pages**:
1. Edit source: `src/network/html/<pagename>.html`
2. Build: `pio run` (auto-triggers `scripts/build_html.py`)
3. Generated headers update: `src/network/html/<pagename>Html.generated.h`
4. **Commit ONLY** source HTML, NOT generated `.generated.h` files

**To add/modify translations (i18n)**:
1. Edit or add YAML file: `lib/I18n/translations/<language>.yaml`
   - Each file must contain: `_language_name`, `_language_code`, `_order`, and `STR_*` keys
   - English (`english.yaml`) is the reference; missing keys in other languages fall back to English
2. Run generator: `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/`
3. Generated files update: `I18nKeys.h`, `I18nStrings.h`, `I18nStrings.cpp`
4. **Commit** source YAML files only. All three generated files are in `.gitignore` and regenerated at build time.

**To use translated strings in code**:
```cpp
#include <I18n.h>
// Use tr() macro with StrId enum (defined in generated I18nKeys.h)
renderer.drawText(FONT_UI, x, y, tr(STR_LOADING), true);
```

**To add custom fonts**:
1. Place source fonts in `lib/EpdFont/fontsrc/` (gitignored)
2. Run conversion script (see `lib/EpdFont/README`)
3. Update global font objects in `src/main.cpp:40-115`
4. Add font ID constant to `src/fontIds.h`

---

## Local Development Configuration

### platformio.local.ini (Personal Overrides)

**Purpose**: Personal development settings that should NEVER be committed.

**Use Cases**:
- Serial port configuration (varies by machine)
- Debug flags for specific testing
- Local build optimizations
- Developer-specific paths

**Example** `platformio.local.ini`:
```ini
# platformio.local.ini (gitignored)
[env:default]
upload_port = COM7              # Windows: COMx, Linux: /dev/ttyUSBx
monitor_port = COM7

build_flags =
  ${base.build_flags}
  -DMY_DEBUG_FLAG=1             # Personal debug flags
  -DTEST_FEATURE_ENABLED=1
```

**Configuration Hierarchy**:
1. `platformio.ini` - **Committed**, shared project settings
2. `platformio.local.ini` - **Gitignored**, personal overrides
3. Local file extends/overrides base config

**Rules**:
- **NEVER commit** `platformio.local.ini`
- **NEVER put** personal info (serial ports, credentials) in main `platformio.ini`
- Use `${base.build_flags}` to extend (not replace) base flags

---

## Testing and Verification Workflow

### Testing Checklist

**AI agent scope** (what you CAN verify):
1. ✅ **Build**: `pio run -t clean && pio run` (0 errors/warnings)
2. ✅ **Quality**: `pio check` + `find src -name "*.cpp" -o -name "*.h" | xargs clang-format -i`
3. ✅ **Format**: Commit messages (`feat:`/`fix:`), no `.gitignore`-excluded files staged (e.g., `*.generated.h`, `.pio/`, `platformio.local.ini`)
4. ✅ **CI**: Fix GitHub Actions failures before review
5. ✅ **Code review**: Inspect switch/case coverage for the states a change can reach

**Human tester scope** (flag these for the user):
6. 🔲 **Device**: Test on hardware
7. 🔲 **Heap**: `ESP.getFreeHeap()` > 50KB, no leaks
8. 🔲 **Cache**: If EPUB modified, delete `.crosspoint/` and verify re-parse

### CI/CD Pipeline Awareness

**GitHub Actions** run automatically on pull requests:

| Workflow | File | Purpose |
|----------|------|---------|
| Build Check | `.github/workflows/ci.yml` | Verifies code compiles |
| Format Check | `.github/workflows/pr-formatting-check.yml` | Validates clang-format |
| Release Build | `.github/workflows/release.yml` | Production releases |
| RC Build | `.github/workflows/release_candidate.yml` | Release candidates |

**Rules**:
- **Fix CI failures BEFORE** requesting review
- CI runs on: Push to PR, PR updates
- Format check fails → Run clang-format locally
- Build check fails → Fix compile errors

---

## Serial Monitoring and Live Debugging

### Serial Monitor Options

1. **Enhanced**: `python3 scripts/debugging_monitor.py` (color-coded, recommended)
2. **Standard**: `pio device monitor` (basic, no colors)
3. **VS Code**: Monitor (🔌) button (IDE-integrated)

### Live Debugging Patterns

**Heap**: `LOG_DBG("MEM", "Free: %d", ESP.getFreeHeap());` (every 5s in loop)
**Stack**: `uxTaskGetStackHighWaterMark(nullptr)` (< 512 bytes → increase stack)
**Flush**: `logSerial.flush();` (force output before crash)

**Port Detection**: Windows: `mode` | Linux: `ls /dev/ttyUSB* /dev/ttyACM*` or `dmesg | grep tty`

---

## Cache Management and Invalidation

### Cache Structure on SD Card

**Location**: `.crosspoint/` directory on SD card root

**Structure**: `.crosspoint/epub_<hash>/{book.bin, progress.bin, cover.bmp, sections/*.bin}`

**Hash**: `std::hash<std::string>{}(filepath)` → Moving/renaming a file OUTSIDE the firmware (PC, web upload) = new hash = lost progress. Rename/move via the on-device **Manage Files** screen migrates the cache dir to the new hash (`FsOps::migrateBookRefs`, recursive for folders), so progress survives; if that cache-dir rename fails the old cache is deleted, never orphaned. Ruling + mechanism: [docs/manage-files.md](docs/manage-files.md). The read-folder move (`moveFinishedBookToReadFolder`) migrates the same way.

### Cache Invalidation Rules

**Cache is automatically invalidated when**:
1. **File format version changes** (see `docs/file-formats.md`)
   - `book.bin` version number incremented
   - `section.bin` version number incremented
2. **Render settings change**:
   - Font family or size (`SETTINGS.fontFamily`, `SETTINGS.fontSize`)
   - Line spacing (`SETTINGS.lineSpacing`)
   - Paragraph spacing (`SETTINGS.extraParagraphSpacing`)
   - Screen margins (`SETTINGS.screenMargin`)
3. **Viewport dimensions change**:
   - Display resolution change
4. **Book file modified**:
   - Moved, renamed (outside Manage Files / read-folder move — those migrate), or content changed (new hash)

**Manual Cache Clear** (safe operations):
```bash
# Delete ALL caches (forces full regeneration)
rm -rf /path/to/sd/.crosspoint/

# Delete specific book cache
rm -rf /path/to/sd/.crosspoint/epub_<hash>/

# Keep progress, delete only rendered sections
rm -rf /path/to/sd/.crosspoint/epub_<hash>/sections/
```

**When to Clear Cache**:
- EPUB parsing errors after code changes to `lib/Epub/`
- Corrupt rendering (missing text, wrong layout)
- Testing cache generation logic
- After modifying:
  - `lib/Epub/Epub/Section.cpp`
  - `lib/Epub/Epub/BookMetadataCache.cpp`
  - Render settings in `CrossPointSettings`

### Cache File Format Versioning

**Source**: `lib/Epub/Epub/Section.cpp`, `lib/Epub/Epub/BookMetadataCache.cpp`

**Current Versions** (as of docs/file-formats.md):
- `book.bin`: **Version 10** (metadata structure)
- `section.bin`: **Version 35** (layout structure; v35 added the per-page
  word-anchor LUT used for exact reposition after font/size changes, and made
  h1-h3 headings force a page break so a page-top heading stays pinned across
  reflows)
- `progress.bin`: **no version byte** — its LENGTH discriminates. 4 bytes =
  spine+page, 6 adds the chapter page count, 8 adds the paragraph index the
  reader was on, 12 adds the page's word anchor (uint32 LE source byte
  position). Shorter files still load and degrade gracefully.

**Version Increment Rules**:
1. **ALWAYS increment version** BEFORE changing binary structure
2. Version mismatch → Cache auto-invalidated and regenerated
3. Document format changes in `docs/file-formats.md`

**Example** (incrementing section format version):
```cpp
// lib/Epub/Epub/Section.cpp
static constexpr uint8_t SECTION_FILE_VERSION = 26;  // Was 25, now 26

// Add new field to structure
struct PageLine {
  // ... existing fields ...
  uint16_t newField;  // New field added
};
```

---

Philosophy: We are building a dedicated e-reader, not a Swiss Army knife. If a feature adds RAM pressure without significantly improving the reading experience, it is Out of Scope.
