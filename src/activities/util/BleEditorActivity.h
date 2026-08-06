// SPIKE — throwaway. Types BLE-HID keystrokes onto the panel and saves to
// /notes-spike.txt. Deliberately skips tr(), settings rows and theme polish;
// the point is the heap/flash/latency numbers, not the UI.
#pragma once

#include <HalStorage.h>

#include <memory>

#include "activities/Activity.h"

class BleEditorActivity final : public Activity {
  static constexpr size_t BUF_SIZE = 8192;
  static constexpr size_t MAX_TRACKED_LINES = 64;
  // E-ink: batch keystrokes rather than refreshing per key.
  static constexpr uint32_t DEBOUNCE_MS = 350;
  static constexpr size_t FLUSH_CHARS = 24;

  std::unique_ptr<char[]> buf;
  size_t len = 0;

  // Ring of the most recent laid-out line spans (offsets into buf).
  uint16_t lineStart[MAX_TRACKED_LINES] = {0};
  uint16_t lineEnd[MAX_TRACKED_LINES] = {0};
  size_t lineTotal = 0;

  int lineHeight = 1;
  int maxLines = 1;
  int maxWidth = 0;
  int contentTop = 0;

  bool dirty = false;
  // Response of the last Claude exchange, shown read-only until the next
  // keystroke. Empty = normal typing view.
  std::string lastResponse;
  char phaseText[48] = {0};  // live phase line during an exchange
  size_t pendingChars = 0;
  uint32_t lastKeyMs = 0;
  uint32_t pendingKeyStampMs = 0;  // BLE-side stamp of the oldest unrendered key
  uint32_t renderKeyStampMs = 0;   // handed to render() so it can time itself
  uint32_t lastLatencyMs = 0;
  uint32_t heapMin = UINT32_MAX;

  void layout();
  void save();
  void sendToClaude();
  void setPhase(const char* phase);

 public:
  BleEditorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BleEditor", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
};
