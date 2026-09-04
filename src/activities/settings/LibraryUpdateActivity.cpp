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
#include "network/LibrarySyncPlan.h"
#ifdef SIMULATOR
#include <SimHostSettings.h>
#endif

namespace {
// WHERE the owner should go to set the token, which is not the same sentence on
// every build this firmware runs on. settings.json on the card is the truth on
// an X3 and on the desktop simulator; on a phone that file cannot be opened at
// all, and printing it there was advice nobody could follow -- the whole reason
// Update Library was unreachable on iOS. The host says whether it has a
// settings surface of its own; see SimHostSettings.h.
// Returns the resolved string, not the id: tr() is a macro that pastes
// `StrId::` onto its argument, so it cannot take a value chosen at runtime.
const char* needsTokenHint() {
#ifdef SIMULATOR
  if (sim_host_settings::hasSettingsSurface()) {
    return I18N.get(StrId::STR_LIBRARY_NEEDS_TOKEN_HINT_HOST);
  }
#endif
  return I18N.get(StrId::STR_LIBRARY_NEEDS_TOKEN_HINT);
}
}  // namespace

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
  // Repaint between the check's network steps. immediate=true for the same
  // reason the per-book progress callback uses it: this runs inside a blocking
  // call that will not drain the flag for us.
  auto stepCb = +[](void* ctx, LibraryUpdater::CheckStep step) {
    auto* self = static_cast<LibraryUpdateActivity*>(ctx);
    {
      RenderLock lock(*self);
      self->checkStep = step;
    }
    self->requestUpdate(true);
  };
  const LibraryUpdater::LibraryError err = updater.fetchManifest(stepCb, this);

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
    // BAD_TOKEN joins them for the same reason, and it is now the most likely
    // of the four: the token used to come from a file edited deliberately on a
    // computer, and now it is typed on a phone keyboard.
    //
    // NO_RELEASE and NO_REPO_ACCESS were ONE message until the updater learned
    // to probe the repo after a 404. GitHub answers 404 for a private repo
    // whether the release is missing or the token cannot see it, so the release
    // endpoint alone cannot separate them; asking about the repo can, and it
    // costs one request on a path that has already failed.
    errorMessage = err == LibraryUpdater::NO_RELEASE         ? tr(STR_LIBRARY_NO_RELEASE)
                   : err == LibraryUpdater::NO_REPO_ACCESS   ? tr(STR_LIBRARY_NO_REPO_ACCESS)
                   : err == LibraryUpdater::BAD_TOKEN        ? tr(STR_LIBRARY_BAD_TOKEN)
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
    case State::CHECKING: {
      // Two lines and a two-step bar, not one static line. The whole check runs
      // inside one loop() call -- nothing repaints and no button is read until
      // it returns -- so a single frozen line reads as a hang. The step
      // callback repaints between the two network requests, which is the only
      // honest motion available here.
      //
      // A bar rather than a spinner because this is e-ink: an animation costs a
      // panel refresh per frame, and two steps cost two.
      const bool reading = checkStep == LibraryUpdater::CheckStep::READING;
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_CHECKING_FOR_UPDATES), true, EpdFontFamily::BOLD);
      int y = top + lineHeight + metrics.verticalSpacing;
      renderer.drawCenteredText(UI_10_FONT_ID, y,
                                reading ? tr(STR_LIBRARY_READING_MANIFEST) : tr(STR_LIBRARY_CONTACTING));
      y += lineHeight + metrics.verticalSpacing;
      GUI.drawProgressBar(
          renderer,
          Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
          reading ? 1 : 0, 2);
      break;
    }

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
      UITheme::drawCenteredWrappedText(renderer, hintBounds, UI_10_FONT_ID, needsTokenHint(), 3, true,
                                       EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    case State::SYNCING: {
      const size_t total = updater.getTotalSize();
      const size_t processed = updater.getProcessedSize();
      // The CURRENT book's own progress; LibraryUpdater resets these per book.
      const unsigned int bookPct = total > 0 ? static_cast<unsigned int>((processed * 100) / total) : 0;
      // ...and the bar shows the WHOLE JOB. Per-book was seventeen fills from 0
      // to 100 on a seventeen-book sync, which says "busy" and never says "how
      // far". librarysync::overallPercent carries the reasoning, including why
      // the denominator is books rather than bytes.
      const unsigned int pct = librarysync::overallPercent(currentBook, updater.getBooks().size(), bookPct);
      // Once per percent, same e-ink reasoning as the OTA screen.
      if (pct == lastRenderedPercent) return;
      lastRenderedPercent = pct;

      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_LIBRARY_SYNCING), true, EpdFontFamily::BOLD);
      int y = top + lineHeight + metrics.verticalSpacing;
      char bookLine[48];
      snprintf(bookLine, sizeof(bookLine), tr(STR_LIBRARY_BOOK_PROGRESS_FORMAT), static_cast<unsigned>(currentBook + 1),
               static_cast<unsigned>(updater.getBooks().size()));
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
