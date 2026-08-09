#include "ClaudeChatActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "SdCardFontSystem.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "notes/BleHidHost.h"
#include "notes/ClaudeChat.h"
#include "notes/EditorFonts.h"
#include "notes/HidKeymap.h"
#include "notes/MarkdownSpans.h"

namespace {
constexpr const char* TAG = "CLAUDEUI";
constexpr int CHAT_FONT_ID_FALLBACK = UI_10_FONT_ID;

// The Editor Font setting picks from the editor-group families (see
// src/notes/EditorFonts.h). If that family is not installed on the card the UI
// face stands in, so the screen is never blank because of a missing font.
}  // namespace

void ClaudeChatActivity::onEnter() {
  Activity::onEnter();

// The host keyboard channel. Announcing text entry is what raises an iPhone's
// on-screen keyboard (the harness calls SDL_StartTextInput on this flag), and
// it is also what suppresses the scancode->button map -- without it every "p"
// typed here would press POWER and every "s" would sleep the device.
//
// KeyboardEntryActivity and DaisyEntryActivity have always done this; the two
// editors never did, so on the phone Create Note and Claude had no keyboard at
// all and a paired one fought the button map.
  mappedInput.setTextEntryActive(true);

  // The on-screen keyboard is the default. BLE only comes up when a keyboard is
  // already bonded — it costs ~72 KB of heap and a CPU-clock lock, which an
  // owner with none paired should not pay. Hold Up to pair on demand.
  if (blekbd::hasBondedKeyboard()) {
    blekbd::begin();
  } else {
    LOG_INF(TAG, "no bonded keyboard; on-screen keyboard only (hold Up to pair)");
  }
  panel.begin(/*okIsDone=*/true);  // OK asks Claude here

  editorFontId = editorfonts::resolve(
      SETTINGS.editorFont, [this](int id) { return renderer.getFontMap().count(id) > 0; },
      [this](const char* family) {
        // loadForDisplay(), NOT SETTINGS.sdFontIdResolver. The resolver is
        // resolveFontId(), which returns a font id only when that family is the
        // family the READER currently has resident -- it never loads anything.
        // So every card-only editor face resolved to 0 unless the owner
        // happened to be reading in that same family, and resolve() fell
        // through to the compiled-in mono. Measured on the simulator with all
        // three iA families installed: the text band of a note rendered with
        // editorFont=iAWriterQuattro was BYTE-IDENTICAL to one rendered with
        // editorFont=SpaceMono. Three of the five rows did nothing at all.
        //
        // loadForDisplay does load it, at the editor's 12 pt, the same way the
        // Lyra theme loads its title face and CalendarSleepScreen loads 18 pt.
        // It can evict the reader family; that is safe and already routine --
        // the reader re-asserts through sdFontSystem.ensureLoaded() on entry,
        // which is exactly why FontSelectionActivity::onEnter does the same.
        return sdFontSystem.loadForDisplay(family, 12, renderer);
      },
      CHAT_FONT_ID_FALLBACK);
  const auto& metrics = UITheme::getInstance().getMetrics();
  lineHeight = renderer.getLineHeight(editorFontId);
  if (lineHeight < 1) lineHeight = 1;
  contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  // Reserve the right gutter: the side-button hints are drawn there whenever
  // row navigation is available, and wrapped text would otherwise run under
  // them. Daisy draws no hints, so it keeps the full width.
  const int sideGutter = panel.isDaisy() ? 0 : metrics.sideButtonHintsWidth;
  maxWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2 - sideGutter;

  // Split screen: prompt/answer above, keyboard below.
  panelHeight = panel.preferredHeight(renderer);
  panelTop = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing - panelHeight;
  // Vertical bands, top to bottom, each owning a disjoint range:
  //   header | text | status | keyboard panel | button hints
  // The status line sits BETWEEN the text and the panel and must be reserved
  // here — computing maxLines against panelTop let the last line of text draw
  // underneath it.
  statusHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int textBottom = panelTop - statusHeight;
  maxLines = (textBottom - contentTop) / lineHeight;
  if (maxLines < 1) maxLines = 1;

  // Allocate AFTER geometry, so an OOM cannot leave render() painting with an
  // unset editorFontId (0 is the font-not-found sentinel).
  storage = makeUniqueNoThrow<char[]>(BUF_SIZE);
  if (storage) buf = makeUniqueNoThrow<textbuf::TextBuffer>(storage.get(), BUF_SIZE);
  if (!buf) {
    LOG_ERR(TAG, "OOM: %u byte prompt buffer", (unsigned)BUF_SIZE);
    oomFailed = true;
  }

  requestUpdate();
}

