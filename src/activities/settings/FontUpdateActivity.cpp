#include "FontUpdateActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/FontSyncPlan.h"
#ifdef SIMULATOR
#include <SimHostSettings.h>
#endif

namespace {
// WHERE the owner should go to set the token, which is not the same sentence on
// every build this firmware runs on -- settings.json on the card cannot be
// opened at all on a phone. Same helper and same reasoning as
// LibraryUpdateActivity's; the strings are shared because the token is.
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
// The small line under the bar: the elapsed clock while the run works, or --
// once Back has been pressed -- what is about to happen. Shared by the checking
// and syncing frames so the press is acknowledged on whichever is up.
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

// A run that ends in errors leaves its log ring on the CARD.
//
// Update Fonts has now failed on the owner's device three times running, each
// time differently, and every diagnosis has cost a round trip: the ring holds
// the one line that names the cause -- "Download of Edgar_8.cpfont failed (2)
// after 131072 of 582482 bytes" -- and LOG_ERR is compiled into gh_release, but
// getLastLogs() is only ever dumped inside a PANIC report
// (lib/hal/HalSystem.cpp). These failures do not panic, so the answer exists,
// is already built, and is invisible without a USB cable.
//
// This is the project rule about a second device failure meaning
// instrumentation rather than another plausible patch. Overwritten each run,
// never appended: one file, always the latest failure, no growth on the card.
void FontUpdateActivity::writeFailureLog(unsigned updated, unsigned unchanged, unsigned removed, unsigned errors) {
  // openFileForWrite rather than raw open flags: it is the helper every other
  // writer here uses, and it is the one the host stubs implement.
  HalFile file;
  if (!Storage.openFileForWrite("FONTUPD", FAILURE_LOG_PATH, file)) {
    LOG_ERR("FONTUPD", "Could not write %s", FAILURE_LOG_PATH);
    return;
  }
#ifndef CROSSPOINT_VERSION
#define CROSSPOINT_VERSION "host"
#endif
  char header[160];
  const int n = snprintf(header, sizeof(header),
                         "CrossPoint " CROSSPOINT_VERSION
                         "\nfont sync: %u updated, %u unchanged, %u removed, "
                         "%u errors\n\nLast logs:\n",
                         updated, unchanged, removed, errors);
  // snprintf returns what it WOULD have written, not what it did. Writing that
  // many bytes out of a 160-byte array reads past it whenever the header is
  // longer -- four %u at ten digits each is 40 characters of counters alone.
  // Clamp to what is actually in the buffer.
  const size_t headerLen =
      n < 0 ? 0 : (static_cast<size_t>(n) < sizeof(header) ? static_cast<size_t>(n) : sizeof(header) - 1);

  // Both writes are CHECKED. This file is the only diagnostic B-055 has, and a
  // card with no room left would otherwise truncate it silently while the log
  // line below claimed success -- sending the next session to debug whatever
  // the missing tail would have named.
  bool wrote = (headerLen == 0) || file.write(header, headerLen) == headerLen;
  const std::string logs = getLastLogs();
  if (wrote && !logs.empty()) wrote = file.write(logs.c_str(), logs.size()) == logs.size();
  file.close();
  if (wrote) {
    LOG_INF("FONTUPD", "wrote %s", FAILURE_LOG_PATH);
  } else {
    LOG_ERR("FONTUPD", "%s is incomplete -- the card would not take it", FAILURE_LOG_PATH);
  }
}

void FontUpdateActivity::onEnter() {
  Activity::onEnter();
  UPD_TRACE_BEGIN("FontUpdateActivity");

  // Joining a network is Settings' job, and saying so beats a generic failure
  // after a timeout.
  if (WiFi.status() != WL_CONNECTED) {
    state = State::NO_WIFI;
    requestUpdate();
    return;
  }

  state = State::CHECKING;
  startMs = millis();
  UPD_KEEP_AWAKE(true);
  updater.setAbortFlag(&abandon);
  // WAIT for this paint, do not merely request it. See the header.
  UPD_TRACE_MARK("onEnter: waiting for the CHECKING frame");
  requestUpdateAndWait();
  UPD_TRACE_MARK("onEnter: CHECKING frame rendered");
}

void FontUpdateActivity::onExit() {
  // A host worker still inside a family: abandon it and wait. The family in
  // hand fails whole -- its staging directory is removed and whatever was
  // installed before is untouched -- which is the same state a network failure
  // mid-family leaves. On the device a step never outlives a loop() call, so
  // this finds nothing in flight.
  if (worker.inFlight()) {
    abandon.store(true);
    worker.join();
  }
  // A run left part-way still records what it installed.
  if (checkStarted && !runFinished && updater.getFamilies().size() > 0) {
    updater.finishRun();
    runFinished = true;
  }
  UPD_KEEP_AWAKE(false);  // Back, sleep, home, destroyed: every exit releases
  UPD_TRACE_END();
  Activity::onExit();
}

bool FontUpdateActivity::handleHomeGesture() {
  if (worker.inFlight()) {
    requestStop();
    return true;
  }
  return false;
}

void FontUpdateActivity::loop() {
  // READ THE BACK EDGE FIRST, before any branch can return past it.
  //
  // The edge itself is sampled for us: src/main.cpp:1097 calls
  // mappedInputManager.update() at the top of every main-loop iteration, and
  // ActivityManager.cpp:82 calls this loop() afterwards, so wasPressed() here
  // reports this tick's input. What made Back unreachable during a sync -- and
  // made an earlier version of this header's claim false -- was only the early
  // `return` in the SYNCING branch below, which used to sit ahead of the
  // input block at the bottom. Reading it up here is what makes the cancel
  // check a check rather than dead code.
  // Every tick, ahead of any return: the host holds the phone awake exactly
  // while the run works, so done / failed / stopped release it on the next tick.
  UPD_KEEP_AWAKE(state == State::CHECKING || state == State::SYNCING);
  const bool backPressed = mappedInput.wasPressed(MappedInputManager::Button::Back);

  // A STEP IN FLIGHT (a host worker; on the device start() already finished it).
  // Take Back now -- it is acknowledged on screen at once and acts between
  // families -- keep the clock moving, and collect the result when it is ready.
  if (worker.inFlight() || state == State::CHECKING || state == State::SYNCING) UPD_TRACE_WORKING();
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

  // ONE FAMILY PER STEP, then back to the main loop. See the header.
  if (state == State::SYNCING) {
    if (backPressed) requestStop();
    maybeRepaint();
    // THE CANCEL POINT, and the only one. A step begins after the previous
    // family's commit finished and before the next one starts, so a cancel
    // here can never catch a family mid-install -- the two-rename commit has
    // already either happened or been rolled back. Owner ruling 2026-09-07.
    switch (updprogress::nextStep(stopRequested.load(), nextFamily, updater.getFamilies().size())) {
      case updprogress::Next::STOP:
        cancelSync();
        return;
      case updprogress::Next::FINISH:
        startStep(Step::FINISH);
        return;
      case updprogress::Next::RUN_ITEM:
        beginNextFamily();
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

void FontUpdateActivity::requestStop() {
  if (stopRequested.exchange(true)) return;
  LOG_INF("FONTUPD", "Back pressed: stopping after the family in hand");
  // Structural: painted within a quarter second, so the press is seen to land.
  maybeRepaint();
}

void FontUpdateActivity::startStep(Step next) {
  step = next;
  UPD_TRACE_MARK(next == Step::CHECK ? "step CHECK start" : next == Step::FINISH ? "step FINISH start" : "step FAMILY start");
  worker.start(&FontUpdateActivity::runStep, this);
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
void FontUpdateActivity::runStep(void* ctx) {
  auto* self = static_cast<FontUpdateActivity*>(ctx);
  switch (self->step) {
    case Step::CHECK: {
      // Repaint between the check's network steps.
      auto stepCb = +[](void* c, FontUpdater::CheckStep s) {
        auto* me = static_cast<FontUpdateActivity*>(c);
        // The frame onEnter waited for already names the first step; exchange()
        // skips a repeat of it.
        // NO RenderLock here: see checkStep's declaration.
        if (me->checkStep.exchange(s) == s) return;
        me->maybeRepaint();
      };
      self->checkResult = self->updater.fetchManifest(stepCb, self);
      break;
    }
    case Step::FAMILY:
      self->familyResult = self->updater.syncFamily(self->familyIndex, &FontUpdateActivity::onProgress, self);
      break;
    case Step::FINISH:
      self->updater.setPhase(updprogress::Phase::FINISHING);
      self->maybeRepaint();
      // REMOVAL RUNS HERE: after every family has been offered, and only on the
      // path that reaches the summary. Removing FIRST would free card space
      // before ~80 MB of downloads, and would mean a run that then fails, or
      // that the reader stops, has destroyed families and installed nothing --
      // the worst trade this screen could make with the owner's data. A CANCEL
      // NEVER REMOVES (owner ruling 2026-09-07): updprogress::nextStep answers
      // STOP, not FINISH, once Back has been pressed.
      self->removed = static_cast<unsigned>(self->updater.removeUnlistedFamilies(self->removedFamilies));
      break;
    case Step::NONE:
      break;
  }
  UPD_TRACE_MARK("worker: step returned");
}

void FontUpdateActivity::completeStep() {
  UPD_TRACE_MARK("loop: collecting the step");
  const Step done = step;
  step = Step::NONE;
  switch (done) {
    case Step::CHECK:
      afterCheck();
      break;
    case Step::FAMILY:
      afterFamily();
      break;
    case Step::FINISH:
      afterFinish();
      break;
    case Step::NONE:
      break;
  }
}

void FontUpdateActivity::cancelSync() {
  // finishRun() STILL RUNS. Families that committed before the cancel are
  // installed, so the ledger has to record them and the registry has to be
  // re-discovered -- skipping it would leave a newly installed family absent
  // from the picker until reboot and make the next run hash it all again.
  // It is the same call the DONE path makes, for the same reasons.
  updater.finishRun();
  runFinished = true;
  LOG_INF("FONTUPD", "font sync stopped by the reader after %u of %u families: %u updated, %u unchanged, %u errors",
          static_cast<unsigned>(nextFamily), static_cast<unsigned>(updater.getFamilies().size()), updated, unchanged,
          errors);
  RenderLock lock(*this);
  state = State::CANCELED;
  requestUpdate();
}

void FontUpdateActivity::afterCheck() {
  const FontUpdater::FontError err = checkResult;

  if (err == FontUpdater::NO_TOKEN) {
    LOG_INF("FONTUPD", "no GitHub token configured");
    RenderLock lock(*this);
    state = State::NO_TOKEN;
    requestUpdate();
    return;
  }

  if (err != FontUpdater::OK) {
    LOG_ERR("FONTUPD", "manifest check failed (%d)", static_cast<int>(err));
    // Distinct causes, distinct sentences, for the reason the library screen
    // learned to separate them: "check failed" sends the owner to debug Wi-Fi
    // over a manifest GitHub served perfectly.
    //
    // OOM_ERROR is here because it fell into the default arm and printed
    // "Could not reach GitHub" at an owner whose network was fine -- exactly
    // the failure this chain exists to prevent, repeated against a new cause.
    // Reported 2026-09-07 on 1.5.30-BD; see B-053.
    errorMessage = err == FontUpdater::NO_RELEASE         ? tr(STR_FONTS_NO_RELEASE)
                   : err == FontUpdater::NO_REPO_ACCESS   ? tr(STR_LIBRARY_NO_REPO_ACCESS)
                   : err == FontUpdater::BAD_TOKEN        ? tr(STR_LIBRARY_BAD_TOKEN)
                   : err == FontUpdater::MANIFEST_TOO_NEW ? tr(STR_FONTS_MANIFEST_TOO_NEW)
                   : err == FontUpdater::OOM_ERROR        ? tr(STR_UPDATE_OUT_OF_MEMORY)
                                                          : tr(STR_UPDATE_CHECK_FAILED);
    RenderLock lock(*this);
    state = State::FAILED;
    requestUpdate();
    return;
  }

  if (updater.getFamilies().empty()) {
    // fetchManifest returns OK with zero families when no entry survived
    // validation (a half-published release, or one whose families all failed
    // the all-or-nothing parse). The SYNCING frame indexes
    // getFamilies()[currentFamily], which on an empty vector is a
    // LoadProhibited panic. Nothing to sync is DONE.
    RenderLock lock(*this);
    state = State::DONE;
    requestUpdate();
    return;
  }

  if (stopRequested.load()) {
    // Back during the check. Nothing has touched the card; say so honestly as
    // "stopped after 0 of N" rather than pretending the run completed.
    cancelSync();
    return;
  }

  {
    RenderLock lock(*this);
    state = State::SYNCING;
    updater.resetFamilyProgress();
  }
  // Waited for, like the CHECKING frame and for the same reason: on the device
  // the next tick blocks on the first family, and "Font 1 of N" over a bar at
  // zero must be on the panel before it does.
  UPD_TRACE_MARK("afterCheck: waiting for the SYNCING frame");
  requestUpdateAndWait();
  UPD_TRACE_MARK("afterCheck: SYNCING frame rendered");
  std::lock_guard<std::mutex> guard(paintMutex);
  shown = snapshot(millis());
  hasShown = true;
  lastPaintMs = millis();
}

void FontUpdateActivity::beginNextFamily() {
  const size_t i = nextFamily++;
  {
    RenderLock lock(*this);
    currentFamily = i;
    updater.resetFamilyProgress();  // before the repaint below can read them
  }
  familyIndex = i;
  maybeRepaint(/*force=*/true);
  startStep(Step::FAMILY);
}

void FontUpdateActivity::afterFamily() {
  const auto& fonts = updater.getFamilies();
  const size_t i = familyIndex;
  switch (familyResult) {
    case FontUpdater::FamilyResult::ADDED:
    case FontUpdater::FamilyResult::UPDATED:
      updated++;
      break;
    case FontUpdater::FamilyResult::UNCHANGED:
      unchanged++;
      break;
    case FontUpdater::FamilyResult::SKIPPED_DELETED:
      skippedDeleted++;
      if (!skippedNames.empty()) skippedNames += ", ";
      skippedNames += fonts[i].name;
      break;
    case FontUpdater::FamilyResult::SKIPPED_BUNDLED:
      keptBundled++;
      if (!keptBundledNames.empty()) keptBundledNames += ", ";
      keptBundledNames += fonts[i].name;
      break;
    case FontUpdater::FamilyResult::FAILED:
      errors++;
      switch (updater.lastFailure()) {
        case fontsync::FailureKind::STORAGE:
          storageErrors++;
          break;
        case fontsync::FailureKind::NETWORK:
          networkErrors++;
          break;
        case fontsync::FailureKind::VERIFY:
          verifyErrors++;
          break;
        case fontsync::FailureKind::NONE:
          break;
      }
      break;
  }
}

void FontUpdateActivity::afterFinish() {
  for (const auto& name : removedFamilies) {
    if (!removedNames.empty()) removedNames += ", ";
    removedNames += name;
  }

  // finishRun() writes the ledger once and, if anything installed OR was
  // removed, re-discovers the registry and drops layout caches built with a
  // replaced active font.
  updater.finishRun();
  runFinished = true;
  LOG_INF("FONTUPD", "font sync done: %u updated, %u unchanged, %u removed, %u errors", updated, unchanged, removed,
          errors);
  if (removed != 0) LOG_INF("FONTUPD", "removed: %s", removedNames.c_str());
  if (skippedDeleted != 0) {
    LOG_INF("FONTUPD", "not downloaded, deleted by the owner: %s", skippedNames.c_str());
  }
  if (keptBundled != 0) {
    LOG_INF("FONTUPD", "not downloaded, the app's bundled copy is kept: %s", keptBundledNames.c_str());
  }
  if (errors != 0) writeFailureLog(updated, unchanged, removed, errors);
  RenderLock lock(*this);
  state = State::DONE;
  requestUpdate();
}

// What the progress frame would show now; see updprogress::Snapshot.
updprogress::Snapshot FontUpdateActivity::snapshot(unsigned long now) const {
  updprogress::Snapshot s;
  s.state = static_cast<uint8_t>(state);
  s.phase = state == State::CHECKING ? static_cast<uint8_t>(checkStep.load()) : static_cast<uint8_t>(updater.phase());
  s.item = static_cast<uint32_t>(currentFamily);
  s.file = static_cast<uint32_t>(updater.getCurrentFile());
  s.bytes = updater.getProcessedSize();
  s.elapsedSec = static_cast<uint32_t>((now - startMs) / 1000);
  s.stopping = stopRequested.load();
  return s;
}

// The one place a progress frame is asked for while the run works. Reached
// from the updater's callbacks (the loop task on the device, the worker on a
// host) and from loop()'s own heartbeat (which is what keeps the clock moving
// on a host while a single file's bytes are in flight).
void FontUpdateActivity::maybeRepaint(bool force) {
  if (state != State::CHECKING && state != State::SYNCING) return;
  std::lock_guard<std::mutex> guard(paintMutex);
  const unsigned long now = millis();
  const updprogress::Snapshot s = snapshot(now);
  if (!force && !updprogress::shouldRepaint(hasShown, shown, s, static_cast<uint32_t>(now - lastPaintMs))) return;
  shown = s;
  hasShown = true;
  lastPaintMs = now;
  // immediate=true: on the device this runs inside a download loop that will
  // not drain a deferred flag for us.
  UPD_TRACE_REQUESTED();
  requestUpdate(true);
}

void FontUpdateActivity::onProgress(void* ctx) { static_cast<FontUpdateActivity*>(ctx)->maybeRepaint(); }

void FontUpdateActivity::render(RenderLock&&) {
  UPD_TRACE_RENDERED();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_UPDATE_FONTS));

  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - lineHeight) / 2;

  switch (state) {
    case State::CHECKING: {
      // Two lines and a two-step bar, not one static line, plus the elapsed
      // clock: on a slow link the release request alone is seconds, and a clock
      // that moves is the difference between "working" and "hung". A bar rather
      // than a spinner because this is e-ink and an animation costs a panel
      // refresh per frame; the clock is throttled to one FAST refresh a second.
      const bool reading = checkStep.load() == FontUpdater::CheckStep::READING;
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_CHECKING_FOR_UPDATES), true, EpdFontFamily::BOLD);
      int y = top + lineHeight + metrics.verticalSpacing;
      renderer.drawCenteredText(UI_10_FONT_ID, y,
                                reading ? tr(STR_FONTS_READING_MANIFEST) : tr(STR_LIBRARY_CONTACTING));
      y += lineHeight + metrics.verticalSpacing;
      GUI.drawProgressBar(
          renderer,
          Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
          reading ? 1 : 0, 2);
      // Below the bar's own percentage label, which BaseTheme::drawProgressBar
      // draws at the bar's bottom + 15 in UI_10.
      y += metrics.progressBarHeight + 15 + lineHeight + metrics.verticalSpacing;
      drawStatusLine(renderer, y, startMs, stopRequested.load(), tr(STR_FONTS_STOPPING));
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
      // The same token as Update Library, so the same words: one credential for
      // one private repo, and telling the owner about a second one would be a
      // lie about how this is configured.
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
      const auto& family = updater.getFamilies()[currentFamily];
      const updprogress::Phase phase = updater.phase();
      const size_t fileCount = family.files.size();
      size_t fileIdx = updater.getCurrentFile();
      if (fileCount > 0 && fileIdx >= fileCount) fileIdx = fileCount - 1;
      const size_t total = updater.getTotalSize();
      const size_t processed = updater.getProcessedSize();
      // The bar is the WHOLE RUN. A family contributes only while its files are
      // DOWNLOADING (six files' worth, file by file) and all of itself once it
      // is installing: the hash of the files already on the card walks the same
      // file indices from the start, so counting it would run the bar forward
      // and then pull it back when the download began.
      // uint64_t like FontUpdater's own percent: `processed * 100` wraps a 32-bit
      // size_t above 42.9 MB.
      const unsigned int filePct =
          total > 0 ? static_cast<unsigned int>((static_cast<uint64_t>(processed) * 100) / total) : 0;
      unsigned int familyPct = 0;
      if (phase == updprogress::Phase::DOWNLOADING) {
        familyPct = fontsync::familyPercent(fileIdx, fileCount, filePct);
      } else if (phase == updprogress::Phase::INSTALLING || phase == updprogress::Phase::FINISHING) {
        familyPct = 100;
      }
      const unsigned int pct = fontsync::overallPercent(currentFamily, updater.getFamilies().size(), familyPct);

      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_FONTS_SYNCING), true, EpdFontFamily::BOLD);
      int y = top + lineHeight + metrics.verticalSpacing;
      char familyLine[48];
      snprintf(familyLine, sizeof(familyLine), tr(STR_FONTS_FAMILY_PROGRESS_FORMAT),
               static_cast<unsigned>(currentFamily + 1), static_cast<unsigned>(updater.getFamilies().size()));
      renderer.drawCenteredText(UI_10_FONT_ID, y, familyLine);
      y += lineHeight + metrics.verticalSpacing;
      renderer.drawCenteredText(UI_10_FONT_ID, y, family.name.c_str());
      y += lineHeight + metrics.verticalSpacing;

