#include "ClaudeChatActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "components/UITheme.h"
#include "fontIds.h"
#include "CrossPointSettings.h"
#include "spike/BleHidHost.h"
#include "spike/EditorFonts.h"
#include "spike/ClaudeChat.h"
#include "spike/HidKeymap.h"
#include "spike/MarkdownSpans.h"

namespace {
constexpr const char* TAG = "CLAUDEUI";
constexpr int CHAT_FONT_ID_FALLBACK = UI_10_FONT_ID;

// The Editor Font setting picks from the editor-group families (see
// src/spike/EditorFonts.h). If that family is not installed on the card the UI
// face stands in, so the screen is never blank because of a missing font.
int resolveEditorFont() {
  const char* family = editorfonts::selectedFamily(SETTINGS.editorFont);
  if (SETTINGS.sdFontIdResolver != nullptr) {
    const int id = SETTINGS.sdFontIdResolver(SETTINGS.sdFontResolverCtx, family, 12);
    if (id != 0) return id;
  }
  return CHAT_FONT_ID_FALLBACK;
}
}  // namespace

void ClaudeChatActivity::onEnter() {
  Activity::onEnter();

  // BLE first: nimble_port_init() wants ~65 KB contiguous and hangs rather than
  // failing when the heap is fragmented, so claim it while the heap is widest.
  blespike::begin();

  storage = makeUniqueNoThrow<char[]>(BUF_SIZE);
  if (storage) buf = makeUniqueNoThrow<textbuf::TextBuffer>(storage.get(), BUF_SIZE);
  if (!buf) LOG_ERR(TAG, "OOM: %u byte prompt buffer", (unsigned)BUF_SIZE);

  editorFontId = resolveEditorFont();
  const auto& metrics = UITheme::getInstance().getMetrics();
  lineHeight = renderer.getLineHeight(editorFontId);
  if (lineHeight < 1) lineHeight = 1;
  contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  maxLines = (contentBottom - contentTop) / lineHeight;
  if (maxLines < 1) maxLines = 1;
  maxWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2;

  requestUpdate();
}

void ClaudeChatActivity::onExit() {
  blespike::end();
  buf.reset();
  storage.reset();
  Activity::onExit();
}

void ClaudeChatActivity::setPhase(const char* phase) {
  snprintf(phaseText, sizeof(phaseText), "%s", phase);
  view = View::Working;
  LOG_INF(TAG, "phase: %s", phase);
  requestUpdateAndWait();  // the exchange blocks loop(); paint each phase
}

void ClaudeChatActivity::layoutAnswer() {
  answerLines.clear();
  answerTop = 0;
  if (answer.empty()) return;
  // Split on newlines first so markdown structure survives, then soft-wrap.
  size_t start = 0;
  while (start <= answer.size()) {
    size_t nl = answer.find('\n', start);
    if (nl == std::string::npos) nl = answer.size();
    const std::string para = answer.substr(start, nl - start);
    if (para.empty()) {
      answerLines.emplace_back();
    } else {
      const auto wrapped = renderer.wrappedText(editorFontId, para.c_str(), maxWidth, 200);
      for (const auto& w : wrapped) answerLines.push_back(w);
    }
    if (nl >= answer.size()) break;
    start = nl + 1;
  }
}

void ClaudeChatActivity::handleKey(int key) {
  if (!buf) return;
  switch (key) {
    case hidkeymap::KEY_LEFT:
      buf->cursorLeft();
      break;
    case hidkeymap::KEY_RIGHT:
      buf->cursorRight();
      break;
    case hidkeymap::KEY_UP:
      buf->cursorUp();
      break;
    case hidkeymap::KEY_DOWN:
      buf->cursorDown();
      break;
    case hidkeymap::KEY_HOME:
      buf->cursorHome();
      break;
    case hidkeymap::KEY_END:
      buf->cursorEnd();
      break;
    case hidkeymap::KEY_DELETE:
      buf->del();
      break;
    case '\b':
      buf->backspace();
      break;
    case hidkeymap::KEY_PAGE_UP:
    case hidkeymap::KEY_PAGE_DOWN:
      break;  // paging belongs to the answer view
    default:
      if (key < 0x100) buf->insert(static_cast<char>(key));
      break;
  }
}

