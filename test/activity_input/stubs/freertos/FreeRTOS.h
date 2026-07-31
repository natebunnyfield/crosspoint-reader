// Minimal host FreeRTOS shim — only what src/activities/ActivityManager.h needs
// to compile (it is included transitively by every Activity header).
//
// Deliberately far smaller than the simulator's shim: this suite is
// single-threaded, drives frames by hand and never starts the render task, so
// nothing here needs to block or schedule. Anything that would require real
// concurrency is left undeclared so a future test that needs it fails to
// compile rather than silently no-op.
#pragma once

#include <cstdint>

#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xFFFFFFFF
#define eIncrement 1
#define portTICK_PERIOD_MS 1

using BaseType_t = int;

// No cross-core state to guard in a single-threaded host process.
struct HostPortMux {
  int unused;
};
typedef HostPortMux portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED \
  { 0 }

inline void taskENTER_CRITICAL(portMUX_TYPE*) {}
inline void taskEXIT_CRITICAL(portMUX_TYPE*) {}
#define portENTER_CRITICAL(mux) taskENTER_CRITICAL(mux)
#define portEXIT_CRITICAL(mux) taskEXIT_CRITICAL(mux)
