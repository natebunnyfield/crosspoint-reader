#include "OnlineFirmwareUpdateActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void OnlineFirmwareUpdateActivity::onEnter() {
  Activity::onEnter();
  LOG_INF("OTA", "OnlineFirmwareUpdateActivity build=%s %s", __DATE__, __TIME__);

  // The check needs a connection and this screen does not offer to make one:
  // joining a network is Settings' job and it already has a whole screen for it.
  // Saying so is more useful than a generic failure after a 30 s timeout.
  if (WiFi.status() != WL_CONNECTED) {
    state = State::NO_WIFI;
    requestUpdate();
    return;
  }

  state = State::CHECKING;
  requestUpdate();
}

void OnlineFirmwareUpdateActivity::loop() {
  // First pass after the CHECKING frame is on screen. Doing this in onEnter()
  // would block before anything had been painted, so the owner would stare at
  // the previous screen for the length of a TLS handshake.
  if (state == State::CHECKING && !checkStarted) {
    checkStarted = true;
    runCheck();
    return;
  }

  if (state == State::CONFIRMING) {
    confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); });
    return;
  }

  int x = 0;
  int y = 0;
  const bool dismissed = mappedInput.wasPressed(MappedInputManager::Button::Back) ||
                         mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
                         mappedInput.wasScreenTapped(x, y);
  if (dismissed && (state == State::FAILED || state == State::UP_TO_DATE || state == State::NO_WIFI)) {
    finish();
  }
}

const char* OnlineFirmwareUpdateActivity::messageForError(OtaUpdater::OtaUpdaterError err) const {
  switch (err) {
    case OtaUpdater::HTTP_ERROR:
      return tr(STR_UPDATE_CHECK_FAILED);
    case OtaUpdater::JSON_PARSE_ERROR:
      return tr(STR_UPDATE_CHECK_FAILED);
    // Reached GitHub; GitHub says this repo has published nothing. Saying
    // "could not reach" here sends someone to debug their Wi-Fi over a release
    // that was never cut.
    case OtaUpdater::NO_RELEASE:
      return tr(STR_UPDATE_NO_RELEASE);
    case OtaUpdater::WRONG_DEVICE_ERROR:
      return tr(STR_FIRMWARE_WRONG_DEVICE);
    case OtaUpdater::OOM_ERROR:
      return tr(STR_UPDATE_FAILED);
    default:
      return tr(STR_UPDATE_FAILED);
  }
}

void OnlineFirmwareUpdateActivity::runCheck() {
  const OtaUpdater::OtaUpdaterError err = updater.checkForUpdate();

  // NO_UPDATE is not a failure, and treating it as one is the most common
  // outcome of all: it is what checkForUpdate() returns when the newest release
  // has no firmware.bin asset, and it is what the simulator returns by default.
  // Reported as "Update failed" it would tell the owner something is broken
  // every time nothing is broken.
  if (err == OtaUpdater::NO_UPDATE) {
    LOG_INF("OTA", "no newer release published");
    RenderLock lock(*this);
    state = State::UP_TO_DATE;
    requestUpdate();
    return;
  }

  if (err != OtaUpdater::OK) {
    LOG_ERR("OTA", "check failed (%d)", static_cast<int>(err));
    errorMessage = messageForError(err);
    RenderLock lock(*this);
    state = State::FAILED;
    requestUpdate();
    return;
  }

  if (!updater.isUpdateNewer()) {
    LOG_INF("OTA", "already on the latest release");
    RenderLock lock(*this);
    state = State::UP_TO_DATE;
    requestUpdate();
    return;
  }

  latestVersion = updater.getLatestVersion();
  total = updater.getOtaSize();
  LOG_INF("OTA", "update available: %s (%u bytes)", latestVersion.c_str(), static_cast<unsigned>(total));
  promptConfirmation();
}

void OnlineFirmwareUpdateActivity::promptConfirmation() {
  {
    RenderLock lock(*this);
    state = State::CONFIRMING;
  }
  const char* options[] = {tr(STR_CANCEL), tr(STR_CONFIRM)};
  confirmPopup.show(tr(STR_FIRMWARE_UPDATE_PROMPT), options, 2, 0, [this](int idx) {
    if (idx != 1) {
      finish();
      return;
    }
    {
      RenderLock lock(*this);
      state = State::UPDATING;
      processed = 0;
      lastRenderedPercent = 101;
    }
    requestUpdateAndWait();
    performUpdate();
  });
  // The version is the one thing worth reading before saying yes.
  confirmPopup.setInfoLines({latestVersion});
  requestUpdate();
}

void OnlineFirmwareUpdateActivity::performUpdate() {
  LOG_INF("OTA", "installing %s", latestVersion.c_str());

  auto progressCb = +[](void* ctx) {
    auto* self = static_cast<OnlineFirmwareUpdateActivity*>(ctx);
    self->processed = self->updater.getProcessedSize();
    self->total = self->updater.getTotalSize();
    // immediate=true: this runs in a tight download loop, so the main loop is
    // not going to drain the flag for us.
    self->requestUpdate(true);
  };

  // Everything protective lives inside installUpdate: the inactive slot, the
  // chip_id guard before the first write, esp_ota_end()'s verification, and only
  // then the boot switch. A failure at any point leaves the running firmware
  // exactly where it was.
  const OtaUpdater::OtaUpdaterError err = updater.installUpdate(progressCb, this);
  if (err != OtaUpdater::OK) {
    LOG_ERR("OTA", "install failed (%d)", static_cast<int>(err));
    errorMessage = messageForError(err);
    RenderLock lock(*this);
    state = State::FAILED;
    requestUpdate();
    return;
  }

  LOG_INF("OTA", "install complete, restarting into the new image");
  {
    RenderLock lock(*this);
    state = State::SUCCESS;
  }
  requestUpdateAndWait();
  delay(1500);
  ESP.restart();
}

void OnlineFirmwareUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_UPDATE_FIRMWARE));

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

    case State::UP_TO_DATE: {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UP_TO_DATE), true, EpdFontFamily::BOLD);
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    case State::UPDATING: {
      const unsigned int pct = total > 0 ? static_cast<unsigned int>((processed * 100) / total) : 0;
      // Once per percent. E-ink cannot repaint faster and the render task's
      // framebuffer work contends with TLS for heap while the download runs.
      if (pct == lastRenderedPercent) return;
      lastRenderedPercent = pct;

      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATING), true, EpdFontFamily::BOLD);
      int y = top + lineHeight + metrics.verticalSpacing;
      GUI.drawProgressBar(
          renderer,
          Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
          static_cast<int>(pct), 100);
      y += metrics.progressBarHeight + metrics.verticalSpacing;
      y += lineHeight + metrics.verticalSpacing;
      renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_FIRMWARE_UPDATE_DO_NOT_POWER_OFF));
      break;
    }

    case State::SUCCESS: {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_COMPLETE), true, EpdFontFamily::BOLD);
      const int hintY = top + lineHeight + metrics.verticalSpacing;
      const Rect hintBounds{metrics.contentSidePadding, hintY, pageWidth - metrics.contentSidePadding * 2,
                            pageHeight - hintY};
      UITheme::drawCenteredWrappedText(renderer, hintBounds, UI_10_FONT_ID, tr(STR_RESTARTING_HINT), 3, true,
                                       EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
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

    case State::CONFIRMING:
      if (confirmPopup.processRender(renderer, mappedInput)) return;
      break;
  }

  renderer.displayBuffer();
}
