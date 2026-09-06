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
  // WAIT for this paint, do not merely request it. The next loop() tick
  // blocks on the network; a deferred request would still be a notification
  // in flight when it does, and the reader would be looking at Home -- on a
  // host that presents only between loop() calls, for the whole sync. The
  // frame names what is about to happen (Contacting GitHub, bar at 0 of 2)
  // and is displayed before this returns. See the header.
  requestUpdateAndWait();
}

void LibraryUpdateActivity::loop() {
  // First pass after the CHECKING frame is on screen -- onEnter waited for it.
  if (state == State::CHECKING && !checkStarted) {
    checkStarted = true;
    runCheck();
    return;
  }

  // ONE BOOK PER TICK, then back to the main loop. The whole sync used to run
  // inside a single loop() call, and a host build presents pixels only between
  // loop() calls: it showed the first SYNCING frame and then nothing until the
  // summary, every "Book N of M" frame converted and never presented. Returning
  // between books gives the host one present per book and costs the device
  // nothing -- skipLoopDelay() is true while SYNCING, so the next tick follows
  // at once. See the header.
  if (state == State::SYNCING) {
    syncNextBook();
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

void LibraryUpdateActivity::runCheck() {
  // Repaint between the check's network steps. immediate=true for the same
  // reason the per-book progress callback uses it: this runs inside a blocking
  // call that will not drain the flag for us.
  auto stepCb = +[](void* ctx, LibraryUpdater::CheckStep step) {
    auto* self = static_cast<LibraryUpdateActivity*>(ctx);
    // The frame onEnter waited for already names the first step, so the
    // CONTACTING callback has nothing to add. Repainting it anyway cost a
    // second, identical refresh that ran on top of the TLS handshake.
    // checkStep has one writer, this task, so reading it here needs no lock.
    if (step == self->checkStep) return;
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

  if (updater.getBooks().empty()) {
    // fetchManifest returns OK with zero books when no entry has a matching
    // asset (a half-published release). The SYNCING frame indexes
    // getBooks()[currentBook], which on an empty vector is a LoadProhibited
    // panic (second-pass audit, 2026-09-04). Nothing to sync is DONE.
    RenderLock lock(*this);
    state = State::DONE;
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = State::SYNCING;
  }
  // Waited for, like the CHECKING frame in onEnter and for the same reason:
  // the next tick blocks on the first download, and "Book 1 of N" over a bar
  // at zero must be on the panel before it does. The books themselves are
  // loop()'s, one per tick.
  requestUpdateAndWait();
}

void LibraryUpdateActivity::syncNextBook() {
  const auto& books = updater.getBooks();
  if (nextBook >= books.size()) {
    // The tick after the last book, so its 100% frame had a tick of its own to
    // reach a host's glass before the summary replaces it.
    // One write at the end of the run, not one per book: see flushSyncRecords.
    updater.flushSyncRecords();
    LOG_INF("LIB", "library sync done: %u updated, %u unchanged, %u errors", updated, unchanged, errors);
    RenderLock lock(*this);
    state = State::DONE;
    requestUpdate();
    return;
  }

  auto progressCb = +[](void* ctx) {
    auto* self = static_cast<LibraryUpdateActivity*>(ctx);
    // immediate=true: this runs inside a download loop that will not drain the
    // flag for us — same as the OTA progress callback.
    self->requestUpdate(true);
  };

  const size_t i = nextBook++;
  {
    RenderLock lock(*this);
    currentBook = i;
    lastRenderedPercent = 101;
    updater.resetBookProgress();  // before the repaint below can read them
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
      switch (updater.lastFailure()) {
        case librarysync::FailureKind::STORAGE:
          storageErrors++;
          break;
        case librarysync::FailureKind::NETWORK:
          networkErrors++;
          break;
        case librarysync::FailureKind::VERIFY:
          verifyErrors++;
          break;
        case librarysync::FailureKind::NONE:
          break;
      }
      break;
  }
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
      // "22 errors" sent the owner to debug Wi-Fi over a card with no /books
      // folder (2026-09-06). One more line names the thing to fix.
      const librarysync::FailureKind why = librarysync::dominantFailure(storageErrors, networkErrors, verifyErrors);
      if (why != librarysync::FailureKind::NONE) {
        const char* hint = why == librarysync::FailureKind::STORAGE   ? tr(STR_LIBRARY_ERRORS_STORAGE)
                           : why == librarysync::FailureKind::NETWORK ? tr(STR_LIBRARY_ERRORS_NETWORK)
                                                                      : tr(STR_LIBRARY_ERRORS_VERIFY);
        renderer.drawCenteredText(SMALL_FONT_ID, top + 2 * (lineHeight + metrics.verticalSpacing), hint);
      }
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
