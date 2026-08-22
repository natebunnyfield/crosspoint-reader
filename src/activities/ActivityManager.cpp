#include "ActivityManager.h"

#include <FontCacheManager.h>
#include <HalPowerManager.h>

#include <algorithm>

#include "boot_sleep/BootActivity.h"
#include "boot_sleep/SleepActivity.h"
#include "home/CrashActivity.h"
#include "home/FileBrowserActivity.h"
#include "home/FileManagerActivity.h"
#include "home/HomeActivity.h"
#include "home/RecentBooksActivity.h"
#include "network/CrossPointWebServerActivity.h"
#include "reader/ReaderActivity.h"
#include "settings/LibraryUpdateActivity.h"
#include "settings/SettingsActivity.h"
#ifndef CROSSPOINT_NO_DEVICE_FLASH
#include "settings/OnlineFirmwareUpdateActivity.h"
#endif
#include "util/ClaudeChatActivity.h"
#include "util/FullScreenMessageActivity.h"
#include "util/NoteEditorActivity.h"

static portMUX_TYPE activityManagerSpinlock = portMUX_INITIALIZER_UNLOCKED;

void ActivityManager::begin() {
#if defined(configNUM_CORES) && configNUM_CORES > 1
  constexpr BaseType_t renderTaskCore = 1;
#else
  constexpr BaseType_t renderTaskCore = 0;
#endif
  xTaskCreatePinnedToCore(&renderTaskTrampoline, "ActivityManagerRender",
                          8192,               // Stack size
                          this,               // Parameters
                          1,                  // Priority
                          &renderTaskHandle,  // Task handle
                          renderTaskCore  // Keep long renders/cover decodes off CPU 0's idle watchdog when available
  );
  assert(renderTaskHandle != nullptr && "Failed to create render task");
}

void ActivityManager::renderTaskTrampoline(void* param) {
  auto* self = static_cast<ActivityManager*>(param);
  self->renderTaskLoop();
}

void ActivityManager::renderTaskLoop() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Acquire the lock before reading currentActivity to avoid a TOCTOU race
    // where the main task deletes the activity between the null-check and render().
    RenderLock lock;
    if (currentActivity) {
      HalPowerManager::Lock powerLock;  // Ensure we don't go into low-power mode while rendering
      currentActivity->render(std::move(lock));
    }
    // Notify any task blocked in requestUpdateAndWait() that the render is done.
    TaskHandle_t waiter = nullptr;
    taskENTER_CRITICAL(&activityManagerSpinlock);
    waiter = waitingTaskHandle;
    waitingTaskHandle = nullptr;
    taskEXIT_CRITICAL(&activityManagerSpinlock);
    if (waiter) {
      xTaskNotify(waiter, 1, eIncrement);
    }
  }
}

