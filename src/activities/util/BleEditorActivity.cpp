// SPIKE — throwaway. See BleEditorActivity.h.
#include "BleEditorActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>

#include "components/UITheme.h"
#include "fontIds.h"
#include "spike/BleHidHost.h"

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

  blespike::begin();
  logHeap("P2-after-ble-init");

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

  bool got = false;
  for (int c = blespike::popChar(); c >= 0; c = blespike::popChar()) {
    if (!buf) break;
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
  snprintf(header, sizeof(header), "BLE %s%s%s", blespike::stateName(), blespike::peerName()[0] != 0 ? " - " : "",
           blespike::peerName());
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, header);

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
  GUI.drawButtonHints(renderer, "Save+Exit", "", status, "");

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
