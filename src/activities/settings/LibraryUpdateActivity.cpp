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
// The small line under the bar: the elapsed clock, or -- once Back has been
// pressed -- what is about to happen. FontUpdateActivity has the same helper.
void drawStatusLine(const GfxRenderer& renderer, int y, unsigned long startMs, bool stopping,
                    const char* stoppingText) {
  if (stopping) {
    renderer.drawCenteredText(SMALL_FONT_ID, y, stoppingText, true, EpdFontFamily::BOLD);
    return;
  }
  char clock[16];
  updprogress::formatElapsed(static_cast<uint32_t>((millis() - startMs) / 1000), clock, sizeof(clock));
  char line[48];
  snprintf(line, sizeof(line), tr(STR_UPDATE_ELAPSED_FORMAT), clock);
  renderer.drawCenteredText(SMALL_FONT_ID, y, line);
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
  startMs = millis();
  updater.setAbortFlag(&abandon);
  // WAIT for this paint, do not merely request it. The next loop() tick
  // blocks on the network; a deferred request would still be a notification
  // in flight when it does, and the reader would be looking at Home -- on a
  // host that presents only between loop() calls, for the whole sync. The
  // frame names what is about to happen (Contacting GitHub, bar at 0 of 2)
  // and is displayed before this returns. See the header.
  requestUpdateAndWait();
}

void LibraryUpdateActivity::onExit() {
  // A host worker still inside a book: abandon it and wait. The book fails --
  // its .part is removed and the copy already on the card is untouched.
  if (worker.inFlight()) {
    abandon.store(true);
    worker.join();
  }
  // Books that synced before a sleep or a home gesture keep their ledger
  // entries, so the next run does not hash them again.
  if (checkStarted && !recordsFlushed) {
    updater.flushSyncRecords();
    recordsFlushed = true;
  }
  Activity::onExit();
}

bool LibraryUpdateActivity::handleHomeGesture() {
  if (worker.inFlight()) {
    requestStop();
    return true;
  }
  return false;
}

void LibraryUpdateActivity::loop() {
  // Read Back FIRST: the SYNCING branch returns early, and a check placed after
  // it is dead code (FontUpdateActivity.cpp learned this in 2026-09).
  const bool backPressed = mappedInput.wasPressed(MappedInputManager::Button::Back);

  // A step in flight (a host worker; on the device start() already finished it).
  if (worker.inFlight()) {
    if (backPressed && (state == State::CHECKING || state == State::SYNCING)) requestStop();
    if (!worker.done()) {
      maybeRepaint();
      return;
    }
    worker.join();
    completeStep();
    return;
  }

  // First pass after the CHECKING frame is on screen -- onEnter waited for it.
  if (state == State::CHECKING && !checkStarted) {
    checkStarted = true;
    startStep(Step::CHECK);
    return;
  }

  // ONE BOOK PER STEP, then back to the main loop. See the header.
  if (state == State::SYNCING) {
    if (backPressed) requestStop();
    maybeRepaint();
    switch (updprogress::nextStep(stopRequested.load(), nextBook, updater.getBooks().size())) {
      case updprogress::Next::STOP:
        finishSync(/*stopped=*/true);
        return;
      case updprogress::Next::FINISH:
        finishSync(/*stopped=*/false);
        return;
      case updprogress::Next::RUN_ITEM:
        beginNextBook();
        return;
    }
    return;
  }

  int x = 0;
  int y = 0;
  const bool dismissed =
      backPressed || mappedInput.wasPressed(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y);
  if (dismissed && (state == State::FAILED || state == State::DONE || state == State::CANCELED ||
                    state == State::NO_WIFI || state == State::NO_TOKEN)) {
    finish();
  }
}

void LibraryUpdateActivity::requestStop() {
  if (stopRequested.exchange(true)) return;
  LOG_INF("LIB", "Back pressed: stopping after the book in hand");
  maybeRepaint();
}

void LibraryUpdateActivity::startStep(Step next) {
  step = next;
  worker.start(&LibraryUpdateActivity::runStep, this);
  // The device ran the step inline: collect it in THIS tick, exactly as the
  // code did before the worker existed (one family / one book per tick). A
  // host collects it from loop() on the tick it finishes.
  if (worker.done()) {
    worker.join();
    completeStep();
  }
}

// THE BLOCKING HALF. On a host this is a worker thread: it touches the updater
// and the step's result fields and nothing else the loop thread writes.
void LibraryUpdateActivity::runStep(void* ctx) {
  auto* self = static_cast<LibraryUpdateActivity*>(ctx);
  switch (self->step) {
    case Step::CHECK: {
      // Repaint between the check's network steps.
      auto stepCb = +[](void* c, LibraryUpdater::CheckStep s) {
        auto* me = static_cast<LibraryUpdateActivity*>(c);
        // The frame onEnter waited for already names the first step; exchange()
        // skips a repeat of it.
        // NO RenderLock here: see checkStep's declaration.
        if (me->checkStep.exchange(s) == s) return;
        me->maybeRepaint();
      };
      self->checkResult = self->updater.fetchManifest(stepCb, self);
      break;
    }
    case Step::BOOK:
      self->bookResult = self->updater.syncBook(self->bookIndex, &LibraryUpdateActivity::onProgress, self);
      break;
    case Step::NONE:
      break;
  }
}

