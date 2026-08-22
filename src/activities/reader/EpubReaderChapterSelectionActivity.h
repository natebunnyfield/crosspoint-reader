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

  // Total TOC items count
  int getTotalItems() const;

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
