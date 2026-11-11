#pragma once
#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

class EpubReaderChapterSelectionActivity final : public UiListActivity {
  std::shared_ptr<Epub> epub;
  std::string epubPath;
  int currentSpineIndex = 0;
  // Position through the whole book, 0..1 (Epub::calculateProgress at the
  // reader's current page). Drawn as a thin bar under the header.
  float bookProgress = 0.0f;

  // Total rows in the list: the Find row, the book-notes row when this book has
  // notes, then the TOC items.
  int listCount() const override { return epub ? epub->getTocItemsCount() + headerRowCount() : 0; }
  // Index = list row: the Find row, the notes row when this book has notes,
  // then the TOC items (read from the card only for the rows on screen).
  void describeRow(int index, ListRow& row) const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Back cancels with a result and Confirm activates on RELEASE here, and a
  // missing epub swallows everything past Back.
  bool handleButtons() override;
  // Header plus the book-position bar under it, inside the safe area.
  void drawChrome() override;
  // Top of the list band: below the header and the book-position bar.
  int listTop() const;

  // Rows above the first chapter: the Find row, then the notes row if any.
  int headerRowCount() const { return FIND_ROW_COUNT + noteRowCount; }

  // Whole-book Find (docs/find-in-book-options-2026-09-28.md section 9). Always
  // row 0: the owner's ruling is that there is no reader menu, so Find lives
  // here, one press from the page, and puts nothing over the page itself. It
  // previews nothing on the progress bar, like the notes row.
  static constexpr int FIND_ROW = 0;
  static constexpr int FIND_ROW_COUNT = 1;
  // Opens the text entry prefilled with the last query; a committed query
  // becomes this screen's result (FindQueryResult) and the reader searches.
  void openFindPrompt();

  // 1 when this book carries notes, 0 otherwise. The notes row sits right under
  // the Find row, so every chapter index is shifted by headerRowCount().
  //
  // A ROW rather than a banner band: the owner's constraint was that the notice
  // must not push the chapter list off screen, and the chapter list is what the
  // screen is for. One row costs one chapter of visible list, is reachable with
  // the four buttons and by touch with no new gesture, and disappears entirely
  // when a book has nothing to say -- which is what "show nothing at all rather
  // than an empty heading" asks for. The verbose text lives one press away, in
  // BookNotesActivity.
  //
  // Latched in onEnter, not recomputed per call: the set cannot change while
  // this screen is up, and loop() and render() must agree about the shift or
  // the highlight selects a different chapter from the one it draws.
  int noteRowCount = 0;

 public:
  explicit EpubReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                              const std::shared_ptr<Epub>& epub, const std::string& epubPath,
                                              const int currentSpineIndex, const float bookProgress = 0.0f)
      : UiListActivity("EpubReaderChapterSelection", renderer, mappedInput),
        epub(epub),
        epubPath(epubPath),
        currentSpineIndex(currentSpineIndex),
        bookProgress(bookProgress) {}
  void onEnter() override;
};
