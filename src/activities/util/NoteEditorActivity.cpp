#include "NoteEditorActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "CrossPointSettings.h"
#include "SdCardFontSystem.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "notes/BleHidHost.h"
#include "notes/EditorFonts.h"
#include "notes/HidKeymap.h"
#include "notes/MarkdownRender.h"
#include "notes/MarkdownSpans.h"

namespace {
constexpr const char* TAG = "NOTEEDIT";
constexpr int EDITOR_FONT_ID_FALLBACK = UI_10_FONT_ID;

// The Editor Font setting picks from the editor-group families (see
// src/notes/EditorFonts.h). If that family is not installed on the card the UI
// face stands in, so the screen is never blank because of a missing font.
}  // namespace

// The vertical bands, top to bottom, each owning a disjoint range:
//   text | status | keyboard panel | button hints
// (no header band since 2026-08-15 -- the title bar was removed)
//
// Recomputed rather than fixed at entry, because ONE of those bands can vanish
// while the editor is open: when a host's own software keyboard is covering the
// screen, drawing our on-screen panel underneath it is two keyboards for one
// job, and the rows are better spent on text (owner ruling 2026-08-16). On
// device isHostKeyboardVisible() is a constant false, so panelHeight is always
// the panel's own height and this is the same arithmetic it always was.
//
// The status line sits BETWEEN the text and the panel and must be reserved
// here -- computing maxLines against panelTop let the last line of text draw
// underneath it.
void NoteEditorActivity::applyLayout() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  panelHidden = mappedInput.isHostKeyboardVisible();
  panelHeight = panelHidden ? 0 : panel.preferredHeight(renderer);
  // With the panel gone the band closes up entirely rather than leaving a hole:
  // panelTop lands on the hints, and the text below gains those rows.
  panelTop = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing - panelHeight;
  statusHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int textBottom = panelTop - statusHeight;
  maxLines = (textBottom - contentTop) / lineHeight;
  if (maxLines < 1) maxLines = 1;
}

