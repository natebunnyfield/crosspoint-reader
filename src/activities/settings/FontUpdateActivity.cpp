#include "FontUpdateActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstdint>
#include <cstdio>

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
}  // namespace

void FontUpdateActivity::onEnter() {
  Activity::onEnter();

  // Joining a network is Settings' job, and saying so beats a generic failure
  // after a timeout.
  if (WiFi.status() != WL_CONNECTED) {
    state = State::NO_WIFI;
    requestUpdate();
    return;
  }

  state = State::CHECKING;
  // WAIT for this paint, do not merely request it. See the header.
  requestUpdateAndWait();
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
  const bool backPressed = mappedInput.wasPressed(MappedInputManager::Button::Back);

  // First pass after the CHECKING frame is on screen -- onEnter waited for it.
  if (state == State::CHECKING && !checkStarted) {
    checkStarted = true;
    runCheck();
    return;
  }

  // ONE FAMILY PER TICK, then back to the main loop. See the header.
  if (state == State::SYNCING) {
    // THE CANCEL POINT, and the only one. This tick begins after the previous
    // family's commit finished and before the next one starts, so a cancel
    // here can never catch a family mid-install -- the two-rename commit has
    // already either happened or been rolled back. Owner ruling 2026-09-07.
    if (backPressed) {
      cancelSync();
      return;
    }
    syncNextFamily();
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

void FontUpdateActivity::cancelSync() {
  // finishRun() STILL RUNS. Families that committed before the cancel are
  // installed, so the ledger has to record them and the registry has to be
  // re-discovered -- skipping it would leave a newly installed family absent
  // from the picker until reboot and make the next run hash it all again.
  // It is the same call the DONE path makes, for the same reasons.
  updater.finishRun();
  LOG_INF("FONTUPD", "font sync stopped by the reader after %u of %u families: %u updated, %u unchanged, %u errors",
          static_cast<unsigned>(nextFamily), static_cast<unsigned>(updater.getFamilies().size()), updated, unchanged,
          errors);
  RenderLock lock(*this);
  state = State::CANCELED;
  requestUpdate();
}

void FontUpdateActivity::runCheck() {
  // Repaint between the check's network steps. immediate=true for the same
  // reason the per-file progress callback uses it: this runs inside a blocking
  // call that will not drain the flag for us.
  auto stepCb = +[](void* ctx, FontUpdater::CheckStep step) {
    auto* self = static_cast<FontUpdateActivity*>(ctx);
    // The frame onEnter waited for already names the first step, so the
    // CONTACTING callback has nothing to add; repainting it anyway costs a
    // second identical refresh on top of the TLS handshake. checkStep has one
    // writer, this task, so reading it here needs no lock.
    if (step == self->checkStep) return;
    {
      RenderLock lock(*self);
      self->checkStep = step;
    }
    self->requestUpdate(true);
  };
  const FontUpdater::FontError err = updater.fetchManifest(stepCb, this);

  if (err == FontUpdater::NO_TOKEN) {
    LOG_INF("FONTUPD", "no GitHub token configured");
    RenderLock lock(*this);
    state = State::NO_TOKEN;
    requestUpdate();
    return;
  }

  if (err != FontUpdater::OK) {
    LOG_ERR("FONTUPD", "manifest check failed (%d)", static_cast<int>(err));
    // Four distinct causes, four distinct sentences, for the reason the library
    // screen learned to separate them: "check failed" sends the owner to debug
    // Wi-Fi over a manifest GitHub served perfectly.
    errorMessage = err == FontUpdater::NO_RELEASE         ? tr(STR_FONTS_NO_RELEASE)
                   : err == FontUpdater::NO_REPO_ACCESS   ? tr(STR_LIBRARY_NO_REPO_ACCESS)
                   : err == FontUpdater::BAD_TOKEN        ? tr(STR_LIBRARY_BAD_TOKEN)
                   : err == FontUpdater::MANIFEST_TOO_NEW ? tr(STR_FONTS_MANIFEST_TOO_NEW)
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

  {
    RenderLock lock(*this);
    state = State::SYNCING;
  }
  // Waited for, like the CHECKING frame and for the same reason: the next tick
  // blocks on the first family, and "Font 1 of N" over a bar at zero must be on
  // the panel before it does.
  requestUpdateAndWait();
}

void FontUpdateActivity::syncNextFamily() {
  const auto& fonts = updater.getFamilies();
  if (nextFamily >= fonts.size()) {
    // REMOVAL RUNS HERE: after every family has been offered, and only on the
    // path that reaches the summary. Two orderings were possible and this one
    // is deliberate.
    //
    // Removing FIRST would free card space before ~80 MB of downloads, which is
    // the only argument for it. It also means a run that then fails, or that
    // the reader stops, has destroyed families and installed nothing -- the
    // worst trade this screen could make with the owner's data. Removing last
    // costs nothing but disk headroom on a card that was already holding both
    // sets a moment ago.
    //
    // A CANCEL NEVER REMOVES: cancelSync() is a different exit and does not
    // call this. Owner ruling 2026-09-07, and it follows from the same
    // reasoning -- a reader who stopped the run did not ask for a mirror.
    removed = static_cast<unsigned>(updater.removeUnlistedFamilies(removedFamilies));
    for (const auto& name : removedFamilies) {
      if (!removedNames.empty()) removedNames += ", ";
      removedNames += name;
    }

    // finishRun() writes the ledger once and, if anything installed OR was
    // removed, re-discovers the registry and drops layout caches built with a
    // replaced active font.
    updater.finishRun();
    LOG_INF("FONTUPD", "font sync done: %u updated, %u unchanged, %u removed, %u errors", updated, unchanged, removed,
            errors);
    if (removed != 0) LOG_INF("FONTUPD", "removed: %s", removedNames.c_str());
    RenderLock lock(*this);
    state = State::DONE;
    requestUpdate();
    return;
  }

  auto progressCb = +[](void* ctx) {
    auto* self = static_cast<FontUpdateActivity*>(ctx);
    // immediate=true: this runs inside a download loop that will not drain the
    // flag for us.
    self->requestUpdate(true);
  };

  const size_t i = nextFamily++;
  {
    RenderLock lock(*this);
    currentFamily = i;
    lastRenderedPercent = 101;
    updater.resetFamilyProgress();  // before the repaint below can read them
  }
  requestUpdate(true);
  switch (updater.syncFamily(i, progressCb, this)) {
    case FontUpdater::FamilyResult::ADDED:
    case FontUpdater::FamilyResult::UPDATED:
      updated++;
      break;
    case FontUpdater::FamilyResult::UNCHANGED:
      unchanged++;
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

void FontUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_UPDATE_FONTS));

  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - lineHeight) / 2;

  switch (state) {
    case State::CHECKING: {
      // Two lines and a two-step bar, not one static line: the whole check runs
      // inside one loop() call, so a single frozen line reads as a hang. A bar
      // rather than a spinner because this is e-ink and an animation costs a
      // panel refresh per frame.
      const bool reading = checkStep == FontUpdater::CheckStep::READING;
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_CHECKING_FOR_UPDATES), true, EpdFontFamily::BOLD);
      int y = top + lineHeight + metrics.verticalSpacing;
      renderer.drawCenteredText(UI_10_FONT_ID, y,
                                reading ? tr(STR_FONTS_READING_MANIFEST) : tr(STR_LIBRARY_CONTACTING));
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
      const size_t total = updater.getTotalSize();
      const size_t processed = updater.getProcessedSize();
      // The current FILE's own progress, then this FAMILY's share of its six
      // cuts, then the whole run's. Three levels because a family is a
      // multi-megabyte download and the two coarser bars alone would sit still
      // for a minute at a time.
      // uint64_t like FontUpdater's own percent: `processed * 100` wraps a 32-bit
      // size_t above 42.9 MB. No shipped cut is near that, and the multiplication
      // is still wrong if one ever is.
      const unsigned int filePct =
          total > 0 ? static_cast<unsigned int>((static_cast<uint64_t>(processed) * 100) / total) : 0;
      const unsigned int familyPct = fontsync::familyPercent(updater.getCurrentFile(), updater.getFileCount(), filePct);
      const unsigned int pct = fontsync::overallPercent(currentFamily, updater.getFamilies().size(), familyPct);
      // Once per percent, same e-ink reasoning as the OTA and Library screens.
      if (pct == lastRenderedPercent) return;
      lastRenderedPercent = pct;

      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_FONTS_SYNCING), true, EpdFontFamily::BOLD);
      int y = top + lineHeight + metrics.verticalSpacing;
      char familyLine[48];
      snprintf(familyLine, sizeof(familyLine), tr(STR_FONTS_FAMILY_PROGRESS_FORMAT),
               static_cast<unsigned>(currentFamily + 1), static_cast<unsigned>(updater.getFamilies().size()));
      renderer.drawCenteredText(UI_10_FONT_ID, y, familyLine);
      y += lineHeight + metrics.verticalSpacing;
      const std::string& name = updater.getFamilies()[currentFamily].name;
      renderer.drawCenteredText(UI_10_FONT_ID, y, name.c_str());
      y += lineHeight + metrics.verticalSpacing;
      GUI.drawProgressBar(
          renderer,
          Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
          static_cast<int>(pct), 100);
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