void ClaudeChatActivity::send() {
  if (!buf || buf->empty()) return;
  const std::string prompt(buf->data(), buf->size());

  // Radios take turns: BLE returns ~72 KB, and TLS needs a large slice of it.
  setPhase("closing keyboard");
  blespike::end();

  claudechat::Result r = claudechat::runExchange(
      prompt, [](void* ctx, const char* p) { static_cast<ClaudeChatActivity*>(ctx)->setPhase(p); }, this);

  if (r.ok) {
    answer = std::move(r.responseText);
    buf->clear();
  } else {
    answer = "Could not reach Claude.\n\n" + r.error + "\n\nYour prompt is kept — press Back, reopen, and try again.";
    LOG_ERR(TAG, "exchange failed: %s", r.error.c_str());
  }
  layoutAnswer();
  phaseText[0] = '\0';
  view = View::Answer;

  // Re-arm the keyboard only if the heap can take it. After a WiFi teardown the
  // largest free block is ~24 KB against the ~65 KB nimble_port_init() needs;
  // attempting it there hung the device and tripped the watchdog.
  if (blespike::canStart()) {
    blespike::begin();
  } else {
    LOG_ERR(TAG, "not restarting BLE: heap too fragmented after the exchange");
  }
  requestUpdate();
}

void ClaudeChatActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (view == View::Answer) {  // Back from an answer returns to the prompt
      view = View::Prompt;
      answer.clear();
      answerLines.clear();
      requestUpdate();
      return;
    }
    finish();
    return;
  }

  if (view == View::Answer) {
    const size_t step = static_cast<size_t>(maxLines) > 1 ? maxLines - 1 : 1;
    if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
      if (answerTop + step < answerLines.size()) answerTop += step;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
      answerTop = answerTop > step ? answerTop - step : 0;
      requestUpdate();
      return;
    }
  }

  if (view == View::Prompt && mappedInput.wasReleased(MappedInputManager::Button::Confirm) && buf && !buf->empty()) {
    send();
    return;
  }

  bool got = false;
  for (int c = blespike::popChar(); c >= 0; c = blespike::popChar()) {
    if (view == View::Answer) {  // typing returns to the prompt
      view = View::Prompt;
      answer.clear();
      answerLines.clear();
    }
    handleKey(c);
    got = true;
    ++pendingChars;
  }

  if (got) {
    lastKeyMs = millis();
    dirty = true;
  }
  if (dirty && (pendingChars >= FLUSH_CHARS || millis() - lastKeyMs >= DEBOUNCE_MS)) {
    dirty = false;
    pendingChars = 0;
    requestUpdate();
  }
}

void ClaudeChatActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const char* title = view == View::Working ? phaseText : "Claude";
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);

  const int statusY = renderer.getScreenHeight() - metrics.buttonHintsHeight - renderer.getLineHeight(SMALL_FONT_ID);

  if (view == View::Answer) {
    for (size_t n = 0; n < static_cast<size_t>(maxLines) && answerTop + n < answerLines.size(); ++n) {
      renderer.drawText(editorFontId, metrics.contentSidePadding, contentTop + static_cast<int>(n) * lineHeight,
                        answerLines[answerTop + n].c_str());
    }
    char note[80];
    snprintf(note, sizeof(note), "saved to /claude-chat.md   line %u/%u", (unsigned)(answerTop + 1),
             (unsigned)answerLines.size());
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, statusY, note);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "PgUp", "PgDn");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (view == View::Working) {
    renderer.drawCenteredText(editorFontId, contentTop + lineHeight * 2, phaseText);
    renderer.displayBuffer();
    return;
  }

  // Prompt view.
  if (buf && !buf->empty()) {
    const std::string prompt(buf->data(), buf->size());
    const auto wrapped = renderer.wrappedText(editorFontId, prompt.c_str(), maxWidth, maxLines);
    for (size_t n = 0; n < wrapped.size(); ++n) {
      renderer.drawText(editorFontId, metrics.contentSidePadding, contentTop + static_cast<int>(n) * lineHeight,
                        wrapped[n].c_str());
    }
  } else {
    renderer.drawText(editorFontId, metrics.contentSidePadding, contentTop, "Type a question, then press Ask.");
  }

  char status[80];
  snprintf(status, sizeof(status), "%u ch   keyboard: %s", (unsigned)(buf ? buf->size() : 0), blespike::stateName());
  renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, statusY, status);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), (buf && !buf->empty()) ? "Ask" : "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