      // WHAT IT IS DOING, NAMED. "Checking Edgar_12 · 1.2 of 6.8 MB" while it
      // hashes what is already on the card, "Downloading ..." while bytes
      // arrive, the family's bytes in both. See UpdateProgress.h.
      char detail[96];
      detail[0] = '\0';
      if (phase == updprogress::Phase::CHECKING || phase == updprogress::Phase::DOWNLOADING) {
        char stem[40];
        char done[12];
        char all[12];
        updprogress::stemOf(fileCount > 0 ? family.files[fileIdx].file.c_str() : "", stem, sizeof(stem));
        updprogress::formatMb(updprogress::bytesDone(family.files, fileIdx, processed), done, sizeof(done));
        updprogress::formatMb(updprogress::bytesTotal(family.files), all, sizeof(all));
        snprintf(detail, sizeof(detail),
                 phase == updprogress::Phase::CHECKING ? tr(STR_UPDATE_CHECKING_FILE_FORMAT)
                                                       : tr(STR_UPDATE_DOWNLOADING_FILE_FORMAT),
                 stem, done, all);
      } else if (phase == updprogress::Phase::INSTALLING) {
        snprintf(detail, sizeof(detail), "%s", tr(STR_UPDATE_INSTALLING));
      } else if (phase == updprogress::Phase::FINISHING) {
        snprintf(detail, sizeof(detail), "%s", tr(STR_UPDATE_FINISHING));
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
      drawStatusLine(renderer, y, startMs, stopRequested.load(), tr(STR_FONTS_STOPPING));
      // A cancel is only useful if the reader knows it exists, and this is the
      // one screen in the family that runs for minutes.
      const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    // Stopped by the reader, not finished. It must NOT read as success: some
    // families installed and the rest are untouched, and the next run picks up
    // where this one left off.
    case State::CANCELED: {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_FONTS_STOPPED), true, EpdFontFamily::BOLD);
      char summary[64];
      snprintf(summary, sizeof(summary), tr(STR_FONTS_STOPPED_FORMAT), static_cast<unsigned>(nextFamily),
               static_cast<unsigned>(updater.getFamilies().size()));
      int y = top + lineHeight + metrics.verticalSpacing;
      renderer.drawCenteredText(UI_10_FONT_ID, y, summary);
      y += lineHeight + metrics.verticalSpacing;
      char counts[64];
      snprintf(counts, sizeof(counts), tr(STR_FONTS_SUMMARY_FORMAT), updated, unchanged, errors);
      renderer.drawCenteredText(UI_10_FONT_ID, y, counts);
      const Rect hintBounds{metrics.contentSidePadding, y + lineHeight + metrics.verticalSpacing,
                            pageWidth - metrics.contentSidePadding * 2,
                            pageHeight - (y + lineHeight + metrics.verticalSpacing)};
      UITheme::drawCenteredWrappedText(renderer, hintBounds, SMALL_FONT_ID, tr(STR_FONTS_STOPPED_HINT), 2, true,
                                       EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    case State::DONE: {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_COMPLETE), true, EpdFontFamily::BOLD);
      char summary[64];
      snprintf(summary, sizeof(summary), tr(STR_FONTS_SUMMARY_FORMAT), updated, unchanged, errors);
      int y = top + lineHeight + metrics.verticalSpacing;
      renderer.drawCenteredText(UI_10_FONT_ID, y, summary);
      // WHAT WAS DELETED, BY NAME. This is the only destructive thing the
      // screen does, and a bare count would be the "22 errors" mistake again --
      // the owner needs to see WHICH family left the card, because a sideloaded
      // one he put there himself is exactly the case the ruling accepted.
      // snprintf truncates a long list rather than growing the line; the log
      // carries every name in full.
      if (removed != 0) {
        y += lineHeight + metrics.verticalSpacing;
        char removedLine[64];
        snprintf(removedLine, sizeof(removedLine), tr(STR_FONTS_REMOVED_FORMAT), removedNames.c_str());
        renderer.drawCenteredText(SMALL_FONT_ID, y, removedLine);
      }
      // A count with no noun sent the owner to debug Wi-Fi once already
      // (2026-09-06, Update Library). One more line names the thing to fix.
      const fontsync::FailureKind why = fontsync::dominantFailure(storageErrors, networkErrors, verifyErrors);
      if (why != fontsync::FailureKind::NONE) {
        y += lineHeight + metrics.verticalSpacing;
        const char* hint = why == fontsync::FailureKind::STORAGE   ? tr(STR_FONTS_ERRORS_STORAGE)
                           : why == fontsync::FailureKind::NETWORK ? tr(STR_LIBRARY_ERRORS_NETWORK)
                                                                   : tr(STR_FONTS_ERRORS_VERIFY);
        renderer.drawCenteredText(SMALL_FONT_ID, y, hint);
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
