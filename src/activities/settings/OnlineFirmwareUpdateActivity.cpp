#include "OnlineFirmwareUpdateActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "WifiCredentialStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/OtaPreflight.h"

void OnlineFirmwareUpdateActivity::onEnter() {
  Activity::onEnter();
  LOG_INF("OTA", "OnlineFirmwareUpdateActivity build=%s %s", __DATE__, __TIME__);

  // THE SCREEN NOW BRINGS THE NETWORK UP ITSELF (owner 2026-08-28). It used to
  // answer "not connected" with "go to Settings", which sent the owner to
  // another screen to supply a network the device had already saved. If there
  // is a last-used SSID with a stored credential, join it here.
  //
  // And even when the link is ALREADY up we still go through CONNECTING, which
  // is the half that fixes "github.com not found": WL_CONNECTED means
  // ASSOCIATED, not resolvable. The pre-flight waits for the resolver before
  // any fetch starts. See network/OtaPreflight.h.
  beginMs = millis();
  linkMs = 0;
  haveCredential = false;
  joiningSsid.clear();

  if (WiFi.status() != WL_CONNECTED) {
    const std::string last = WIFI_STORE.getLastConnectedSsid();
    if (!last.empty()) {
      if (const auto cred = WIFI_STORE.findCredential(last)) {
        joiningSsid = cred->ssid;
        haveCredential = true;
        LOG_INF("OTA", "No link; rejoining last network \"%s\"", joiningSsid.c_str());
        WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
      }
    }
    if (!haveCredential) {
      LOG_INF("OTA", "No link and no saved network to rejoin");
      state = State::NO_WIFI;
      requestUpdate();
      return;
    }
  } else {
    haveCredential = true;  // already associated; nothing to join
    linkMs = millis();
  }

  state = State::CONNECTING;
  requestUpdate();
}

// Waits for BOTH conditions the check needs: an association, and a resolver
// that answers for the host the check will actually use. Returns true when the
// screen may proceed; sets a terminal state and returns false when it may not.
bool OnlineFirmwareUpdateActivity::preflight() {
  const bool linkUp = WiFi.status() == WL_CONNECTED;
  if (linkUp && linkMs == 0) {
    linkMs = millis();
    LOG_INF("OTA", "Associated%s%s; waiting for the resolver", joiningSsid.empty() ? "" : " with ",
            joiningSsid.c_str());
  }

  // Only ask the resolver once the link is up, and no more often than the
  // retry gap: hostByName() blocks, and asking with no link just burns the
  // timeout. The host is the one the check fetches, because resolving any
  // other proves nothing about this record.
  bool dnsOk = false;
  if (linkUp) {
    const uint32_t now = millis();
    if (lastDnsTryMs == 0 || now - lastDnsTryMs >= otapreflight::kDnsRetryMs) {
      lastDnsTryMs = now;
      IPAddress addr;
      dnsOk = WiFi.hostByName(OtaUpdater::releaseHost(), addr) == 1;
      if (dnsOk) LOG_INF("OTA", "Resolver answered for %s", OtaUpdater::releaseHost());
    } else {
      dnsOk = dnsResolved;
    }
    if (dnsOk) dnsResolved = true;
  }

  const uint32_t sinceBegin = millis() - beginMs;
  const uint32_t sinceLink = linkMs == 0 ? 0 : millis() - linkMs;
  const otapreflight::Phase phase = otapreflight::decide(haveCredential, linkUp, dnsResolved, sinceBegin, sinceLink);

  switch (phase) {
    case otapreflight::Phase::Ready:
      return true;
    case otapreflight::Phase::Connecting:
    case otapreflight::Phase::Resolving:
      return false;
    case otapreflight::Phase::NoCredential:
      state = State::NO_WIFI;
      break;
    case otapreflight::Phase::ConnectFailed:
      LOG_ERR("OTA", "Could not join \"%s\" in %u ms", joiningSsid.c_str(), (unsigned)otapreflight::kConnectTimeoutMs);
      state = State::NO_WIFI;
      break;
    case otapreflight::Phase::DnsFailed:
      // Associated but nothing resolves. Reported as its own thing rather than
      // as "GitHub is unreachable", because the network is the part that is
      // wrong and saying so is what makes it fixable.
      LOG_ERR("OTA", "Associated but no DNS after %u ms", (unsigned)otapreflight::kDnsTimeoutMs);
      errorMessage = tr(STR_UPDATE_CHECK_FAILED);
      state = State::FAILED;
      break;
  }
  requestUpdate();
  return false;
}

void OnlineFirmwareUpdateActivity::loop() {
  // First pass after the CHECKING frame is on screen. Doing this in onEnter()
  // would block before anything had been painted, so the owner would stare at
  // the previous screen for the length of a TLS handshake.
  if (state == State::CONNECTING) {
    if (!preflight()) return;
    state = State::CHECKING;
    requestUpdate();
    return;
  }

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
    case State::CONNECTING:
      // Deliberately reuses the "checking" wording rather than adding a string
      // for a state that is usually gone in under a second. What the owner
      // needs to know is that the screen is working, not which of two network
      // preconditions it is on; the [OTA] log carries that distinction for
      // anyone diagnosing it.
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_CHECKING_FOR_UPDATES));
      break;

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