void LibraryUpdateActivity::completeStep() {
  const Step done = step;
  step = Step::NONE;
  if (done == Step::CHECK) afterCheck();
  if (done == Step::BOOK) afterBook();
}

void LibraryUpdateActivity::afterCheck() {
  const LibraryUpdater::LibraryError err = checkResult;

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
    // OOM_ERROR: same reason as the fonts screen, same fix, applied here so the
    // next report is not this ambiguity wearing a different feature's name.
    errorMessage = err == LibraryUpdater::NO_RELEASE         ? tr(STR_LIBRARY_NO_RELEASE)
                   : err == LibraryUpdater::NO_REPO_ACCESS   ? tr(STR_LIBRARY_NO_REPO_ACCESS)
                   : err == LibraryUpdater::BAD_TOKEN        ? tr(STR_LIBRARY_BAD_TOKEN)
                   : err == LibraryUpdater::MANIFEST_TOO_NEW ? tr(STR_LIBRARY_MANIFEST_TOO_NEW)
                   : err == LibraryUpdater::OOM_ERROR        ? tr(STR_UPDATE_OUT_OF_MEMORY)
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

  if (stopRequested.load()) {
    // Back during the check: nothing touched, "stopped after 0 of N".
    finishSync(/*stopped=*/true);
    return;
  }

  {
    RenderLock lock(*this);
    state = State::SYNCING;
    updater.resetBookProgress();
  }
  // Waited for, like the CHECKING frame in onEnter and for the same reason:
  // on the device the next tick blocks on the first download, and "Book 1 of N"
  // over a bar at zero must be on the panel before it does.
  requestUpdateAndWait();
  std::lock_guard<std::mutex> guard(paintMutex);
  shown = snapshot(millis());
  hasShown = true;
  lastPaintMs = millis();
}

void LibraryUpdateActivity::finishSync(bool stopped) {
  // The tick after the last book, so its 100% frame had a tick of its own to
  // reach a host's glass before the summary replaces it.
  // One write at the end of the run, not one per book: see flushSyncRecords.
  updater.flushSyncRecords();
  recordsFlushed = true;
  if (stopped) {
    LOG_INF("LIB", "library sync stopped by the reader after %u of %u books: %u updated, %u unchanged, %u errors",
            static_cast<unsigned>(nextBook), static_cast<unsigned>(updater.getBooks().size()), updated, unchanged,
            errors);
  } else {
    LOG_INF("LIB", "library sync done: %u updated, %u unchanged, %u errors", updated, unchanged, errors);
  }
  RenderLock lock(*this);
  state = stopped ? State::CANCELED : State::DONE;
  requestUpdate();
}

void LibraryUpdateActivity::beginNextBook() {
  const size_t i = nextBook++;
  {
    RenderLock lock(*this);
    currentBook = i;
    updater.resetBookProgress();  // before the repaint below can read them
  }
  bookIndex = i;
  maybeRepaint(/*force=*/true);
  startStep(Step::BOOK);
}

