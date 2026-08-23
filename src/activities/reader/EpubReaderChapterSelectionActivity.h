#pragma once
#include <Epub.h>

#include <memory>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class EpubReaderChapterSelectionActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  std::string epubPath;
  ButtonNavigator buttonNavigator;
  int currentSpineIndex = 0;
  int selectorIndex = 0;
  // Position through the whole book, 0..1 (Epub::calculateProgress at the
  // reader's current page). Drawn as a thin bar under the header.
  float bookProgress = 0.0f;

  // Number of items that fit on a page, derived from logical screen height.
  // This adapts automatically when switching between portrait and landscape.
  int getPageItems() const;

  // Total rows in the list: the TOC items, plus the book-notes row when this
  // book has notes.
  int getTotalItems() const;

  // 1 when this book carries notes, 0 otherwise. The notes row is row 0 of the
  // list, so every chapter index is shifted by it.
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
      : Activity("EpubReaderChapterSelection", renderer, mappedInput),
        epub(epub),
        epubPath(epubPath),
        currentSpineIndex(currentSpineIndex),
        bookProgress(bookProgress) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