void NoteEditorActivity::onEnter() {
  Activity::onEnter();

  // The host keyboard channel. Announcing text entry is what raises an iPhone's
  // on-screen keyboard (the harness calls SDL_StartTextInput on this flag), and
  // it is also what suppresses the scancode->button map -- without it every "p"
  // typed here would press POWER and every "s" would sleep the device.
  //
  // KeyboardEntryActivity and DaisyEntryActivity have always done this; the two
  // editors never did, so on the phone Create Note and Claude had no keyboard at
  // all and a paired one fought the button map.
  //
  // Multi, because this is an editor and not a field: a host keyboard's Return
  // is a line break here, exactly as a paired Bluetooth keyboard's Enter already
  // is (notes/HidKeymap.h decodes usage 0x28 to '\n', and the drain below turns
  // TYPED_COMMIT into the same). Announcing Single left the host treating Return
  // as the Confirm button, so it pressed Select on the on-screen panel instead
  // of breaking the line -- ST-006.
  mappedInput.setTextEntryActive(true, HalGPIO::TextEntryLines::Multi);

  panel.begin();

  // Geometry FIRST, allocation second. render() runs on its own task and will
  // paint this activity whatever happens here; returning early on OOM used to
  // leave editorFontId at 0 — the "font not found" sentinel — plus lineHeight
  // and panelHeight unset. Same shape as the NimBLE-before-init panic: a
  // failure path that leaves the object half-built and still live.
  editorFontId = editorfonts::resolve(
      SETTINGS.editorFont, SETTINGS.editorFontSize, [this](int id) { return renderer.getFontMap().count(id) > 0; },
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
        // loadForDisplay does load it, at the editor's chosen size, the same way the
        // Lyra theme loads its title face and CalendarSleepScreen loads 18 pt.
        // It can evict the reader family; that is safe and already routine --
        // the reader re-asserts through sdFontSystem.ensureLoaded() on entry,
        // which is exactly why FontSelectionActivity::onEnter does the same.
        return sdFontSystem.loadForDisplay(family, editorfonts::nearestOfferedSize(SETTINGS.editorFontSize), renderer);
      },
      EDITOR_FONT_ID_FALLBACK);
  const auto& metrics = UITheme::getInstance().getMetrics();
  lineHeight = renderer.getLineHeight(editorFontId);
  if (lineHeight < 1) lineHeight = 1;
  // Starts at topPadding: there is no header band to skip past any more.
  contentTop = metrics.topPadding;
  // No side gutter: the side-button hints that it reserved room for were
  // removed (owner ruling 2026-08-15), so the text gets the width back.
  maxWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2;

  applyLayout();

  storage = makeUniqueNoThrow<char[]>(BUF_SIZE);
  if (storage) buf = makeUniqueNoThrow<textbuf::TextBuffer>(storage.get(), BUF_SIZE);
  if (!buf) {
    // Say so on screen rather than presenting an editor that silently eats
    // every keystroke.
    LOG_ERR(TAG, "OOM: %u byte note buffer", (unsigned)BUF_SIZE);
    oomFailed = true;
    requestUpdate();
    return;
  }

  // BLE comes up AFTER the note buffer, and only when a keyboard is already
  // bonded. Ordering matters: BLE takes ~72 KB, so starting it first was a way
  // to make the 8 KB buffer fail. It used to go first to give
  // nimble_port_init() the roomiest heap — but that hang was the CPU dropping
  // to 10 MHz, not memory, so the reason no longer holds.
  if (blekbd::hasBondedKeyboard()) {
    blekbd::begin();
  } else {
    LOG_INF(TAG, "no bonded keyboard; on-screen keyboard only (pair from Settings)");
  }

  // Load an existing note so Edit-from-Manage-Files is a real edit, not a
  // silent overwrite. Anything past the cap is refused rather than truncated.
  HalFile f;
  // A file that EXISTS but will not open is the dangerous case: the load block
  // below is skipped, the buffer stays empty, and onExit()'s save() writes that
  // emptiness back over it. loadRefused only covered the too-big branch, so a
  // transient open failure -- a flaky card, or EMFILE after enough directory
  // handles leak -- silently emptied the note. A file that does NOT exist is
  // just a new note from Create Note and must still save.
  const bool existed = Storage.exists(path.c_str());
  const bool opened = Storage.openFileForRead(TAG, path, f);
  if (existed && !opened) {
    LOG_ERR(TAG, "%s exists but will not open; refusing to overwrite it", path.c_str());
    loadRefused = true;
  } else if (opened) {
    const size_t size = static_cast<size_t>(f.size());
    if (size >= BUF_SIZE) {
      LOG_ERR(TAG, "%s is %u bytes, cap is %u — refusing to open", path.c_str(), (unsigned)size, (unsigned)BUF_SIZE);
      bufferFull = true;
      loadRefused = true;
    } else {
      char chunk[256];
      int n;
      while ((n = f.read(chunk, sizeof(chunk))) > 0) {
        for (int i = 0; i < n; ++i) buf->insert(chunk[i]);
      }
      // END, not 0: insert() writes AT the cursor, so opening at byte 0 made
      // every keystroke prepend to the top of the file and made Del a no-op.
      buf->cursorTo(buf->size());
      LOG_INF(TAG, "loaded %u bytes from %s", (unsigned)buf->size(), path.c_str());
    }
  }

  lines.reserve(64);
  relayout();
  requestUpdate();
}

void NoteEditorActivity::onExit() {
  mappedInput.setTextEntryActive(false);
  save();
  blekbd::end();
  buf.reset();
  storage.reset();
  Activity::onExit();
}