void ClaudeChatActivity::onExit() {
  mappedInput.setTextEntryActive(false);
  blekbd::end();
  buf.reset();
  storage.reset();
  Activity::onExit();
}

void ClaudeChatActivity::setPhase(const char* phase) {
  {
    // render() reads phaseText on the render task; the wait below must NOT be
    // inside this scope (see handlePanelKey).
    RenderLock lock;
    snprintf(phaseText, sizeof(phaseText), "%s", phase);
    view = View::Working;
  }
  LOG_INF(TAG, "phase: %s", phase);
  requestUpdateAndWait();  // the exchange blocks loop(); paint each phase
}

// Lines the answer view can show: it has no keyboard panel, so it runs from
// contentTop down to the status line.
int ClaudeChatActivity::answerLinesOnScreen() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int statusY = renderer.getScreenHeight() - metrics.buttonHintsHeight - renderer.getLineHeight(SMALL_FONT_ID);
  return std::max(1, (statusY - contentTop) / lineHeight);
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
  blekbd::end();

  claudechat::Result r = claudechat::runExchange(
      prompt, [](void* ctx, const char* p) { static_cast<ClaudeChatActivity*>(ctx)->setPhase(p); }, this);

  if (!r.ok) LOG_ERR(TAG, "exchange failed: %s", r.error.c_str());
  {
    // answer / answerLines / view are walked by render() on its own task.
    RenderLock lock;
    if (r.ok) {
      answer = std::move(r.responseText);
      buf->clear();
    } else {
      answer = "Could not reach Claude.\n\n" + r.error + "\n\nYour prompt is kept — press Back, reopen, and try again.";
    }
    layoutAnswer();
    phaseText[0] = '\0';
    view = View::Answer;
  }

  // Re-arm the keyboard only if the heap can take it. After a WiFi teardown the
  // largest free block is ~24 KB against the ~65 KB nimble_port_init() needs;
  // attempting it there hung the device and tripped the watchdog.
  if (blekbd::canStart()) {
    blekbd::begin();
  } else {
    LOG_ERR(TAG, "not restarting BLE: heap too fragmented after the exchange");
  }
  requestUpdate();
}

// Hold Up to pair a BLE keyboard, hold Down to disconnect one keeping the
// bond. Short presses of the same buttons still navigate, so the gesture only
// fires past the hold threshold and swallows the release that follows.
void ClaudeChatActivity::pollPairingGestures() {
  // In DAISY, Up/Down are the wheel's pick buttons and holding them means
  // uppercase — so the pairing gesture cannot live there too, or one long press
  // would type a capital AND start Bluetooth. Daisy users pair from Settings.
  if (panel.isDaisy()) return;

  constexpr uint32_t HOLD_MS = 1500;
  const bool up = mappedInput.isPressed(MappedInputManager::Button::Up);
  const bool down = mappedInput.isPressed(MappedInputManager::Button::Down);
  if (!up && !down) {
    sideHeldSince = 0;
    return;
  }
  if (sideHeldSince == 0) {
    sideHeldSince = millis();
    sideHandled = false;
    return;
  }
  if (sideHandled || millis() - sideHeldSince < HOLD_MS) return;

  sideHandled = true;
  if (up) {
    if (blekbd::state() == blekbd::State::Off) {
      LOG_INF(TAG, "hold Up: starting BLE to pair");
      blekbd::begin();
    }
  } else {
    LOG_INF(TAG, "hold Down: disconnecting keyboard, bond kept");
    blekbd::disconnectKeepingBond();
  }
  requestUpdate();
}

// Re-wrap the prompt for display. Called from loop() under the render lock.
void ClaudeChatActivity::relayoutPrompt() {
  promptLines.clear();
  if (!buf || buf->empty()) return;
  const std::string prompt(buf->data(), buf->size());
  promptLines = renderer.wrappedText(editorFontId, prompt.c_str(), maxWidth, maxLines);
}