void ActivityManager::loop() {
  if (currentActivity) {
    if (!currentActivity->isHomeActivity() && mappedInput.wasHomeGesture()) {
      if (currentActivity->handleHomeGesture()) {
        return;
      }
      goHome();
      return;
    }

    // Note: do not hold a lock here, the loop() method must be responsible for acquire one if needed
    currentActivity->loop();
  }

  while (pendingAction != PendingAction::None) {
    if (pendingAction == PendingAction::Pop) {
      RenderLock lock;

      if (!currentActivity) {
        // Should never happen in practice
        LOG_ERR("ACT", "Pop set but currentActivity is null; ignoring pop request");
        pendingAction = PendingAction::None;
        continue;
      }

      ActivityResult pendingResult = std::move(currentActivity->result);

      // Destroy the current activity
      exitActivity(lock);
      pendingAction = PendingAction::None;

      if (stackActivities.empty()) {
        LOG_DBG("ACT", "No more activities on stack, going home");
        // No swallow here: goHome() lands in replaceActivity's immediate-launch
        // branch (currentActivity is already null), which swallows before
        // onEnter() — see the comment there (input-edge audit 2026-08-21,
        // finding 4).
        lock.unlock();  // goHome may acquire its own lock
        goHome();
        continue;  // Will launch goHome immediately

      } else {
        currentActivity = std::move(stackActivities.back());
        stackActivities.pop_back();
        LOG_DBG("ACT", "Popped from activity stack, new size = %zu", stackActivities.size());
        // Swallow any press/release edges that the outgoing activity consumed
        // so they do not leak into the incoming activity's first loop().
        mappedInput.swallowUntilIdle();
        // Handle result if necessary
        if (currentActivity->resultHandler) {
          LOG_DBG("ACT", "Handling result for popped activity");

          // Move it here to avoid the case where handler calling another startActivityForResult()
          auto handler = std::move(currentActivity->resultHandler);
          currentActivity->resultHandler = nullptr;
          lock.unlock();  // Handler may acquire its own lock
          handler(pendingResult);
        }

        // Request an update to ensure the popped activity gets re-rendered
        if (pendingAction == PendingAction::None) {
          requestUpdate();
        }

        // Handler may request another pending action, we will handle it in the next loop iteration
        continue;
      }

    } else if (pendingActivity) {
      // Current activity has requested a new activity to be launched
      RenderLock lock;

      if (pendingAction == PendingAction::Replace) {
        // Destroy the current activity
        exitActivity(lock);
        // Clear the stack
        while (!stackActivities.empty()) {
          stackActivities.back()->onExit();
          stackActivities.pop_back();
        }
      } else if (pendingAction == PendingAction::ReplaceCurrentOnly) {
        // Destroy current activity but leave the stack intact.
        // Used when ReaderActivity swaps in the concrete reader so a caller
        // on the stack (e.g. FileBrowserActivity) survives the swap.
        exitActivity(lock);
      } else if (pendingAction == PendingAction::Push) {
        // Move current activity to stack
        stackActivities.push_back(std::move(currentActivity));
        LOG_DBG("ACT", "Pushed to activity stack, new size = %zu", stackActivities.size());
      }
      pendingAction = PendingAction::None;
      currentActivity = std::move(pendingActivity);
      // Swallow edges so the new activity's first loop() does not see the
      // press/release that triggered this push or replace.
      mappedInput.swallowUntilIdle();

      lock.unlock();  // onEnter may acquire its own lock
      currentActivity->onEnter();

      // onEnter may request another pending action, we will handle it in the next loop iteration
      continue;
    }
  }

  if (requestedUpdate.exchange(false)) {
    // Using direct notification to signal the render task to update
    // Increment counter so multiple rapid calls won't be lost
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  }
}

void ActivityManager::exitActivity(const RenderLock& lock) {
  // Note: lock must be held by the caller
  if (currentActivity) {
    currentActivity->onExit();
    currentActivity.reset();
  }
}

void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& newActivity) {
  // Note: no lock here, this is usually called by loop() and we may run into deadlock
  if (currentActivity) {
    // Defer launch if we're currently in an activity, to avoid deleting the current activity
    // leading to the "delete this" problem
    pendingActivity = std::move(newActivity);
    pendingAction = PendingAction::Replace;
  } else {
    // No current activity, safe to launch immediately
    currentActivity = std::move(newActivity);
    // Swallow stale edges/holds HERE, not only at the deferred call sites:
    // every goTo* from null-current takes this branch (boot routing with a
    // button held, the recovery picker entered mid-UP-hold, pop-to-empty →
    // goHome), and patching individual callers let the class re-open
    // (input-edge audit 2026-08-21, finding 4).
    mappedInput.swallowUntilIdle();
    currentActivity->onEnter();
  }
}

void ActivityManager::replaceCurrentActivity(std::unique_ptr<Activity>&& newActivity) {
  // Like replaceActivity() but does NOT clear the stack, so activities below
  // (e.g. FileBrowserActivity that pushed a ReaderActivity) survive the swap.
  pendingActivity = std::move(newActivity);
  pendingAction = PendingAction::ReplaceCurrentOnly;
}

