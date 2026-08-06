// Claude: ask a question over the BLE keyboard, get an answer, keep a
// timestamped transcript on the SD card.
//
// Its own home row and its own flow — the note editor is a text editor and
// knows nothing about the network, and this knows nothing about editing files.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "spike/TextBuffer.h"

class ClaudeChatActivity final : public Activity {
  static constexpr size_t BUF_SIZE = 2048;  // a prompt, not a document
  static constexpr uint32_t DEBOUNCE_MS = 350;
  static constexpr size_t FLUSH_CHARS = 24;

  enum class View : uint8_t { Prompt, Working, Answer };

  std::unique_ptr<char[]> storage;
  std::unique_ptr<textbuf::TextBuffer> buf;

  View view = View::Prompt;
  char phaseText[48] = {0};
  std::string answer;
  std::vector<std::string> answerLines;
  size_t answerTop = 0;  // pagination through a long answer

  int editorFontId = 0;
  int lineHeight = 1;
  int maxLines = 1;
  int maxWidth = 0;
  int contentTop = 0;

  bool dirty = false;
  size_t pendingChars = 0;
  uint32_t lastKeyMs = 0;

  void handleKey(int key);
  void send();
  void setPhase(const char* phase);
  void layoutAnswer();

 public:
  ClaudeChatActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ClaudeChat", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
};
