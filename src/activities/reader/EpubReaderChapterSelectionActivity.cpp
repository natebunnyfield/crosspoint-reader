#include "EpubReaderChapterSelectionActivity.h"

#include <Epub/BookNotes.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "BookNotesActivity.h"
#include "CrossPointState.h"
#include "FindMatcher.h"
#include "MappedInputManager.h"
#include "activities/util/TextEntryFactory.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
// How far the chapter-preview tick extends past the progress bar's outline,
// above and below. Shared by loop() and render() so the touch-target math and
// the drawn layout cannot drift apart.
constexpr int kTickOverhang = 2;
}  // namespace

void EpubReaderChapterSelectionActivity::openFindPrompt() {
  // Prefilled with the last query, so "find the next one" is a single commit.
  // 128 BYTES is findtext::kMaxQueryCodepoints: no query that fits the field
  // can overflow the matcher's window.
  startActivityForResult(makeTextEntryActivity(renderer, mappedInput, tr(STR_FIND_PROMPT), APP_STATE.lastFindQuery,
                                               findtext::kMaxQueryCodepoints),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) return;  // Back from the prompt: stay on the list
                           const auto* entry = std::get_if<KeyboardResult>(&result.data);
                           if (!entry) return;
                           if (!findtext::hasSearchableText(entry->text))
                             return;  // nothing to search: stay on the list
                           setResult(FindQueryResult{entry->text});
                           finish();
                         });
}

void EpubReaderChapterSelectionActivity::onEnter() {
  UiListActivity::onEnter();

  if (!epub) {
    return;
  }

  // Latch the notes row before the selection is computed against it. Reading
  // the accumulated set is a mask test -- nothing is parsed or measured here,
  // which is the whole point of collecting these at parse time.
  noteRowCount = booknotes::current().any() ? 1 : 0;

  // Open on the current chapter, which may sit past the first page; the first
  // screen build pulls the viewport to it (ListNav follow-on-build).
  int selected = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (selected == -1) {
    selected = 0;
  }
  selected += headerRowCount();
  // A book with no table of contents has no chapter row to land on; highlight
  // Find rather than a row past the end of the list, where Confirm did nothing.
  if (selected >= listCount()) selected = FIND_ROW;
  nav.selected = selected;
}

void EpubReaderChapterSelectionActivity::describeRow(const int index, ListRow& row) const {
  if (index == FIND_ROW) {
    row.title = tr(STR_FIND_ROW);
    return;
  }
  if (noteRowCount != 0 && index == FIND_ROW_COUNT) {
    char label[64];
    snprintf(label, sizeof(label), "%s (%u)", tr(STR_BOOK_NOTES), static_cast<unsigned>(booknotes::current().count()));
    row.title = label;
    return;
  }
  const auto item = epub->getTocItem(index - headerRowCount());
  row.title = std::string(item.level > 0 ? (item.level - 1) * 2 : 0, ' ') + item.title;
}

void EpubReaderChapterSelectionActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) {
    return;
  }
  nav.selected = index;
  if (index == FIND_ROW) {
    openFindPrompt();
    return;
  }
  if (noteRowCount != 0 && index == FIND_ROW_COUNT) {
    // The notes row. Push the verbose screen rather than leaving this one:
    // Back from there comes straight back to the chapter list, with the
    // highlight where it was.
    startActivityForResult(std::make_unique<BookNotesActivity>(renderer, mappedInput), [](const ActivityResult&) {});
    return;
  }
  // The activated row leaves this screen (finish); a lingering flash would gray
  // an unrelated element on the next render.
  app.clearTapFlash();
  const auto tocItem = epub->getTocItem(index - headerRowCount());
  if (tocItem.spineIndex == -1) {
    // The entry reaches no spine item, so there is nowhere to go and the pick
    // is dropped -- which on screen is the reader repainting the page it was
    // already on, i.e. "Chapter Select does nothing". Say so: silence here
    // cost a whole investigation, because a dropped pick and a pick that
    // lands wrong look identical from the outside. The href is the useful
    // half -- it is what failed to match a spine item at index time.
    LOG_ERR("ECS", "TOC entry '%s' reaches no spine item (href '%s'); pick dropped", tocItem.title.c_str(),
            tocItem.href.c_str());
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
  } else {
    setResult(ChapterResult{tocItem.spineIndex, tocItem.anchor});
    finish();
  }
}

bool EpubReaderChapterSelectionActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return true;
  }

  if (!epub) {
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }

  return false;
}

int EpubReaderChapterSelectionActivity::listTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // The book-progress bar band (bar plus the tick overhang above and below)
  // sits between the header and the list, so the list starts one band lower.
  return safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing +
         metrics.popupProgressBarHeight * 2 + kTickOverhang * 2;
}

void EpubReaderChapterSelectionActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band and the progress bar
  // drawChrome paints.
  screen.setContentMargin(fui::Insets{
      static_cast<int16_t>(listTop()), static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - safe.height + metrics.verticalSpacing),
      static_cast<int16_t>(safe.x)});
  // The list band ends where the pre-port screen's did: verticalSpacing above
  // the safe area's height (its contentHeight was safe.height - contentTop -
  // verticalSpacing).
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (!epub) {
    return;
  }
  drawRows(screen, ListLook{});
}

void EpubReaderChapterSelectionActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_SELECT_CHAPTER));
  if (!epub) return;

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
  // The Find and notes rows preview nothing -- neither is a place in the
  // book -- so the tick stays where the reader is.
  const auto selectedItem = nav.selected < headerRowCount() ? BookMetadataCache::TocEntry{}
                                                            : epub->getTocItem(nav.selected - headerRowCount());
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
}