// One column step on press, then repeats while held. Returns true when it
// consumed the button this tick.
bool ClaudeChatActivity::repeatCol(const MappedInputManager::Button button, const int delta) {
  constexpr uint32_t FIRST_REPEAT_MS = 450;
  constexpr uint32_t NEXT_REPEAT_MS = 140;
  if (mappedInput.wasPressed(button)) {
    panel.moveCol(delta);
    colRepeatAt = millis() + FIRST_REPEAT_MS;
    requestUpdate();
    return true;
  }
  if (mappedInput.isPressed(button) && colRepeatAt != 0 && millis() >= colRepeatAt) {
    panel.moveCol(delta);
    colRepeatAt = millis() + NEXT_REPEAT_MS;
    requestUpdate();
    return true;
  }
  if (mappedInput.wasReleased(button)) {
    colRepeatAt = 0;
    return true;
  }
  return false;
}

void ClaudeChatActivity::handlePanelKey(const int slot, const bool longPress) {
  bool askClaude = false;
  bool textChanged = true;
  {
    RenderLock lock;
    const notes::KeyboardPanel::Result r =
        panel.isDaisy() ? panel.activateSlot(slot, longPress) : panel.activate(longPress);
    switch (r.event) {
      case notes::KeyboardPanel::Event::Character:
        handleKey(static_cast<unsigned char>(r.ch));
        break;
      case notes::KeyboardPanel::Event::Backspace:
        handleKey('\b');
        break;
      case notes::KeyboardPanel::Event::ClearAll:
        if (buf) buf->clear();
        break;
      case notes::KeyboardPanel::Event::Enter:
        handleKey('\n');
        break;
      case notes::KeyboardPanel::Event::Done:
        // "OK" on the panel asks Claude, which is what a finished prompt means.
        askClaude = buf && !buf->empty();
        textChanged = false;
        break;
      case notes::KeyboardPanel::Event::None:
        // Shift, symbols layer, daisy ring swap: the KEYBOARD changed, the text
        // did not. Repaint now; deferring would leave the old layer on screen.
        textChanged = false;
        break;
    }
  }
  // send() paints a phase per step via requestUpdateAndWait(), which waits on
  // the render task — so it must run with the render lock RELEASED or the wait
  // deadlocks. It asserted instead: "Cannot call requestUpdateAndWait() while
  // holding RenderLock" (ActivityManager.cpp:326), on every press of the
  // panel's OK key. send() takes the lock itself for the state it mutates.
  if (askClaude) {
    send();
    return;
  }

  // Typed text takes the SAME debounce as the Bluetooth drain in loop(). This
  // path used to repaint unconditionally, so Typing Redraw Delay only ever
  // applied to BLE typing — with no keyboard paired it did nothing at all.
  if (textChanged) {
    lastKeyMs = millis();
    dirty = true;
    ++pendingChars;
    return;
  }
  requestUpdate();
}

void ClaudeChatActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (view == View::Answer) {  // Back from an answer returns to the prompt
      {
        // Same rule as relayoutAnswer() above: render() walks answer /
        // answerLines / view on the render task, and loop() holds no lock of
        // its own, so clearing them here frees strings mid-draw.
        RenderLock lock;
        view = View::Prompt;
        answer.clear();
        answerLines.clear();
      }
      requestUpdate();
      return;
    }
    finish();
    return;
  }

  pollPairingGestures();

  if (view == View::Answer) {
    // Step by what the ANSWER view shows, not the prompt view's smaller count,
    // or paging skips lines it never displayed.
    const size_t step = answerLinesOnScreen() > 1 ? answerLinesOnScreen() - 1 : 1;
    if (!sideHandled && mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      if (answerTop + step < answerLines.size()) answerTop += step;
      requestUpdate();
      return;
    }
    if (!sideHandled && mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      answerTop = answerTop > step ? answerTop - step : 0;
      requestUpdate();
      return;
    }
  }

  if (oomFailed || !buf) return;  // Back above is the only thing that works

  if (view == View::Prompt) {
    // Confirm types the selected key; the panel's OK key is what asks Claude.
    // Confirm was "Ask" before the on-screen keyboard existed — with a keyboard
    // on screen it has to be the type key, or there is no way to type at all.
    // Pick buttons. In the GRIDS only Confirm types, and holding it yields the
    // alt output (uppercase for a letter) exactly as the full-screen keyboard
    // does. In DAISY the three buttons pick top/middle/bottom of the current
    // petal, which is how the real wheel works.
    {
      constexpr uint32_t LONG_PRESS_MS = 500;
      struct Pick {
        MappedInputManager::Button button;
        int slot;
      };
      const Pick picks[] = {{MappedInputManager::Button::Up, 0},
                            {MappedInputManager::Button::Confirm, 1},
                            {MappedInputManager::Button::Down, 2}};
      for (const auto& pk : picks) {
        // Up/Down only pick in daisy; elsewhere they page and long-hold pairs.
        if (!panel.isDaisy() && pk.slot != 1) continue;

        if (mappedInput.wasPressed(pk.button) && pickSlot < 0) {
          pickSlot = pk.slot;
          pickHeldSince = millis();
          pickFired = false;
        }
        if (pickSlot == pk.slot && !pickFired && mappedInput.isPressed(pk.button) &&
            millis() - pickHeldSince > LONG_PRESS_MS) {
          handlePanelKey(pk.slot, /*longPress=*/true);
          pickFired = true;
        }
        if (mappedInput.wasReleased(pk.button) && pickSlot == pk.slot) {
          if (!pickFired) handlePanelKey(pk.slot, /*longPress=*/false);
          pickSlot = -1;
          pickFired = false;
          return;
        }
      }
    }

    // Auto-repeat: a 13-Grid row is 13 keys, and one move per RELEASE meant
    // twelve presses (and twelve e-ink repaints) to cross it.
    if (repeatCol(MappedInputManager::Button::Left, -1)) return;
    if (repeatCol(MappedInputManager::Button::Right, 1)) return;
    if (!panel.isDaisy() && !sideHandled && mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      panel.moveRow(-1);
      requestUpdate();
      return;
    }
    if (!panel.isDaisy() && !sideHandled && mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      panel.moveRow(1);
      requestUpdate();
      return;
    }
  }

  bool got = false;

  // Host-typed text (an iPhone's on-screen keyboard, or one paired to it),
  // drained the same way as BLE and folded into the same debounce. Return is a
  // newline here, as it is on the panel; Escape never reaches this stream
  // because HalGPIO leaves it mapped to BTN_BACK.
  std::string typedHost;
  if (mappedInput.consumeTypedText(typedHost)) {
    for (const char c : typedHost) {
      if (view == View::Answer) {  // typing returns to the prompt
        RenderLock lock;
        view = View::Prompt;
        answer.clear();
        answerLines.clear();
      }
      switch (c) {
        case HalGPIO::TYPED_COMMIT:
          handleKey('\n');
          break;
        case HalGPIO::TYPED_BACKSPACE:
          handleKey('\b');
          break;
        case HalGPIO::TYPED_CANCEL:
          break;
        default:
          handleKey(static_cast<unsigned char>(c));
          break;
      }
      got = true;
      ++pendingChars;
    }
  }

  for (int c = blekbd::popChar(); c >= 0; c = blekbd::popChar()) {
    if (view == View::Answer) {  // typing returns to the prompt
      RenderLock lock;  // render() walks these on its own task; see loop() above
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
  if (dirty && (pendingChars >= FLUSH_CHARS || millis() - lastKeyMs >= SETTINGS.getDisplayDebounceMs())) {
    dirty = false;
    pendingChars = 0;
    {
      RenderLock lock;
      relayoutPrompt();
    }
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
    // The answer view draws no keyboard, so it gets the whole height down to
    // the status line. Reusing the prompt view's maxLines showed ~8 lines on a
    // screen that fits far more, with the bottom half blank.
    const int shown = answerLinesOnScreen();
    for (size_t n = 0; n < static_cast<size_t>(shown) && answerTop + n < answerLines.size(); ++n) {
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
    // WRAPPED, and to the same maxWidth the prompt uses. Drawn raw it ran off
    // the right edge the moment the editor font became a real monospace face:
    // the string was sized by eye against the narrower UI face it used to
    // borrow. Also tr() — it was a hardcoded English literal.
    const auto hint = renderer.wrappedText(editorFontId, tr(STR_CLAUDE_PROMPT_HINT), maxWidth, 3);
    for (size_t n = 0; n < hint.size(); ++n) {
      renderer.drawText(editorFontId, metrics.contentSidePadding, contentTop + static_cast<int>(n) * lineHeight,
                        hint[n].c_str());
    }
  }

  panel.render(renderer, metrics.contentSidePadding, panelTop, pageWidth - metrics.contentSidePadding * 2, panelHeight);

  char status[80];
  snprintf(status, sizeof(status), "%u ch   kbd:%s   OK asks Claude", (unsigned)(buf ? buf->size() : 0),
           blekbd::stateName());
  renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, panelTop - statusHeight, status);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  // Row navigation lives on the side buttons and is otherwise undiscoverable.
  if (!panel.isDaisy()) GUI.drawSideButtonHints(renderer, ">", "<");  // same convention as the full-screen keyboard
  renderer.displayBuffer();
}
