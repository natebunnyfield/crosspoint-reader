// Host task shim. See stubs/freertos/FreeRTOS.h for scope.
#pragma once

#include <cstdint>

#include "FreeRTOS.h"

struct HostTask {
  const char* name;
};
typedef HostTask* TaskHandle_t;

// One "task": the process itself. The suite never creates the render task, so
// ActivityManager::renderTaskHandle stays null and every notify path is inert.
inline TaskHandle_t xTaskGetCurrentTaskHandle() {
  static HostTask mainTask{"host-main"};
  return &mainTask;
}

inline void xTaskNotify(TaskHandle_t, uint32_t, int) {}
inline uint32_t ulTaskNotifyTake(int, uint32_t) { return 0; }