void ActivityManager::goToFileTransfer() {
  lastHomeMenuItem = HomeMenuItem::FILE_TRANSFER;
  replaceActivity(std::make_unique<CrossPointWebServerActivity>(renderer, mappedInput));
}

void ActivityManager::goToSettings() {
  lastHomeMenuItem = HomeMenuItem::SETTINGS_MENU;
  replaceActivity(std::make_unique<SettingsActivity>(renderer, mappedInput));
}

void ActivityManager::goToFileBrowser(std::string path) {
  lastHomeMenuItem = HomeMenuItem::FILE_BROWSER;
  replaceActivity(std::make_unique<FileBrowserActivity>(renderer, mappedInput, std::move(path)));
}

void ActivityManager::goToFileManager(std::string startPath) {
  lastHomeMenuItem = HomeMenuItem::MANAGE_FILES;
  replaceActivity(std::make_unique<FileManagerActivity>(renderer, mappedInput, std::move(startPath)));
}

void ActivityManager::goToRecentBooks() {
  lastHomeMenuItem = HomeMenuItem::RECENTS;
  replaceActivity(std::make_unique<RecentBooksActivity>(renderer, mappedInput));
}

void ActivityManager::goToReader(std::string path, const bool allowFastInitialRefresh) {
  replaceActivity(std::make_unique<ReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh));
}

void ActivityManager::goToSleep(bool fromTimeout) {
  replaceActivity(std::make_unique<SleepActivity>(renderer, mappedInput, fromTimeout));
  loop();  // Important: sleep screen must be rendered immediately, the caller will go to sleep right after this returns
}

void ActivityManager::goToBoot() { replaceActivity(std::make_unique<BootActivity>(renderer, mappedInput)); }

void ActivityManager::goToFullScreenMessage(std::string message, EpdFontFamily::Style style) {
  replaceActivity(std::make_unique<FullScreenMessageActivity>(renderer, mappedInput, std::move(message), style));
}

void ActivityManager::goHome(HomeMenuItem initialMenuItem) {
  // Land the selector back on the row the user left from.
  //
  // This used to match currentActivity->name against a hardcoded list of
  // activity-name strings. Any home row whose name was missing from that list
  // silently fell back to index 0 — Manage Files did exactly that until it was
  // added, and Create Note and Claude would have repeated it. lastHomeMenuItem
  // is set by the goTo* wrappers instead, so a new row inherits this and there
  // is no name list left to forget.
  if (initialMenuItem == HomeMenuItem::NONE) initialMenuItem = lastHomeMenuItem;
  replaceActivity(std::make_unique<HomeActivity>(renderer, mappedInput, initialMenuItem));
}
void ActivityManager::goToCrashReport() { replaceActivity(std::make_unique<CrashActivity>(renderer, mappedInput)); }

// REPLACE, not push. Pushing keeps HomeActivity alive on the stack with its
// recent-book cover buffers still allocated, which left ~94 KB free and a
// 73,716-byte largest block — and nimble_port_init(), which wants ~65 KB
// contiguous, HUNG at that level repeatedly (watchdog reboot, no panic). Opened
// straight from boot instead, the same call saw 134,972 free / 114,676 and
// returned 0. Replacing frees Home before BLE starts; Back still works because
// popActivity() on an empty stack goes Home.
void ActivityManager::goToNoteEditor(std::string path, std::string returnDir) {
  lastHomeMenuItem = HomeMenuItem::CREATE_NOTE;
  replaceActivity(std::make_unique<NoteEditorActivity>(renderer, mappedInput, std::move(path), std::move(returnDir)));
}

void ActivityManager::goToClaudeChat() {
  lastHomeMenuItem = HomeMenuItem::CLAUDE;
  replaceActivity(std::make_unique<ClaudeChatActivity>(renderer, mappedInput));
}

