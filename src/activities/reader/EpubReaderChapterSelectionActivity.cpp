#include "EpubReaderChapterSelectionActivity.h"

#include <Epub/BookNotes.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "BookNotesActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// How far the chapter-preview tick extends past the progress bar's outline,
// above and below. Shared by loop() and render() so the touch-target math and
// the drawn layout cannot drift apart.
constexpr int kTickOverhang = 2;
}  // namespace

int EpubReaderChapterSelectionActivity::getTotalItems() const { return epub->getTocItemsCount() + noteRowCount; }

void EpubReaderChapterSelectionActivity::onEnter() {
  Activity::onEnter();

  if (!epub) {
    return;
  }

  // Latch the notes row before the selection is computed against it. Reading
  // the accumulated set is a mask test -- nothing is parsed or measured here,
  // which is the whole point of collecting these at parse time.
  noteRowCount = booknotes::current().any() ? 1 : 0;

  selectorIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (selectorIndex == -1) {
    selectorIndex = 0;
  }
  selectorIndex += noteRowCount;

  // Trigger first update
  requestUpdate();
}

void EpubReaderChapterSelectionActivity::onExit() { Activity::onExit(); }

void EpubReaderChapterSelectionActivity::loop() {
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);
  const int totalItems = getTotalItems();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  auto selectChapter = [this] {
    if (noteRowCount != 0 && selectorIndex == 0) {
      // The notes row. Push the verbose screen rather than leaving this one:
      // Back from there comes straight back to the chapter list, with the
      // highlight where it was.
      startActivityForResult(std::make_unique<BookNotesActivity>(renderer, mappedInput), [](const ActivityResult&) {});
      return;
    }
    const auto tocItem = epub->getTocItem(selectorIndex - noteRowCount);
    if (tocItem.spineIndex == -1) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
    } else {
      setResult(ChapterResult{tocItem.spineIndex, tocItem.anchor});
      finish();
    }
  };

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Keep in step with render(): the book-progress bar band (bar plus the tick
  // overhang above and below) sits between the header and the list, so the
  // list starts one band lower.
  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing +
                         metrics.popupProgressBarHeight * 2 + kTickOverhang * 2 + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;
  switch (handleListTouch(selectorIndex, totalItems, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      selectChapter();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    selectChapter();
  }

  buttonNavigator.onNextRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });

  // The SIDE pair pages by a whole screenful; the FRONT pair above steps one
  // row. They used to be the same action (docs/ui-conventions.md, "Side buttons
  // should page, not repeat the front buttons"). pageDown/pageUp clamp at the
  // ends and return false when nothing moved, so a short list costs no redraw.
  buttonNavigator.onPageNext([this, totalItems, pageItems] {
    if (!ButtonNavigator::pageDown(selectorIndex, totalItems, pageItems)) return;
    requestUpdate();
  });

  buttonNavigator.onPagePrevious([this, totalItems, pageItems] {
    if (!ButtonNavigator::pageUp(selectorIndex, totalItems, pageItems)) return;
    requestUpdate();
  });
}

void EpubReaderChapterSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_SELECT_CHAPTER));

  // Book-position bar (2026-08-22 redesign): twice the popup bar's height,
  // a 1 px ink outline with an EMPTY (paper) interior — no gray fill. The
  // already-read portion, left of the reader's current position, fills solid
  // ink. A vertical tick previews where the HIGHLIGHTED chapter begins, so
  // moving the selection shows the jump target's location before committing.
  // The tick notches through the outline (kTickOverhang above and below the
  // bar) and its interior run flips to paper where it crosses the solid fill,
  // so it reads in both zones on a 1bpp panel. "Ink"/"paper" are the
  // renderer's states — dark mode inverts them for free, no special-casing.
  const int barHeight = metrics.popupProgressBarHeight * 2;
  const int barTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + kTickOverhang;
  const int barLeft = screen.x + metrics.contentSidePadding;
  const int barWidth = screen.width - metrics.contentSidePadding * 2;
  renderer.drawRect(barLeft, barTop, barWidth, barHeight);
  const float clamped = bookProgress < 0.0f ? 0.0f : (bookProgress > 1.0f ? 1.0f : bookProgress);
  const int fillWidth = static_cast<int>((barWidth - 2) * clamped);
  if (fillWidth > 0) {
    renderer.fillRect(barLeft + 1, barTop + 1, fillWidth, barHeight - 2);
  }

  // The highlighted chapter's start, as the same byte-weighted fraction the
  // fill uses: Epub::calculateProgress(spineIndex, 0.0f). Spine granularity —
  // an anchored TOC entry shares its spine's start fraction, the best figure
  // available here without paginating every chapter.
  // The notes row previews nothing -- it is not a place in the book -- so the
  // tick stays where the reader is.
  const auto selectedItem = (noteRowCount != 0 && selectorIndex == 0) ? BookMetadataCache::TocEntry{}
                                                                      : epub->getTocItem(selectorIndex - noteRowCount);
  if (selectedItem.spineIndex != -1) {
    float chapterFrac = epub->calculateProgress(selectedItem.spineIndex, 0.0f);
    chapterFrac = chapterFrac < 0.0f ? 0.0f : (chapterFrac > 1.0f ? 1.0f : chapterFrac);
    const int kTickWidth = 2;
    const int tickX = barLeft + 1 + static_cast<int>((barWidth - 2 - kTickWidth) * chapterFrac + 0.5f);
    renderer.fillRect(tickX, barTop - kTickOverhang, kTickWidth, barHeight + kTickOverhang * 2);
    // Where the tick crosses the solid fill, carve its interior run to paper
    // so it stays visible against the ink.
    const int fillRight = barLeft + 1 + fillWidth;
    const int carveWidth = (tickX + kTickWidth < fillRight ? tickX + kTickWidth : fillRight) - tickX;
    if (fillWidth > 0 && carveWidth > 0) {
      renderer.fillRect(tickX, barTop + 1, carveWidth, barHeight - 2, false);
    }
  }

  const int contentTop = barTop + barHeight + kTickOverhang + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;

  const int totalItems = getTotalItems();
  GUI.drawList(renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, totalItems, selectorIndex,
               [this](int index) {
                 if (noteRowCount != 0 && index == 0) {
                   char row[64];
                   snprintf(row, sizeof(row), "%s (%u)", tr(STR_BOOK_NOTES),
                            static_cast<unsigned>(booknotes::current().count()));
                   return std::string(row);
                 }
                 auto item = epub->getTocItem(index - noteRowCount);
                 std::string indent((item.level - 1) * 2, ' ');
                 return indent + item.title;
               });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
