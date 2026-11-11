#pragma once

class Activity;  // forward declaration

// RAII helper to lock rendering mutex for the duration of a scope.
class RenderLock {
  bool isLocked = false;

 public:
  // Try: take the lock only if it is free right now. For the main loop's idle
  // work, which must never wait out a render (a blocked loop stops sampling
  // presses); check ownsLock() before touching anything the lock guards. A
  // peek() followed by a blocking take was the old shape, and a render could
  // win the lock in between (fork survey #11, upstream 10cb2c3b29).
  enum class Mode { Blocking, Try };
  explicit RenderLock(Mode mode = Mode::Blocking);
  explicit RenderLock(Activity&);  // unused for now, but keep for compatibility
  bool ownsLock() const { return isLocked; }
  RenderLock(const RenderLock&) = delete;
  RenderLock& operator=(const RenderLock&) = delete;
  ~RenderLock();
  void unlock();
  static bool peek();
};