void ActivityManager::goToFirmwareUpdate() {
#ifndef CROSSPOINT_NO_DEVICE_FLASH
  lastHomeMenuItem = HomeMenuItem::UPDATE_FIRMWARE;
  replaceActivity(std::make_unique<OnlineFirmwareUpdateActivity>(renderer, mappedInput));
#else
  // The row is not offered on a build that cannot flash, so nothing should
  // reach this. Going home is a harmless answer if something does.
  goHome();
#endif
}

void ActivityManager::goToLibraryUpdate() {
  lastHomeMenuItem = HomeMenuItem::UPDATE_LIBRARY;
  replaceActivity(std::make_unique<LibraryUpdateActivity>(renderer, mappedInput));
}

void ActivityManager::pushActivity(std::unique_ptr<Activity>&& activity) {
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while pushActivity is not expected");
    pendingActivity.reset();
  }
  pendingActivity = std::move(activity);
  pendingAction = PendingAction::Push;
}

void ActivityManager::popActivity() {
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while popActivity is not expected");
    pendingActivity.reset();
  }
  pendingAction = PendingAction::Pop;
}

bool ActivityManager::preventAutoSleep() const { return currentActivity && currentActivity->preventAutoSleep(); }

bool ActivityManager::isReaderActivity() const {
  return std::any_of(stackActivities.begin(), stackActivities.end(),
                     [](const auto& activity) { return activity->isReaderActivity(); }) ||
         (currentActivity && currentActivity->isReaderActivity());
}

bool ActivityManager::handleForcedRefresh() { return currentActivity && currentActivity->handleForcedRefresh(); }

bool ActivityManager::skipLoopDelay() const { return currentActivity && currentActivity->skipLoopDelay(); }

void ActivityManager::requestUpdate(bool immediate) {
  if (immediate) {
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  } else {
    // Deferring the update until current loop is finished
    // This is to avoid multiple updates being requested in the same loop
    requestedUpdate = true;
  }
}
void ActivityManager::requestUpdateAndWait() {
  if (!renderTaskHandle) {
    return;
  }

  // Atomic section to perform checks
  taskENTER_CRITICAL(&activityManagerSpinlock);
  auto currTaskHandler = xTaskGetCurrentTaskHandle();
  auto mutexHolder = xSemaphoreGetMutexHolder(renderingMutex);
  bool isRenderTask = (currTaskHandler == renderTaskHandle);
  bool alreadyWaiting = (waitingTaskHandle != nullptr);
  bool holdingRenderLock = (mutexHolder == currTaskHandler);
  if (!alreadyWaiting && !isRenderTask && !holdingRenderLock) {
    waitingTaskHandle = currTaskHandler;
  }
  taskEXIT_CRITICAL(&activityManagerSpinlock);

  // Render task cannot call requestUpdateAndWait() or it will cause a deadlock
  assert(!isRenderTask && "Render task cannot call requestUpdateAndWait()");

  // There should never be the case where 2 tasks are waiting for a render at the same time
  assert(!alreadyWaiting && "Already waiting for a render to complete");

  // Cannot call while holding RenderLock or it will cause a deadlock
  assert(!holdingRenderLock && "Cannot call requestUpdateAndWait() while holding RenderLock");

  xTaskNotify(renderTaskHandle, 1, eIncrement);
  // Tell the power manager the loop is parked here: it cannot poll input until the
  // render finishes, so the BUSY-wait slice hook should not yield to it meanwhile.
  powerManager.noteRenderWaitBegin();
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  powerManager.noteRenderWaitEnd();
}

// RenderLock

RenderLock::RenderLock() {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
}

RenderLock::RenderLock([[maybe_unused]] Activity&) {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
}

RenderLock::~RenderLock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

void RenderLock::unlock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

/**
 *
 * Checks if renderingMutex is busy.
 *
 * @return true if renderingMutex is busy, otherwise false.
 *
 */
bool RenderLock::peek() { return xQueuePeek(activityManager.renderingMutex, NULL, 0) != pdTRUE; };
