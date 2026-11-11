#pragma once

#include <memory>
#include <string>

#include "activities/Activity.h"

// The base every reader derives from: upstream a757b9d10 "consolidate reader
// activities", taken 2026-11-08 under the owner ruling "Adopt, keep fork
// behavior" (docs/fork-sync.md). Upstream's shape -- create() picks the reader,
// loadBook() opens the book, onEnter/onExit/Back/forced refresh/the initial
// refresh countdown live here, and a shared loop and render drive page turns --
// with the fork's behavior kept where the two differ:
//
//   * End of book is the fork's minimal centered screen (Confirm, Back and
//     forward go Home; paging back returns into the book). Upstream's
//     EndOfBookOptions suggestions menu stays deleted (owner ruling 2026-08-08,
//     docs/ui-conventions.md).
//   * No orientation hook: the fork is portrait only.
//   * Chapter skip on a held page turn stays retired (docs/hold-gestures.md), so
//     there is no skipPages().
//   * The EPUB reader keeps its own loop() and render(): press-fired overlapped
//     turns, the side-button chords, Find, the deferred gray pass and the
//     prerender all live there, exactly as before the port.
class ReaderActivity : public Activity {
 protected:
  std::string bookPath;
  int pagesUntilFullRefresh = 0;
  // A forced (manual) refresh asked for while a page that refreshes its own way
  // is up (the EPUB reader's image pages) is kept here until that page can
  // issue its clean base pass.
  bool forcedRefreshPending = false;

  explicit ReaderActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                          std::string bookPath, bool allowFastInitialRefresh);

  // Opens the book. Called from onEnter() once the file is known to exist and
  // the SD fonts are loaded; false finishes the activity (back to whatever
  // opened it).
  virtual bool loadBook() = 0;
  virtual std::string getBookTitle() const = 0;
  virtual std::string getBookAuthor() const { return ""; }
  virtual std::string getBookThumbBmpPath() const { return ""; }

  // Format-specific input checked before Back and the page turn (XTC: Confirm
  // opens chapter selection). true consumes the tick.
  virtual bool handleFormatInput() { return false; }
  // One page in the given direction. true when something changed and wants a
  // render; each reader encodes its own edges (the first page, the last).
  virtual bool pageTurn(bool isForward) = 0;
  virtual bool isAtEndOfBook() const = 0;
  // Paging back from the end screen: put the reader on the last page.
  virtual void onReturnFromEndOfBook() {}

  virtual void renderBook(RenderLock&& lock) = 0;
  // The fork's end-of-book screen, one definition for every reader.
  void renderEndOfBook();

  bool handleBackNavigation();
  // The book needed an indexing popup: the reader must clean that frame, so no
  // fast first refresh.
  void disableFastInitialRefresh() { pagesUntilFullRefresh = 0; }

 public:
  ~ReaderActivity() override = default;

  // The reader for a book path (XTC, plain text / Markdown, else EPUB), not yet
  // entered. nullptr on allocation failure.
  static std::unique_ptr<ReaderActivity> create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                std::string path, bool allowFastInitialRefresh);
  // create(), plus the image viewer for .bmp/.png: what opening a file from
  // Home, the file browser or the boot resume needs.
  static std::unique_ptr<Activity> open(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path,
                                        bool allowFastInitialRefresh);
  static bool isTxtFile(const std::string& path);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

  bool isReaderActivity() const final { return true; }
  bool handleForcedRefresh() final;
};