void NoteEditorActivity::exitEditor() {
  if (returnDir.empty()) {
    finish();
  } else {
    // Back to the folder we came from, ON the note we edited: the manager is
    // rebuilt (replaceActivity both ways), so it needs the name to re-focus
    // (audit F7, 2026-09-02). The name is `path` past its last '/'.
    const size_t slash = path.rfind('/');
    activityManager.goToFileManager(returnDir, slash == std::string::npos ? path : path.substr(slash + 1));
  }
}

// Rebuild the soft-wrapped display lines. O(n) over the buffer, and only run
// when the text actually changes — not per render.
void NoteEditorActivity::relayout() {
  lines.clear();
  if (!buf) return;

  const char* text = buf->data();
  const size_t len = buf->size();
  char probe[256];
  size_t i = 0;

  while (i <= len) {
    const size_t start = i;
    size_t lastBreak = 0;
    size_t j = i;
    bool hard = false;

    while (j < len) {
      if (text[j] == '\n') {
        hard = true;
        break;
      }
      const size_t cand = j + 1 - start;
      if (cand >= sizeof(probe)) break;
      memcpy(probe, text + start, cand);
      probe[cand] = '\0';
      // measureLine, NOT getTextWidth: the line is DRAWN by mdrender::drawLine,
      // which consumes the markers, so measuring the raw source charges the
      // line four characters for every "**bold**" it never puts on screen. The
      // wrap then broke early and left a gap after each styled word.
      if (mdrender::measureLine(renderer, editorFontId, lineHeight, probe, cand) > maxWidth) break;
      if (text[j] == ' ') lastBreak = j + 1;
      ++j;
    }

    size_t end = j;
    if (!hard && j < len && lastBreak > start) end = lastBreak;
    lines.push_back({static_cast<uint16_t>(start), static_cast<uint16_t>(end)});

    if (hard) {
      i = end + 1;
      if (i > len) break;
      if (i == len) {  // trailing newline: show the empty line it creates
        lines.push_back({static_cast<uint16_t>(len), static_cast<uint16_t>(len)});
        break;
      }
    } else {
      if (end >= len) break;
      i = end;
    }
  }
  if (lines.empty()) lines.push_back({0, 0});
}

size_t NoteEditorActivity::lineOfCursor() const {
  const size_t c = buf ? buf->cursor() : 0;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (c >= lines[i].start && c <= lines[i].end) return i;
  }
  return lines.empty() ? 0 : lines.size() - 1;
}

// Scroll only as far as needed to bring the cursor back on screen, so typing
// mid-document does not jump the view.
void NoteEditorActivity::ensureCursorVisible() {
  const size_t cl = lineOfCursor();
  if (cl < topLine) {
    topLine = cl;
  } else if (cl >= topLine + static_cast<size_t>(maxLines)) {
    topLine = cl - maxLines + 1;
  }
}

void NoteEditorActivity::pageUp() {
  const size_t step = static_cast<size_t>(maxLines) > 1 ? maxLines - 1 : 1;
  topLine = topLine > step ? topLine - step : 0;
  if (buf && !lines.empty()) buf->cursorTo(lines[topLine].start);
}

void NoteEditorActivity::pageDown() {
  const size_t step = static_cast<size_t>(maxLines) > 1 ? maxLines - 1 : 1;
  if (topLine + step < lines.size()) topLine += step;
  if (buf && topLine < lines.size()) buf->cursorTo(lines[topLine].start);
}

void NoteEditorActivity::handleKey(int key) {
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
    case hidkeymap::KEY_PAGE_UP:
      pageUp();
      break;
    case hidkeymap::KEY_PAGE_DOWN:
      pageDown();
      break;
    case hidkeymap::KEY_DELETE:
      buf->del();
      break;
    case '\b':
      buf->backspace();
      break;
    default:
      if (key < 0x100 && !buf->insert(static_cast<char>(key))) {
        bufferFull = true;  // surfaced on screen; never silently dropped
        LOG_ERR(TAG, "buffer full at %u bytes", (unsigned)buf->size());
      }
      break;
  }
}

