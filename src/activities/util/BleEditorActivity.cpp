// SPIKE — throwaway. See BleEditorActivity.h.
#include "BleEditorActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>

#include "components/UITheme.h"
#include "fontIds.h"
#include "spike/BleHidHost.h"
#include "spike/ClaudeChat.h"

namespace {
constexpr const char* TAG = "BLEEDIT";
constexpr int EDITOR_FONT_ID = UI_10_FONT_ID;
constexpr const char* SAVE_PATH = "/notes-spike.txt";

void logHeap(const char* point) {
  LOG_INF(TAG, "SPIKE-HEAP %s free=%u minfree=%u maxalloc=%u", point, (unsigned)ESP.getFreeHeap(),
          (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}
}  // namespace

void BleEditorActivity::onEnter() {
  Activity::onEnter();

  logHeap("P1-editor-enter-before-ble");

  // BLE FIRST, buffer second. nimble_port_init() wants ~65 KB and a large
  // contiguous block; at ~86 KB free / ~73 KB maxalloc it is nondeterministic —
  // observed both succeeding and HARD-HANGING the device (no panic, no reboot,
  // USB CDC still enumerated but the firmware answers nothing). Allocating the
  // editor's 8 KB first made that boundary worse for no reason.
  blespike::begin();
  logHeap("P2-after-ble-init");

  buf = makeUniqueNoThrow<char[]>(BUF_SIZE);
  if (!buf) {
    LOG_ERR(TAG, "OOM: %u bytes", (unsigned)BUF_SIZE);
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  lineHeight = renderer.getLineHeight(EDITOR_FONT_ID);
  if (lineHeight < 1) lineHeight = 1;
  contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  maxLines = (contentBottom - contentTop) / lineHeight;
  if (maxLines < 1) maxLines = 1;
  if (maxLines > static_cast<int>(MAX_TRACKED_LINES)) maxLines = static_cast<int>(MAX_TRACKED_LINES);
  maxWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2;

  logHeap("P2b-after-editor-buffer");

  layout();
  requestUpdate();
}

void BleEditorActivity::onExit() {
  save();
  blespike::end();
  buf.reset();
  logHeap("P5-after-ble-teardown");
  Activity::onExit();
}

void BleEditorActivity::loop() {
  // No mappedInput.update() here: main.cpp's loop() already ran gpio.update()
  // this tick, and a second call would clear the very edges we test for.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  // Confirm = send the buffer to Claude (ruling: front button, keyboard stays
  // purely for text). Blocks this loop for the whole exchange — spike quality.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && buf && len > 0) {
    sendToClaude();
    return;
  }

  bool got = false;
  for (int c = blespike::popChar(); c >= 0; c = blespike::popChar()) {
    if (!buf) break;
    if (!lastResponse.empty()) lastResponse.clear();  // typing resumes: dismiss the response view
    const uint32_t stamp = blespike::lastKeyMillis();
    if (!got && pendingChars == 0) pendingKeyStampMs = stamp;
    got = true;

    if (c == '\b') {
      if (len > 0) --len;
    } else if (len < BUF_SIZE - 1) {
      buf[len++] = static_cast<char>(c);
    }
    ++pendingChars;
  }

  if (got) {
    lastKeyMs = millis();
    dirty = true;
    const uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < heapMin) heapMin = freeHeap;
  }

  // E-ink: one refresh per burst. Flush early if the burst is long enough that
  // the typist is already ahead of the screen.
  if (dirty && (pendingChars >= FLUSH_CHARS || millis() - lastKeyMs >= DEBOUNCE_MS)) {
    dirty = false;
    pendingChars = 0;
    renderKeyStampMs = pendingKeyStampMs;
    layout();
    requestUpdate();
  }
}

void BleEditorActivity::layout() {
  lineTotal = 0;
  if (!buf) return;

  char probe[192];
  size_t i = 0;
  while (true) {
    const size_t start = i;
    size_t lastBreak = 0;  // offset (exclusive) of the last word boundary that fit
    size_t j = i;
    bool hardBreak = false;

    while (j < len) {
      if (buf[j] == '\n') {
        hardBreak = true;
        break;
      }
      const size_t candidate = j + 1 - start;
      if (candidate < sizeof(probe)) {
        memcpy(probe, buf.get() + start, candidate);
        probe[candidate] = '\0';
        if (renderer.getTextWidth(EDITOR_FONT_ID, probe) > maxWidth) break;
      } else {
        break;
      }
      if (buf[j] == ' ') lastBreak = j + 1;
      ++j;
    }

    size_t end = j;
    if (!hardBreak && j < len && lastBreak > start) end = lastBreak;  // wrap on the word boundary

    const size_t slot = lineTotal % MAX_TRACKED_LINES;
    lineStart[slot] = static_cast<uint16_t>(start);
    lineEnd[slot] = static_cast<uint16_t>(end);
    ++lineTotal;

    i = hardBreak ? end + 1 : end;
    if (i >= len) break;
  }
  if (lineTotal == 0) lineTotal = 1;  // always show the (empty) caret line
}

void BleEditorActivity::render(RenderLock&&) {
  const uint32_t renderStart = millis();
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  char header[64];
  if (phaseText[0] != 0) {
    snprintf(header, sizeof(header), "Claude: %s", phaseText);
  } else {
    snprintf(header, sizeof(header), "BLE %s%s%s", blespike::stateName(), blespike::peerName()[0] != 0 ? " - " : "",
             blespike::peerName());
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, header);

  // Response view: shown after an exchange until the next keystroke.
  if (!lastResponse.empty()) {
    const auto respLines = renderer.wrappedText(EDITOR_FONT_ID, lastResponse.c_str(), maxWidth, maxLines);
    for (size_t n = 0; n < respLines.size(); ++n) {
      renderer.drawText(EDITOR_FONT_ID, metrics.contentSidePadding, contentTop + static_cast<int>(n) * lineHeight,
                        respLines[n].c_str());
    }
    GUI.drawButtonHints(renderer, "Save+Exit", "", "response in /claude-chat.md - type to continue", "");
    renderer.displayBuffer();
    return;
  }

  const size_t shown = lineTotal < static_cast<size_t>(maxLines) ? lineTotal : static_cast<size_t>(maxLines);
  const size_t firstLine = lineTotal - shown;

  char lineBuf[192];
  for (size_t n = 0; n < shown; ++n) {
    const size_t slot = (firstLine + n) % MAX_TRACKED_LINES;
    size_t s = lineStart[slot];
    size_t e = lineEnd[slot];
    if (e > len) e = len;
    size_t count = e > s ? e - s : 0;
    if (count > sizeof(lineBuf) - 2) count = sizeof(lineBuf) - 2;
    if (buf && count > 0) memcpy(lineBuf, buf.get() + s, count);
    // Caret on the last line so the typist can see where they are.
    if (n + 1 == shown) lineBuf[count++] = '_';
    lineBuf[count] = '\0';
    renderer.drawText(EDITOR_FONT_ID, metrics.contentSidePadding, contentTop + static_cast<int>(n) * lineHeight,
                      lineBuf);
  }

  char status[96];
  snprintf(status, sizeof(status), "%u ch  heap %u  notif %u  drop %u  lat %ums", (unsigned)len,
           (unsigned)ESP.getFreeHeap(), (unsigned)blespike::notifyCount(), (unsigned)blespike::droppedCount(),
           (unsigned)lastLatencyMs);
  GUI.drawButtonHints(renderer, "Save+Exit", len > 0 ? "Send" : "", status, "");

  renderer.displayBuffer();

  if (renderKeyStampMs != 0) {
    lastLatencyMs = millis() - renderKeyStampMs;
    LOG_INF(TAG, "SPIKE-LATENCY key->glyph=%ums (render=%ums) chars=%u heap=%u", (unsigned)lastLatencyMs,
            (unsigned)(millis() - renderStart), (unsigned)len, (unsigned)ESP.getFreeHeap());
    renderKeyStampMs = 0;
  }
  if (heapMin != UINT32_MAX) {
    LOG_INF(TAG, "SPIKE-HEAP P4-typing minfree-observed=%u free=%u", (unsigned)heapMin, (unsigned)ESP.getFreeHeap());
  }
}

void BleEditorActivity::setPhase(const char* phase) {
  snprintf(phaseText, sizeof(phaseText), "%s", phase);
  LOG_INF(TAG, "phase: %s", phase);
  // Paint synchronously so each phase is visible even though the exchange
  // blocks this loop() the whole time.
  requestUpdateAndWait();
}

void BleEditorActivity::sendToClaude() {
  std::string prompt(buf.get(), len);

  // The radios take turns: ~72 KB comes back from BLE, TLS wants ~40-50 KB.
  setPhase("ble: shutting down");
  blespike::end();

  claudechat::Result r = claudechat::runExchange(
      prompt, [](void* ctx, const char* p) { static_cast<BleEditorActivity*>(ctx)->setPhase(p); }, this);

  // Re-arm the keyboard. Known risk from the earlier spike runs: below ~86 KB
  // free this can hard-hang; heap here should be ~88 KB+ with WiFi fully off.
  setPhase("ble: restarting");
  blespike::begin();

  if (r.ok) {
    lastResponse = std::move(r.responseText);
    len = 0;  // prompt consumed; /claude-chat.md is the durable record
  } else {
    lastResponse = "ERROR: " + r.error;  // buffer kept so the prompt can be retried
  }
  phaseText[0] = '\0';
  layout();
  requestUpdate();
}

void BleEditorActivity::save() {
  if (!buf || len == 0) {
    LOG_INF(TAG, "nothing to save");
    return;
  }
  HalFile f;
  if (!Storage.openFileForWrite(TAG, SAVE_PATH, f)) {
    LOG_ERR(TAG, "cannot open %s for write", SAVE_PATH);
    return;
  }
  const size_t written = f.write(reinterpret_cast<const uint8_t*>(buf.get()), len);
  f.flush();
  // No explicit close: DESTRUCTOR_CLOSES_FILE=1 releases the local at scope exit.
  LOG_INF(TAG, "saved %u/%u bytes to %s", (unsigned)written, (unsigned)len, SAVE_PATH);
}
