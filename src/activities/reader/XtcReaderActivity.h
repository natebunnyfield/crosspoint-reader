/**
 * XtcReaderActivity.h
 *
 * XTC ebook reader activity for CrossPoint Reader
 * Displays pre-rendered XTC pages on e-ink display
 */

#pragma once

#include <Xtc.h>

#include <string>
#include <utility>

#include "ReaderActivity.h"

class XtcReaderActivity final : public ReaderActivity {
  std::shared_ptr<Xtc> xtc;

  uint32_t currentPage = 0;

  void renderPage();
  // Opens chapter selection when the book has chapters (short-press Confirm); no-op otherwise
  void openChapterSelection();
  void saveProgress() const;
  void loadProgress();

 public:
  explicit XtcReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                             bool allowFastInitialRefresh)
      : ReaderActivity("XtcReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  void onExit() override;

 protected:
  bool loadBook() override;
  std::string getBookTitle() const override { return xtc ? xtc->getTitle() : ""; }
  std::string getBookAuthor() const override { return xtc ? xtc->getAuthor() : ""; }
  std::string getBookThumbBmpPath() const override { return xtc ? xtc->getThumbBmpPath() : ""; }
  // Short-press Confirm (or the touch menu gesture) opens chapter selection.
  bool handleFormatInput() override;
  bool pageTurn(bool isForward) override;
  bool isAtEndOfBook() const override { return xtc && currentPage >= xtc->getPageCount(); }
  void onReturnFromEndOfBook() override { currentPage = xtc->getPageCount() - 1; }
  void renderBook(RenderLock&& lock) override;
};