// One column step on press, then repeats while held. Returns true when it
// consumed the button this tick.
bool NoteEditorActivity::repeatCol(const MappedInputManager::Button button, const int delta) {
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

void NoteEditorActivity::moveCaret(const CaretDir dir) {
  if (!buf) return;
  // render() reads buf->cursor() on its own task.
  RenderLock lock;
  switch (dir) {
    case CaretDir::Left:
      buf->cursorLeft();
      break;
    case CaretDir::Right:
      buf->cursorRight();
      break;
    case CaretDir::Up:
      buf->cursorUp();
      break;
    case CaretDir::Down:
      buf->cursorDown();
      break;
  }
  caretDirty = true;
  caretLastMs = millis();
}

// One caret step on press, then repeats while held. Deliberately SLOWER than
// repeatCol's 450/140: the repaint is debounced (below) and an e-ink refresh is
// ~570 ms, so a caret stepping faster than the panel can show it runs blind and
// overshoots. One step per refresh is the most the display can actually report.
bool NoteEditorActivity::repeatCaret(const MappedInputManager::Button button, const CaretDir dir) {
  constexpr uint32_t FIRST_REPEAT_MS = 600;
  constexpr uint32_t NEXT_REPEAT_MS = 300;
  if (mappedInput.wasPressed(button)) {
    moveCaret(dir);
    caretRepeatAt = millis() + FIRST_REPEAT_MS;
    return true;
  }
  if (mappedInput.isPressed(button) && caretRepeatAt != 0 && millis() >= caretRepeatAt) {
    moveCaret(dir);
    caretRepeatAt = millis() + NEXT_REPEAT_MS;
    return true;
  }
  if (mappedInput.wasReleased(button)) {
    caretRepeatAt = 0;
    return true;
  }
  return false;
}

// Caret mode owns the tick outright — the keyboard panel takes no input at all
// while it is up, the same way an OptionPopup gates the top of a loop().
// Holding Up here means "cursor up".
void NoteEditorActivity::loopCaretMode() {
  // Back or Confirm PRESS leaves. Press, not release: the release that ends the
  // long press on space has not arrived yet and must not read as an exit. Back
  // returns to typing rather than leaving the editor, as a popup's Back does.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    caretMode = false;
    caretDirty = false;
    caretRepeatAt = 0;
    {
      RenderLock lock;
      ensureCursorVisible();
    }
    requestUpdate();
    return;
  }

  bool moved = repeatCaret(MappedInputManager::Button::Left, CaretDir::Left);
  if (!moved) moved = repeatCaret(MappedInputManager::Button::Right, CaretDir::Right);
  if (!moved) moved = repeatCaret(MappedInputManager::Button::Up, CaretDir::Up);
  if (!moved) repeatCaret(MappedInputManager::Button::Down, CaretDir::Down);

  // Same debounce as typing, and for the same reason: a repaint per step would
  // cost a full panel refresh each time. No relayout() — the TEXT did not
  // change, only where the caret sits in it.
  if (caretDirty && millis() - caretLastMs >= SETTINGS.getDisplayDebounceMs()) {
    caretDirty = false;
    {
      RenderLock lock;
      ensureCursorVisible();
    }
    requestUpdate();
  }
}

