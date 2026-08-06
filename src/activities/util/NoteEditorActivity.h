// Note editor: type over a BLE keyboard, edit with a cursor, page through the
// text, see markdown rendered as you go, save to a .md file on the SD card.
//
// Claude lives in its own activity (ClaudeChatActivity) reached from its own
// home row — the editor is a text editor and knows nothing about the network.
#pragma once

#include <HalStorage.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "spike/TextBuffer.h"

class NoteEditorActivity final : public Activity {
  static constexpr size_t BUF_SIZE = 8192;
  // E-ink: batch keystrokes rather than refreshing per key.
  static constexpr uint32_t DEBOUNCE_MS = 350;
  static constexpr size_t FLUSH_CHARS = 24;

  std::unique_ptr<char[]> storage;
  std::unique_ptr<textbuf::TextBuffer> buf;
  std::string path;

  // Soft-wrapped display lines, rebuilt only when the text changes.
  struct DisplayLine {
    uint16_t start;
    uint16_t end;
  };
  std::vector<DisplayLine> lines;
  size_t topLine = 0;  // first display line on screen — this is the pagination

  int lineHeight = 1;
  int maxLines = 1;
  int maxWidth = 0;
  int contentTop = 0;

  bool dirty = false;
  bool bufferFull = false;
  size_t pendingChars = 0;
  uint32_t lastKeyMs = 0;
  bool savedOk = false;

  void relayout();
  void ensureCursorVisible();
  void pageUp();
  void pageDown();
  size_t lineOfCursor() const;
  void handleKey(int key);
  void drawLine(const char* text, size_t len, int y, bool showCursorAt, size_t cursorCol);
  int advanceOf(const char* piece, EpdFontFamily::Style style) const;
  bool save();

 public:
  NoteEditorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath)
      : Activity("NoteEditor", renderer, mappedInput), path(std::move(filePath)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
};
