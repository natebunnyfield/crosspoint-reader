// Host semaphore shim. See stubs/freertos/FreeRTOS.h for scope.
//
// A plain depth counter, not a mutex: the suite is single-threaded, so nothing
// can ever contend. It exists so RenderLock (defined in HostHarness.cpp,
// mirroring ActivityManager.cpp:311-342) has real state to flip, which is what
// makes RenderLock::peek() an honest answer to "is the render lock held right
// now?" — the property the changeFontSize / applySelectedFont tests assert.
#pragma once

#include "FreeRTOS.h"
#include "task.h"

struct HostMutex {
  int depth = 0;
  TaskHandle_t holder = nullptr;
};
typedef HostMutex* SemaphoreHandle_t;

inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new HostMutex(); }

inline bool xSemaphoreTake(SemaphoreHandle_t sem, uint32_t /*ticksToWait*/) {
  if (!sem) return true;
  sem->depth++;
  sem->holder = xTaskGetCurrentTaskHandle();
  return true;
}

inline bool xSemaphoreGive(SemaphoreHandle_t sem) {
  if (!sem) return true;
  if (sem->depth > 0) sem->depth--;
  if (sem->depth == 0) sem->holder = nullptr;
  return true;
}

inline TaskHandle_t xSemaphoreGetMutexHolder(SemaphoreHandle_t sem) { return sem ? sem->holder : nullptr; }

// xQueuePeek on a mutex: pdTRUE when the mutex is available (not taken).
inline int xQueuePeek(SemaphoreHandle_t sem, void*, uint32_t) {
  if (!sem) return pdTRUE;
  return sem->depth == 0 ? pdTRUE : pdFALSE;
}
