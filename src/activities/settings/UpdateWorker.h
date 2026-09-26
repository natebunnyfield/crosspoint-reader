#pragma once

#include <atomic>
#ifdef SIMULATOR
#include <thread>
#endif

// Runs ONE blocking updater step (the manifest check, one font family, one
// book, the end-of-run removals) for Update Fonts and Update Library, off the
// thread that has to keep presenting -- where there is such a thread.
//
// WHY IT SPLITS BY PLATFORM, which is the whole point of this file. The owner's
// report (2026-09-26, "not appearing frozen when i select Update Library and
// Update Fonts") has one mechanism on a host build and a different, milder one
// on the device, and they are answered differently:
//
//   HOST (the iOS app, the Mac apps): simulator_main runs loop() and THEN
//   display.presentIfNeeded() on the same main thread
//   (crosspoint-simulator/src/simulator_main.cpp:373, :419). The render task
//   draws and converts every frame the progress callbacks ask for, but nothing
//   reaches the glass until loop() returns -- and one family's sync was one
//   loop() call. Measured on simulator_x3 against a 1 MB/s link: 17.4 s and
//   18.7 s with not one present, and no input either, because the same thread
//   pumps SDL events (HalGPIO::update). On a phone that is an app that has
//   stopped. So on a host the step runs on a std::thread and loop() returns
//   every tick: frames present, Back is read, the heartbeat repaints.
//
//   DEVICE: the render task is a separate FreeRTOS task
//   (ActivityManager.cpp:34) and has always painted while the loop task
//   blocks, so a worker buys nothing that a second task stack would not cost.
//   The device's own freeze -- nothing to say while hashing, a repaint gated on
//   the whole run's percentage -- is fixed in the updaters and the screens, not
//   here. The step runs inline, exactly as before, and a second stack of 8-16 KB
//   is NOT allocated on a heap measured refusing 22 KB contiguous with Wi-Fi and
//   wolfSSL up (B-053). Running a worker task on the device too is an option
//   for the owner, recorded in docs/update-progress-2026-09-26.md with its cost.
//
// A step's function must leave every piece of ACTIVITY state alone; the loop
// consumes its result on the loop thread after done() (host) or straight after
// start() (device). Updater progress counters are atomics for the render task's
// sake and are the only thing both sides read while a step runs.
class UpdateWorker {
 public:
  using Fn = void (*)(void* ctx);

  ~UpdateWorker() { join(); }

  // Host: starts the step and returns at once. Device: runs it to completion.
  void start(Fn fn, void* ctx) {
    join();
    finished_.store(false);
#ifdef SIMULATOR
    running_ = true;
    thread_ = std::thread([this, fn, ctx]() {
      fn(ctx);
      finished_.store(true);
    });
#else
    running_ = true;
    fn(ctx);
    finished_.store(true);
#endif
  }

  // A step was started and its result has not been collected by join().
  bool inFlight() const { return running_; }
  // ...and it has finished, so join() will not block.
  bool done() const { return finished_.load(); }

  // Wait for the step (if any) and mark its result collected.
  void join() {
#ifdef SIMULATOR
    if (thread_.joinable()) thread_.join();
#endif
    running_ = false;
  }

 private:
  bool running_ = false;
  std::atomic<bool> finished_{false};
#ifdef SIMULATOR
  std::thread thread_;
#endif
};