void LibraryUpdateActivity::afterBook() {
  switch (bookResult) {
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

updprogress::Snapshot LibraryUpdateActivity::snapshot(unsigned long now) const {
  updprogress::Snapshot s;
  s.state = static_cast<uint8_t>(state);
  s.phase = state == State::CHECKING ? static_cast<uint8_t>(checkStep.load()) : static_cast<uint8_t>(updater.phase());
  s.item = static_cast<uint32_t>(currentBook);
  s.bytes = updater.getProcessedSize();
  s.elapsedSec = static_cast<uint32_t>((now - startMs) / 1000);
  s.stopping = stopRequested.load();
  return s;
}

// See FontUpdateActivity::maybeRepaint: callbacks on the device's loop task or
// a host's worker, plus loop()'s heartbeat on a host.
void LibraryUpdateActivity::maybeRepaint(bool force) {
  if (state != State::CHECKING && state != State::SYNCING) return;
  std::lock_guard<std::mutex> guard(paintMutex);
  const unsigned long now = millis();
  const updprogress::Snapshot s = snapshot(now);
  if (!force && !updprogress::shouldRepaint(hasShown, shown, s, static_cast<uint32_t>(now - lastPaintMs))) return;
  shown = s;
  hasShown = true;
  lastPaintMs = now;
  requestUpdate(true);
}

void LibraryUpdateActivity::onProgress(void* ctx) { static_cast<LibraryUpdateActivity*>(ctx)->maybeRepaint(); }

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
      const bool reading = checkStep.load() == LibraryUpdater::CheckStep::READING;
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_CHECKING_FOR_UPDATES), true, EpdFontFamily::BOLD);
      int y = top + lineHeight + metrics.verticalSpacing;
      renderer.drawCenteredText(UI_10_FONT_ID, y,
                                reading ? tr(STR_LIBRARY_READING_MANIFEST) : tr(STR_LIBRARY_CONTACTING));
      y += lineHeight + metrics.verticalSpacing;
      GUI.drawProgressBar(
          renderer,
          Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
          reading ? 1 : 0, 2);
      // Below the bar's own percentage label, which BaseTheme::drawProgressBar
      // draws at the bar's bottom + 15 in UI_10.
      y += metrics.progressBarHeight + 15 + lineHeight + metrics.verticalSpacing;
      drawStatusLine(renderer, y, startMs, stopRequested.load(), tr(STR_LIBRARY_STOPPING));
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
      const updprogress::Phase phase = updater.phase();
      const size_t total = updater.getTotalSize();
      const size_t processed = updater.getProcessedSize();
      // The CURRENT book's share counts only while it DOWNLOADS (and all of it
      // once it is being put in place): processed/total also carry the hash of
      // the copy already on the card, and counting that would run the bar
      // forward and pull it back when the download began.
      unsigned int bookPct = 0;
      if (phase == updprogress::Phase::DOWNLOADING && total > 0) {
        bookPct = static_cast<unsigned int>((static_cast<uint64_t>(processed) * 100) / total);
      } else if (phase == updprogress::Phase::INSTALLING) {
        bookPct = 100;
      }
      // ...and the bar shows the WHOLE JOB. Per-book was seventeen fills from 0
      // to 100 on a seventeen-book sync, which says "busy" and never says "how
      // far". librarysync::overallPercent carries the reasoning, including why
      // the denominator is books rather than bytes.
      const unsigned int pct = librarysync::overallPercent(currentBook, updater.getBooks().size(), bookPct);

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

      // What it is doing, named, with the book's bytes. See UpdateProgress.h.
      char detail[96];
      if (phase == updprogress::Phase::CHECKING || phase == updprogress::Phase::DOWNLOADING) {
        char done[12];
        char all[12];
        const size_t declared = updater.getBooks()[currentBook].bytes;
        updprogress::formatMb(processed < declared ? processed : declared, done, sizeof(done));
        updprogress::formatMb(declared, all, sizeof(all));
        snprintf(detail, sizeof(detail),
                 phase == updprogress::Phase::CHECKING ? tr(STR_UPDATE_CHECKING_BOOK_FORMAT)
                                                       : tr(STR_UPDATE_DOWNLOADING_BOOK_FORMAT),
                 done, all);
      } else if (phase == updprogress::Phase::INSTALLING) {
        snprintf(detail, sizeof(detail), "%s", tr(STR_UPDATE_INSTALLING));
      } else {
        snprintf(detail, sizeof(detail), "%s", tr(STR_UPDATE_PREPARING));
      }
      renderer.drawCenteredText(SMALL_FONT_ID, y, detail);
      y += lineHeight + metrics.verticalSpacing;

      GUI.drawProgressBar(
          renderer,
          Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
          static_cast<int>(pct), 100);
      // Below the bar's own percentage label, which BaseTheme::drawProgressBar
      // draws at the bar's bottom + 15 in UI_10.
      y += metrics.progressBarHeight + 15 + lineHeight + metrics.verticalSpacing;
      drawStatusLine(renderer, y, startMs, stopRequested.load(), tr(STR_LIBRARY_STOPPING));
      const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    // Stopped by the reader, not finished: it must not read as success.
    case State::CANCELED: {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_FONTS_STOPPED), true, EpdFontFamily::BOLD);
      char summary[64];
      snprintf(summary, sizeof(summary), tr(STR_LIBRARY_STOPPED_FORMAT), static_cast<unsigned>(nextBook),
               static_cast<unsigned>(updater.getBooks().size()));
      int y = top + lineHeight + metrics.verticalSpacing;
      renderer.drawCenteredText(UI_10_FONT_ID, y, summary);
      y += lineHeight + metrics.verticalSpacing;
      char counts[64];
      snprintf(counts, sizeof(counts), tr(STR_LIBRARY_SUMMARY_FORMAT), updated, unchanged, errors);
      renderer.drawCenteredText(UI_10_FONT_ID, y, counts);
      const Rect hintBounds{metrics.contentSidePadding, y + lineHeight + metrics.verticalSpacing,
                            pageWidth - metrics.contentSidePadding * 2,
                            pageHeight - (y + lineHeight + metrics.verticalSpacing)};
      UITheme::drawCenteredWrappedText(renderer, hintBounds, SMALL_FONT_ID, tr(STR_LIBRARY_STOPPED_HINT), 2, true,
                                       EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
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
