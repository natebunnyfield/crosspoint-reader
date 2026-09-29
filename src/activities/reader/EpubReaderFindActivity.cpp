#include "EpubReaderFindActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"

void EpubReaderFindActivity::onEnter() {
  Activity::onEnter();
  finder.setLendFrameBuffer(true);
  armed = finder.begin(query, startSpine, startPage, startOffset);
  startedMs = millis();
  if (!armed) {
    // Chapter Select only returns a query with something searchable in it, so
    // this is a book with no spine; say so rather than hang on a popup.
    state = State::NotFound;
    notFoundSinceMs = millis();
  }
  requestUpdate();
}

void EpubReaderFindActivity::onExit() {
  // Releases the open section; an unfinished build is suspended into a partial
  // section file, the same as leaving the reader mid-build.
  finder.close();
  Activity::onExit();
}

void EpubReaderFindActivity::finishCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void EpubReaderFindActivity::loop() {
  if (state == State::NotFound) {
    const bool anyPress = mappedInput.wasReleased(MappedInputManager::Button::Back) ||
                          mappedInput.wasReleased(MappedInputManager::Button::Confirm);
    if (anyPress || millis() - notFoundSinceMs >= NOT_FOUND_HOLD_MS) finishCancelled();
    return;
  }

  // Cancel between units of work: the reader's position has not moved.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    LOG_INF("FIND", "Cancelled after %lums (%u pages scanned)", millis() - startedMs, finder.pagesScanned());
    finder.close();
    finishCancelled();
    return;
  }

  BookFinder::Status status = BookFinder::Status::Running;
  {
    // Layout and page reads measure glyphs through the renderer; hold the lock
    // so the render task cannot draw (or find the framebuffer on loan) meanwhile.
    RenderLock lock;
    const unsigned long t0 = millis();
    do {
      status = finder.step();
    } while (status == BookFinder::Status::Running && millis() - t0 < STEP_BUDGET_MS);
  }

  if (status == BookFinder::Status::Found) {
    const auto& hit = finder.hit();
    LOG_INF("FIND", "Search took %lums", millis() - startedMs);
    setResult(FindResult{hit.spine, hit.page, hit.offset});
    finish();
    return;
  }
  if (status == BookFinder::Status::NotFound) {
    LOG_INF("FIND", "Search took %lums", millis() - startedMs);
    state = State::NotFound;
    notFoundSinceMs = millis();
    requestUpdate();
    return;
  }
  if (finder.chapterNumber() != shownChapter && millis() - lastPopupMs >= POPUP_MIN_INTERVAL_MS) {
    requestUpdate();
  }
}

void EpubReaderFindActivity::render(RenderLock&&) {
  if (state == State::NotFound) {
    GUI.drawPopup(renderer, tr(STR_FIND_NOT_FOUND));
    return;
  }
  const int chapter = finder.chapterNumber() > 0 ? finder.chapterNumber() : 1;
  char message[96];
  snprintf(message, sizeof(message), tr(STR_FIND_SEARCHING_FORMAT), static_cast<unsigned>(chapter),
           static_cast<unsigned>(finder.chapterTotal()));
  GUI.drawPopup(renderer, message);
  shownChapter = finder.chapterNumber();
  lastPopupMs = millis();
}
