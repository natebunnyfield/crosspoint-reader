#include "ReaderActivity.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderActivity.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "TxtReaderActivity.h"
#include "XtcReaderActivity.h"
#include "activities/util/BmpViewerActivity.h"
#include "fontIds.h"

ReaderActivity::ReaderActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                               std::string bookPath, const bool allowFastInitialRefresh)
    : Activity(name, renderer, mappedInput), bookPath(std::move(bookPath)) {
  if (allowFastInitialRefresh) {
    const int refreshFrequency = SETTINGS.getRefreshFrequency();
    pagesUntilFullRefresh = refreshFrequency > 1 ? refreshFrequency : 2;
  }
}

bool ReaderActivity::isTxtFile(const std::string& path) {
  // hasPlainTextExtension covers .txt and the other unstyled formats -- json,
  // log, csv, xml, yaml, ini, cfg, conf (owner ruling 2026-08-11). Markdown is
  // read as plain text until there is a markdown reader.
  return FsHelpers::hasPlainTextExtension(path) || FsHelpers::hasMarkdownExtension(path);
}

std::unique_ptr<ReaderActivity> ReaderActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                       std::string path, const bool allowFastInitialRefresh) {
  // ActivityManager requires heap ownership; each branch allocates exactly one screen-lifetime object.
  std::unique_ptr<ReaderActivity> activity;
  if (FsHelpers::hasXtcExtension(path)) {
    activity = makeUniqueNoThrow<XtcReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  } else if (isTxtFile(path)) {
    activity = makeUniqueNoThrow<TxtReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  } else {
    activity = makeUniqueNoThrow<EpubReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  }
  if (!activity) {
    LOG_ERR("READER", "OOM: reader activity");
  }
  return activity;
}

std::unique_ptr<Activity> ReaderActivity::open(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path,
                                               const bool allowFastInitialRefresh) {
  if (FsHelpers::hasBmpExtension(path) || FsHelpers::hasPngExtension(path)) {
    auto viewer = makeUniqueNoThrow<BmpViewerActivity>(renderer, mappedInput, std::move(path));
    if (!viewer) {
      LOG_ERR("READER", "OOM: bitmap viewer activity");
    }
    return viewer;
  }
  return create(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
}

void ReaderActivity::onEnter() {
  Activity::onEnter();

  if (!Storage.exists(bookPath.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", bookPath.c_str());
    finish();
    return;
  }

  sdFontSystem.ensureLoaded(renderer);

  if (!loadBook()) {
    finish();
    return;
  }

  // Save the book as last opened and add it to recent books
  APP_STATE.openEpubPath = bookPath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(bookPath, getBookTitle(), getBookAuthor(), getBookThumbBmpPath());

  // Trigger first update
  requestUpdate();
}

void ReaderActivity::onExit() {
  Activity::onExit();

  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
}

bool ReaderActivity::handleBackNavigation() {
  return ReaderUtils::handleBackNavigation(mappedInput, activityManager, bookPath.c_str(),
                                           {this, [](void* ctx) { static_cast<ReaderActivity*>(ctx)->onGoHome(); }});
}

void ReaderActivity::loop() {
  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);

  // The end-of-book screen: Confirm (or the touch menu gesture) and Back go
  // Home (docs/ui-conventions.md, "End of book").
  if (isAtEndOfBook() &&
      (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || ReaderUtils::isTouchMenuGesture(mappedInput) ||
       mappedInput.wasReleased(MappedInputManager::Button::Back))) {
    onGoHome();
    return;
  }

  if (handleFormatInput()) return;
  if (handleBackNavigation()) return;

  auto [prevTriggered, nextTriggered] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) return;

  // At end of the book: forward goes home, back returns to the last page
  if (isAtEndOfBook()) {
    if (nextTriggered) {
      onGoHome();
    } else {
      onReturnFromEndOfBook();
      requestUpdate();
    }
    return;
  }

  // Back takes precedence when both register on one tick.
  if (pageTurn(!prevTriggered)) {
    requestUpdate();
  }
}

void ReaderActivity::render(RenderLock&& lock) {
  if (isAtEndOfBook()) {
    renderEndOfBook();
    return;
  }
  renderBook(std::move(lock));
}

void ReaderActivity::renderEndOfBook() {
  renderer.clearScreen();
  // 3/8 of the screen height matches the previous fixed position on the 480x800 panel
  // and scales to other resolutions.
  renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() * 3 / 8, tr(STR_END_OF_BOOK), true,
                            EpdFontFamily::BOLD);
  renderer.displayBuffer();
}

bool ReaderActivity::handleForcedRefresh() {
  {
    RenderLock lock(*this);
    pagesUntilFullRefresh = 1;
    forcedRefreshPending = true;
  }
  requestUpdate();
  return true;
}
