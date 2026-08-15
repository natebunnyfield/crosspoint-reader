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
#include "notes/KeyboardPanel.h"
#include "notes/TextBuffer.h"

class ClaudeChatActivity final : public Activity {
  static constexpr size_t BUF_SIZE = 2048;  // a prompt, not a document
  static constexpr size_t FLUSH_CHARS = 24;

  enum class View : uint8_t { Prompt, Working, Answer };

  std::unique_ptr<char[]> storage;
  std::unique_ptr<textbuf::TextBuffer> buf;

  View view = View::Prompt;
  char phaseText[48] = {0};
  std::string answer;
  std::vector<std::string> answerLines;
  std::vector<std::string> promptLines;  // wrapped in loop(), never in render()
  size_t answerTop = 0;                  // pagination through a long answer

  // On-screen keyboard is the default input here too; a BLE keyboard is
  // optional and only started when one is already bonded.
  notes::KeyboardPanel panel;
  int panelHeight = 0;
  int panelTop = 0;
  int statusHeight = 0;  // reserved band between text and panel
  uint32_t sideHeldSince = 0;
  uint32_t pickHeldSince = 0;
  uint32_t colRepeatAt = 0;
  int pickSlot = -1;
  bool pickFired = false;
  bool sideHandled = false;

  int editorFontId = 0;
  int lineHeight = 1;
  int maxLines = 1;
  int maxWidth = 0;
  int contentTop = 0;

  bool oomFailed = false;  // prompt buffer could not be allocated
  bool dirty = false;
  size_t pendingChars = 0;
  uint32_t lastKeyMs = 0;

  void handleKey(int key);
  // slot is the daisy pick (0=top,1=middle,2=bottom); ignored by the grids.
  void handlePanelKey(int slot = 1, bool longPress = false);
  bool repeatCol(MappedInputManager::Button button, int delta);
  void relayoutPrompt();
  void pollPairingGestures();
  void send();
  void setPhase(const char* phase);
  void layoutAnswer();
  int answerLinesOnScreen() const;
  // The answer's wrapped lines, and nothing else. Split out so the
  // anti-aliasing overlay re-renders exactly the text (src/TextAntiAliasing.h).
  void drawAnswerText() const;

 public:
  ClaudeChatActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ClaudeChat", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
};