void NoteEditorActivity::handlePanelKey(const int slot, const bool longPress) {
  // Everything below mutates buf/lines/topLine, all of which render() walks on
  // its own task.
  RenderLock lock;
  // Daisy picks a slot within the current petal; the grids activate the
  // selected key, with long-press giving the alt output (uppercase).
  const notes::KeyboardPanel::Result r =
      panel.isDaisy() ? panel.activateSlot(slot, longPress) : panel.activate(longPress);

  // HOLD THE SPACE BAR -> caret mode. Detected on the RESULT rather than by
  // asking the panel which key is selected, which keeps this entirely out of
  // KeyboardPanel and works identically on all three layouts.
  //
  // A space is an exact sentinel for "the space key was held", and nothing else
  // can produce one: keyboardAltOutputFor returns nullptr for any key whose
  // kind is not Normal (FreeInkUI.cpp:772), so KeyKind::Space falls back to
  // keyboardOutputFor and yields " " -- a long press on space typed a plain
  // space, byte for byte what a tap already did, so the gesture slot was free.
  // No letter's case flip and no declared alt in any layout is " ".
  // test/keyboard_panel pins both halves of that.
  if (longPress && r.event == notes::KeyboardPanel::Event::Character && r.ch == ' ') {
    // Flush any typing still sitting behind the debounce first: render() is
    // about to paint and `lines` would otherwise be a layout short.
    relayout();
    ensureCursorVisible();
    dirty = false;
    pendingChars = 0;
    caretMode = true;
    caretDirty = false;
    caretRepeatAt = 0;
    // Hand the pick loop back a clean slate. Leaving pickSlot armed meant the
    // Confirm RELEASE that ends this hold got consumed by caret mode instead,
    // and the next Confirm press after leaving the mode was then eaten by the
    // stale pickFired.
    pickSlot = -1;
    pickFired = false;
    requestUpdate();
    return;
  }

  bool textChanged = true;
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
      exitEditor();
      return;
    case notes::KeyboardPanel::Event::None:
      // Shift, the symbols layer, a daisy ring swap: the KEYBOARD changed and
      // the text did not. Repaint at once — deferring it would leave the old
      // layer on screen for up to a second, and there is nothing to batch.
      textChanged = false;
      break;
  }

  if (!textChanged) {
    relayout();
    ensureCursorVisible();
    requestUpdate();
    return;
  }

  // Typed text goes through the SAME debounce as the Bluetooth drain in loop().
  // It used to repaint here unconditionally, which meant Typing Redraw Delay
  // only ever applied to BLE typing — so with no keyboard paired, the
  // out-of-the-box state, the setting did nothing whatsoever and every
  // on-screen keypress cost a full ~500 ms panel refresh.
  lastKeyMs = millis();
  dirty = true;
  ++pendingChars;
}

