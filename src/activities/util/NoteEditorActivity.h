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
#include "notes/KeyboardPanel.h"
#include "notes/TextBuffer.h"

class NoteEditorActivity final : public Activity {
  static constexpr size_t BUF_SIZE = 8192;
  // How long typing settles before a redraw is Settings -> Typing Redraw Delay
  // (SETTINGS.getDisplayDebounceMs()); e-ink batches keystrokes rather than
  // refreshing per key. See docs/notes-and-claude.md for why.
  static constexpr size_t FLUSH_CHARS = 24;

  std::unique_ptr<char[]> storage;
  std::unique_ptr<textbuf::TextBuffer> buf;
  std::string path;
  // Non-empty when launched from FileManagerActivity: Back/Done navigates back
  // there (at that directory) instead of going home.
  std::string returnDir;

  // Soft-wrapped display lines, rebuilt only when the text changes.
  struct DisplayLine {
    uint16_t start;
    uint16_t end;
  };
  std::vector<DisplayLine> lines;
  size_t topLine = 0;  // first display line on screen — this is the pagination

  int editorFontId = 0;
  int lineHeight = 1;
  int maxLines = 1;
  int maxWidth = 0;
  int contentTop = 0;

  // The on-screen keyboard is the DEFAULT input; a BLE keyboard is optional and
  // only started when one is already bonded.
  notes::KeyboardPanel panel;
  int panelHeight = 0;
  // Top of the keyboard strip. Held rather than recomputed in render() so the
  // text area's bottom and the strip's top cannot drift apart.
  int panelTop = 0;
  int statusHeight = 0;  // reserved band between text and panel
  uint32_t sideHeldSince = 0;
  uint32_t pickHeldSince = 0;
  uint32_t colRepeatAt = 0;
  int pickSlot = -1;
  bool pickFired = false;
  bool sideHandled = false;

  // Caret mode: hold Confirm on the SPACE key and the four direction buttons
  // stop driving the key grid and drive the text cursor instead. Without it the
  // on-screen keyboard has NO way to move the caret at all -- cursorLeft() and
  // friends were reachable only from a paired BLE keyboard's arrow keys
  // (handleKey, hidkeymap::KEY_LEFT), so an owner with no keyboard could only
  // reposition by deleting back to the spot. Same shape as the full-screen
  // KeyboardEntryActivity's `cursorMode`, which has had this since long before.
  enum class CaretDir : uint8_t { Left, Right, Up, Down };
  bool caretMode = false;
  bool caretDirty = false;  // a step happened; the repaint is still debounced
  uint32_t caretRepeatAt = 0;
  uint32_t caretLastMs = 0;

  bool dirty = false;
  bool bufferFull = false;
  // The file was too big to load, so the buffer holds nothing that came from
  // it. Distinct from bufferFull, which also means "typing hit the cap" -- that
  // buffer holds real edits and MUST still be written.
  bool loadRefused = false;
  bool oomFailed = false;  // buffer could not be allocated; render says so
  size_t pendingChars = 0;
  uint32_t lastKeyMs = 0;
  bool savedOk = false;

  void relayout();
  void ensureCursorVisible();
  void pageUp();
  void pageDown();
  size_t lineOfCursor() const;
  void handleKey(int key);
  // slot is the daisy pick (0=top,1=middle,2=bottom); ignored by the grids.
  void handlePanelKey(int slot = 1, bool longPress = false);
  bool repeatCol(MappedInputManager::Button button, int delta);
  // Caret mode owns the whole tick while it is active; see CaretDir above.
  void loopCaretMode();
  bool repeatCaret(MappedInputManager::Button button, CaretDir dir);
  void moveCaret(CaretDir dir);
  void pollPairingGestures();
  void drawLine(const char* text, size_t len, int y, bool showCursorAt, size_t cursorCol);
  int advanceOf(const char* piece, EpdFontFamily::Style style) const;
  bool save();
  // Shared exit for Back and keyboard Done: saves, then routes to FileManager
  // (when launched from there) or goes home.
  void exitEditor();

 public:
  NoteEditorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath,
                     std::string returnDir = {})
      : Activity("NoteEditor", renderer, mappedInput), path(std::move(filePath)), returnDir(std::move(returnDir)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
};
