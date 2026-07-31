# Testing and Debugging

CrossPoint runs on real hardware, so debugging usually combines local build checks and on-device logs.

## Local checks

Make sure `clang-format` 21+ is installed and available in `PATH` before running the formatting step.
If needed, see [Getting Started](./getting-started.md).

```sh
./bin/clang-format-fix
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
pio run
```

## Stack frame budget

The ESP32-C3 stacks are small: the render task gets 8192 bytes
(`src/activities/ActivityManager.cpp:25`) and `loopTask` 16384
(`src/main.cpp:319`). A single oversized function can overflow either, and the
symptom on device is a panic with an *empty* reason rather than a useful trace.
`lib/EpdFont/SdCardFont.cpp:265-284` documents one that took a crash report plus
a manual measurement to find.

To check your change did not add one:

```sh
tools/stack_budget/stack_budget.py
```

It builds `-e default` with `-fstack-usage` into its own private build directory
(it will not touch `.pio/` or `.cache/`), then fails if any function in `src/` or
`lib/` has a frame over the threshold. Vendored code is exempt. Pre-existing
frames are listed with a reason in `tools/stack_budget/allowlist.txt`.

If it fails, move the buffer off the stack with `makeUniqueNoThrow<uint8_t[]>(n)`
from `lib/Memory/Memory.h` — see the Resource Protocol in `CLAUDE.md`. Run with
`--suggest` if the frame genuinely has to stay.

Note that this bounds each function's own frame, not the depth of a call chain.
For depth, still use `uxTaskGetStackHighWaterMark()` on device. Full details in
[tools/stack_budget/README.md](../../tools/stack_budget/README.md).

## Flash and monitor

Flash firmware:

```sh
pio run --target upload
```

Open serial monitor:

```sh
pio device monitor
```

Optional enhanced monitor:

```sh
python3 -m pip install pyserial colorama matplotlib
python3 scripts/debugging_monitor.py
```

## Useful bug report contents

- Firmware version and build environment
- Exact steps to reproduce
- Expected vs actual behavior
- Serial logs from boot through failure
- Whether issue reproduces after clearing `.crosspoint/` cache on SD card

## Common troubleshooting references

- [User Guide troubleshooting section](../../USER_GUIDE.md#7-troubleshooting-issues--escaping-bootloop)
- [Webserver troubleshooting](../troubleshooting.md)
