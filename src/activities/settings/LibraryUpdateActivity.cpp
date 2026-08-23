#include "LibraryUpdateActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void LibraryUpdateActivity::onEnter() {
  Activity::onEnter();

  // Same division of labor as the firmware update screen: joining a network is
  // Settings' job, and saying so beats a generic failure after a timeout.
  if (WiFi.status() != WL_CONNECTED) {
    state = State::NO_WIFI;
    requestUpdate();
    return;
  }

  state = State::CHECKING;
  requestUpdate();
}

void LibraryUpdateActivity::loop() {
  // First pass after the CHECKING frame is on screen — see the header.
  if (state == State::CHECKING && !checkStarted) {
    checkStarted = true;
    runSync();
    return;
  }

  int x = 0;
  int y = 0;
  const bool dismissed = mappedInput.wasPressed(MappedInputManager::Button::Back) ||
                         mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
                         mappedInput.wasScreenTapped(x, y);
  if (dismissed &&
      (state == State::FAILED || state == State::DONE || state == State::NO_WIFI || state == State::NO_TOKEN)) {
    finish();
  }
}

void LibraryUpdateActivity::runSync() {
  const LibraryUpdater::LibraryError err = updater.fetchManifest();

  if (err == LibraryUpdater::NO_TOKEN) {
    LOG_INF("LIB", "no GitHub token configured");
    RenderLock lock(*this);
    state = State::NO_TOKEN;
    requestUpdate();
    return;
  }

  if (err != LibraryUpdater::OK) {
    LOG_ERR("LIB", "manifest check failed (%d)", static_cast<int>(err));
    // NO_RELEASE gets its own words for the same reason the OTA screen's does:
    // GitHub was reached and answered; blaming the network sends the owner to
    // debug Wi-Fi over a release that was never published.
    // Three distinct causes, three distinct sentences. "Check failed" sends the
    // owner to debug Wi-Fi over a manifest GitHub served perfectly.
    errorMessage = err == LibraryUpdater::NO_RELEASE      ? tr(STR_LIBRARY_NO_RELEASE)
                   : err == LibraryUpdater::MANIFEST_TOO_NEW ? tr(STR_LIBRARY_MANIFEST_TOO_NEW)
                                                             : tr(STR_UPDATE_CHECK_FAILED);
    RenderLock lock(*this);
    state = State::FAILED;
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = State::SYNCING;
  }
  requestUpdateAndWait();

  auto progressCb = +[](void* ctx) {
    auto* self = static_cast<LibraryUpdateActivity*>(ctx);
    // immediate=true: this runs inside a download loop that will not drain the
    // flag for us — same as the OTA progress callback.
    self->requestUpdate(true);
  };

  const auto& books = updater.getBooks();
  for (size_t i = 0; i < books.size(); ++i) {
    {
      RenderLock lock(*this);
      currentBook = i;
      lastRenderedPercent = 101;
    }
    requestUpdate(true);
    switch (updater.syncBook(i, progressCb, this)) {
      case LibraryUpdater::BookResult::ADDED:
      case LibraryUpdater::BookResult::UPDATED:
        updated++;
        break;
      case LibraryUpdater::BookResult::UNCHANGED:
        unchanged++;
        break;
      case LibraryUpdater::BookResult::FAILED:
        errors++;
        break;
    }
  }

  // One write at the end of the run, not one per book: see flushSyncRecords.
  updater.flushSyncRecords();

  LOG_INF("LIB", "library sync done: %u updated, %u unchanged, %u errors", updated, unchanged, errors);
  RenderLock lock(*this);
  state = State::DONE;
  requestUpdate();
}

void LibraryUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_UPDATE_LIBRARY));

  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - lineHeight) / 2;

  switch (state) {
    case State::CHECKING:
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_CHECKING_FOR_UPDATES));
      break;

    case State::NO_WIFI: {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_NEEDS_WIFI), true, EpdFontFamily::BOLD);
      const int hintY = top + lineHeight + metrics.verticalSpacing;
      const Rect hintBounds{metrics.contentSidePadding, hintY, pageWidth - metrics.contentSidePadding * 2,
                            pageHeight - hintY};
      UITheme::drawCenteredWrappedText(renderer, hintBounds, UI_10_FONT_ID, tr(STR_UPDATE_NEEDS_WIFI_HINT), 3, true,
                                       EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    case State::NO_TOKEN: {
      // Plain words, and nothing else happens: the repo is private, so without
      // a token there is nothing this screen can usefully attempt.
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_LIBRARY_NEEDS_TOKEN), true, EpdFontFamily::BOLD);
      const int hintY = top + lineHeight + metrics.verticalSpacing;
      const Rect hintBounds{metrics.contentSidePadding, hintY, pageWidth - metrics.contentSidePadding * 2,
                            pageHeight - hintY};
      UITheme::drawCenteredWrappedText(renderer, hintBounds, UI_10_FONT_ID, tr(STR_LIBRARY_NEEDS_TOKEN_HINT), 3, true,
                                       EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    case State::SYNCING: {
      const size_t total = updater.getTotalSize();
      const size_t processed = updater.getProcessedSize();
      const unsigned int pct = total > 0 ? static_cast<unsigned int>((processed * 100) / total) : 0;
      // Once per percent, same e-ink reasoning as the OTA screen.
      if (pct == lastRenderedPercent) return;
      lastRenderedPercent = pct;

      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_LIBRARY_SYNCING), true, EpdFontFamily::BOLD);
      int y = top + lineHeight + metrics.verticalSpacing;
      char bookLine[48];
      snprintf(bookLine, sizeof(bookLine), tr(STR_LIBRARY_BOOK_PROGRESS_FORMAT),
               static_cast<unsigned>(currentBook + 1), static_cast<unsigned>(updater.getBooks().size()));
      renderer.drawCenteredText(UI_10_FONT_ID, y, bookLine);
      y += lineHeight + metrics.verticalSpacing;
      const std::string& file = updater.getBooks()[currentBook].file;
      renderer.drawCenteredText(UI_10_FONT_ID, y, file.c_str());
      y += lineHeight + metrics.verticalSpacing;
      GUI.drawProgressBar(
          renderer,
          Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
          static_cast<int>(pct), 100);
      break;
    }

    case State::DONE: {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_COMPLETE), true, EpdFontFamily::BOLD);
      char summary[64];
      snprintf(summary, sizeof(summary), tr(STR_LIBRARY_SUMMARY_FORMAT), updated, unchanged, errors);
      renderer.drawCenteredText(UI_10_FONT_ID, top + lineHeight + metrics.verticalSpacing, summary);
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    case State::FAILED: {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_FAILED), true, EpdFontFamily::BOLD);
      if (!errorMessage.empty()) {
        renderer.drawCenteredText(UI_10_FONT_ID, top + lineHeight + metrics.verticalSpacing, errorMessage.c_str());
      }
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }
  }

  renderer.displayBuffer();
}