void NoteEditorActivity::loop() {
  // A host keyboard can rise or fall at any moment -- the owner taps the chip,
  // or dismisses from the bar -- and the band layout depends on it. Checked
  // here rather than on an event, because nothing on this board raises one:
  // isHostKeyboardVisible() is a plain query, and on device it is a constant
  // false so this whole branch never fires.
  //
  // relayout() as well as applyLayout(), because maxWidth is unchanged but the
  // LINE COUNT is not: the wrap is the same, the window into it is bigger.
  if (mappedInput.isHostKeyboardVisible() != panelHidden) {
    RenderLock lock;
    applyLayout();
    relayout();
    ensureCursorVisible();
    requestUpdate();
  }

  // Gated ABOVE the Back handler, like an OptionPopup: while caret mode is up
  // Back closes the mode and does not leave the editor.
  if (caretMode) {
    loopCaretMode();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    exitEditor();
    return;
  }

  if (oomFailed || !buf) return;  // Back above is the only thing that works

  // Host-typed text, before the on-screen panel: a host with no keyboard never
  // yields a chunk, and both edit the same buffer and cursor.
  //
  // NOT TypedTextInput::apply(), which the single-line fields use: it reports
  // Return as Committed and STOPS the walk. This is a multi-line editor, where
  // Return is a newline and a pasted paragraph is full of them. Escape never
  // reaches this stream at all -- HalGPIO leaves it mapped to BTN_BACK, which
  // the Back handler above already treats as "leave".
  std::string typed;
  if (mappedInput.consumeTypedText(typed)) {
    for (const char c : typed) {
      switch (c) {
        case HalGPIO::TYPED_COMMIT:
          handleKey('\n');
          break;
        case HalGPIO::TYPED_BACKSPACE:
          handleKey('\b');
          break;
        case HalGPIO::TYPED_CANCEL:
          break;  // arrives as BTN_BACK; nothing to do here
        default:
          handleKey(static_cast<unsigned char>(c));
          break;
      }
    }
    if (!typed.empty()) {
      // Same debounce as the BLE drain, so Typing Redraw Delay applies to a
      // phone keyboard exactly as it does to a Bluetooth one.
      lastKeyMs = millis();
      dirty = true;
      pendingChars += static_cast<int>(typed.size());
    }
  }

  // KEYBOARD DRAIN AND REPAINT COME BEFORE THE panelHidden GUARD, and must stay
  // there. They used to sit at the very bottom of loop(), below the early
  // return -- so with a phone's software keyboard up (the only state in which
  // panelHidden is ever true) the drain above put every typed character into
  // buf and nothing ever called relayout() or requestUpdate() again. The text
  // was saved correctly on exit and the screen never changed once while it was
  // being typed, which is exactly what was reported: "create note with iOS
  // keyboard: not updating while typing at all". Verified headlessly --
  // tests/test_note_editor_repaint.sh, two screenshots either side of a TYPE
  // that came back byte-identical under CROSSPOINT_SIM_HOST_KEYBOARD=1.
  //
  // ClaudeChatActivity, the sibling editor, never had the bug because it has no
  // early return at all: it gates the panel's INPUT inside an if and hides the
  // panel in render(). This is the same shape, reached by moving the tail up.
  bool got = false;
  for (int c = blekbd::popChar(); c >= 0; c = blekbd::popChar()) {
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
      // render() runs on the ActivityManager's render task and walks `lines`
      // and `buf`. relayout() clears and regrows that vector, so without the
      // lock a repaint can hold a reference into a reallocated buffer.
      RenderLock lock;
      relayout();
      ensureCursorVisible();
    }
    requestUpdate();
  }

  // A HIDDEN panel takes no input. Nothing below this point is reachable while
  // a host keyboard is up, and that is the point: the panel is not drawn then,
  // and a Confirm that typed a key nobody can see would be worse than a dead
  // button. Typed text from the host still arrives -- that is drained above,
  // before this -- as does Back, which is handled earlier still, so the editor
  // stays fully usable with only the phone's keyboard.
  if (panelHidden) return;

  // Front buttons drive the on-screen keyboard: Left/Right move along a row,
  // Confirm types the selected key. Side buttons move between rows.
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
      // Up/Down only pick in daisy; elsewhere they page.
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
        if (caretMode) return;  // it reset the pick state; nothing below applies
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
  if (!panel.isDaisy() && mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    panel.moveRow(-1);
    requestUpdate();
    return;
  }
  if (!panel.isDaisy() && mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    panel.moveRow(1);
    requestUpdate();
    return;
  }
  // The BLE drain and the debounced repaint used to live here; they are above
  // the panelHidden guard now. See the comment there.
}

// Draw one display line with markdown styling, then the caret.
//
// The styling half is mdrender::drawLine -- shared with the Editor Font
// picker's specimen, so what that pane previews is what this draws, not an
// imitation of it. The caret stays here: it is measured in the RAW source text
// rather than the styled spans, because that is what the typist is editing.
void NoteEditorActivity::drawLine(const char* text, size_t len, int y, bool showCursor, size_t cursorCol) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  mdrender::drawLine(renderer, editorFontId, lineHeight, metrics.contentSidePadding, y, text, len);

  if (showCursor) {
    // The caret still tracks the SOURCE column -- that is what the typist is
    // editing -- but it is placed at the x that column is DRAWN at.
    //
    // It used to be placed by measuring the raw source prefix, which counts the
    // markers drawLine has just thrown away: after typing "**bold**" the caret
    // sat four characters to the right of the "d" it belonged against, and the
    // gap grew with every styled run earlier in the line. caretX walks the same
    // spans drawLine does, so the two cannot disagree; a column that lands
    // inside a marker clamps to the run it opens or closes.
    //
    // This also subsumes the old advanceOf() trailing-space workaround: caretX
    // measures spans, so a prefix ending in a space no longer loses that
    // space's advance the way a raw getTextWidth() call did.
    const int cx = metrics.contentSidePadding +
                   mdrender::caretX(renderer, editorFontId, lineHeight, text, len, cursorCol > len ? len : cursorCol);
    // A BLOCK caret says "the buttons drive me now" without a word of text, and
    // it lands exactly where the eye already is. The status band carries the
    // sentence; this carries the state.
    if (caretMode) {
      renderer.fillRect(cx, y, 3, lineHeight - 2, true);
    } else {
      renderer.drawLine(cx, y, cx, y + lineHeight - 2, true);
    }
  }
}

void NoteEditorActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  // No title bar (owner ruling 2026-08-15). The editor used to draw the file
  // path and an "n/m" page counter across the top; both are gone and the text
  // starts at the top of the screen instead. The filename is chosen by the
  // editor itself (a YYYYMMDDHHmmss.md stamp), so it told the owner nothing
  // they did not already know, and the page counter belongs to a paginated
  // reader rather than to a document being typed into.
  //
  // contentTop in relayout() reclaims the band, so this is a layout change and
  // not just a hidden draw call.

  if (oomFailed || !buf) {
    // Wrapped and translated for the same reason as Claude's hint: a raw
    // drawText of a long English literal overflows once the editor font is a
    // monospace face, and it is user-facing text, so it must go through tr().
    const auto oomLines = renderer.wrappedText(editorFontId, tr(STR_EDITOR_OOM), maxWidth, 3);
    for (size_t n = 0; n < oomLines.size(); ++n) {
      renderer.drawText(editorFontId, metrics.contentSidePadding, contentTop + static_cast<int>(n) * lineHeight,
                        oomLines[n].c_str());
    }
    const auto oomLabels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, oomLabels.btn1, oomLabels.btn2, oomLabels.btn3, oomLabels.btn4);
    renderer.displayBuffer();
    return;
  }

  const size_t cl = lineOfCursor();
  for (size_t n = 0; n < static_cast<size_t>(maxLines) && topLine + n < lines.size(); ++n) {
    const DisplayLine& dl = lines[topLine + n];
    const size_t len = dl.end > dl.start ? dl.end - dl.start : 0;
    const bool onThisLine = (topLine + n) == cl;
    drawLine(buf ? buf->data() + dl.start : "", len, contentTop + static_cast<int>(n) * lineHeight, onThisLine,
             onThisLine && buf ? buf->cursor() - dl.start : 0);
  }

  // Split screen: the keyboard panel occupies the strip above the button hints,
  // so the note stays visible while typing.
  // Skipped entirely while a host keyboard is up -- not drawn zero-height, not
  // drawn behind it. panelHeight is 0 then and the text above has already been
  // laid out into the rows this would have taken.
  if (!panelHidden)
    panel.render(renderer, metrics.contentSidePadding, panelTop, pageWidth - metrics.contentSidePadding * 2,
                 panelHeight);

  // Status bar: a right-aligned GRID, not a concatenated string.
  //
  // Each cell owns a fixed column measured from the right edge, so a cell's
  // width never moves its neighbours. The character count is the only thing
  // here that changes width while typing, and the whole point of the grid is
  // that KBD and FULL do not twitch left and right as digits are added -- so
  // the count column is sized for the widest value the buffer can ever hold
  // (BUF_SIZE is 8192, four digits) rather than for the current value.
  const int statusY = panelTop - statusHeight;
  const int rightEdge = pageWidth - metrics.contentSidePadding;
  const int cellGap = renderer.getTextAdvanceX(SMALL_FONT_ID, "  ", EpdFontFamily::REGULAR);

  // Rightmost cell: the count, just the number. Right-aligned inside its own
  // column so the digits stay flush to the edge as they grow.
  char countText[8];
  snprintf(countText, sizeof(countText), "%u", (unsigned)(buf ? buf->size() : 0));
  const int countColW = renderer.getTextAdvanceX(SMALL_FONT_ID, "8888", EpdFontFamily::REGULAR);
  const int countW = renderer.getTextAdvanceX(SMALL_FONT_ID, countText, EpdFontFamily::REGULAR);
  renderer.drawText(SMALL_FONT_ID, rightEdge - countW, statusY, countText);

  // Next cell left: KBD, shown ONLY while the keyboard stack is up. Off is the
  // ordinary state and printing "kbd:off" there was noise. Its x is derived
  // from countColW, never from countW, which is what keeps it still.
  const int kbdColRight = rightEdge - countColW - cellGap;
  int nextCellRight = kbdColRight;
  if (blekbd::state() != blekbd::State::Off) {
    const int kbdW = renderer.getTextAdvanceX(SMALL_FONT_ID, "KBD", EpdFontFamily::REGULAR);
    renderer.drawText(SMALL_FONT_ID, kbdColRight - kbdW, statusY, "KBD");
  }
  // FULL keeps its own cell to the left of KBD. It is a real warning -- the
  // buffer stops accepting input -- so it is not folded into the count.
  if (bufferFull) {
    const int kbdColW = renderer.getTextAdvanceX(SMALL_FONT_ID, "KBD", EpdFontFamily::REGULAR);
    nextCellRight = kbdColRight - kbdColW - cellGap;
    const int fullW = renderer.getTextAdvanceX(SMALL_FONT_ID, "FULL", EpdFontFamily::REGULAR);
    renderer.drawText(SMALL_FONT_ID, nextCellRight - fullW, statusY, "FULL");
  }

  // Caret mode borrows the LEFT of the same band for the only line of prose on
  // this screen, rather than adding a sixth band. The string is the full-screen
  // keyboard's own cursor-mode hint, reused verbatim -- same gesture, same
  // words, already translated.
  //
  // It shares the band with the grid rather than replacing it: the two occupy
  // opposite ends, and the count is still worth reading while the caret is
  // being driven. A translation long enough to reach the grid's leftmost
  // occupied cell is dropped rather than overprinted -- the count is the
  // load-bearing half.
  if (caretMode) {
    const char* hint = tr(STR_KB_HINT_MOVE_CURSOR);
    const int hintW = renderer.getTextAdvanceX(SMALL_FONT_ID, hint, EpdFontFamily::REGULAR);
    if (metrics.contentSidePadding + hintW < nextCellRight - cellGap) {
      renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, statusY, hint);
    }
  }

  // In caret mode Confirm ends the mode rather than typing, so it is labelled
  // for what it does. Back returns to typing, which is a parent state, so it
  // keeps STR_BACK per docs/ui-conventions.md.
  const auto labels = caretMode
                          ? mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT))
                          : mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

bool NoteEditorActivity::save() {
  if (!buf) return false;
  // onEnter refused to load this file, so the buffer never held its contents.
  // Writing here would truncate it to zero -- openFileForWrite is O_TRUNC and
  // an empty buffer is a legitimate save (see below), so nothing further down
  // can tell the two apart. Opening a .txt book in the editor and pressing
  // Back destroyed it.
  if (loadRefused) {
    LOG_INF(TAG, "not saving %s: it was never loaded", path.c_str());
    return false;
  }
  // An emptied note still saves: openFileForWrite truncates, so writing zero
  // bytes is the correct way to record "the owner deleted this text". Refusing
  // left the previous contents on the card.
  HalFile f;
  if (!Storage.openFileForWrite(TAG, path, f)) {
    LOG_ERR(TAG, "cannot open %s for write", path.c_str());
    return false;
  }
  const size_t written = f.write(buf->data(), buf->size());
  f.flush();
  savedOk = written == buf->size();
  LOG_INF(TAG, "saved %u/%u bytes to %s", (unsigned)written, (unsigned)buf->size(), path.c_str());
  return savedOk;
}
