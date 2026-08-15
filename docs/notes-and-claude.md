# Notes and Claude

Two home-screen features that share a keyboard: **Create Note**, a markdown
note editor, and **Claude**, an on-device Anthropic API client. They are
separate activities with separate flows — the editor knows nothing about the
network, and Claude knows nothing about editing files.

Grew out of the BLE-keyboard feasibility spike; the measurements that justified
shipping it are in [ble-editor-spike.md](ble-editor-spike.md).

## Input: on-screen keyboard by default, Bluetooth optional

The **on-screen keyboard is the default**. It renders into the lower strip of
the screen (`src/notes/KeyboardPanel.{h,cpp}`) with the note text above, so you
see what you type in context — unlike `KeyboardEntryActivity`, which owns the
whole screen and returns a single string.

All three `SETTINGS.keyboardLayout` choices are honored:

| Layout | In the panel |
|---|---|
| QWERTY | the SDK's `builtinKeyboardLayout`, number row on |
| 13-Grid | `grid13::SL_LAYOUT` — **extracted** to `src/notes/Grid13Layout.h` so the full-screen activity and the panel share ONE table |
| Daisy | its petals laid out flat — rings **extracted** to `src/notes/DaisyRings.h`, shared with `DaisyEntryActivity` |

Daisy is the one that is not literally the same UI: its wheel geometry assumes
a full screen and does not fit a half-height strip. The letters, grouping and
order are unchanged; only the arrangement differs. Full-screen text entry
(rename, Wi-Fi password) still shows the real wheel.

Selection within a petal is **direct** — its three characters are three slots
and you pick one, exactly as the full-screen wheel works. An earlier version of
this panel invented a press-repeatedly-to-cycle multi-tap instead, which is not
how Daisy has ever worked here.

Long-press Confirm types the **uppercase** of the selected key, on every layout
(`fui::keyboardAltOutputFor`). On 13-Grid the number row's alts are its symbols,
so long-press `1` gives `!` — the hint is printed in the key's corner.

**Bluetooth is opt-in.** BLE only starts when a keyboard bond already exists,
because bringing it up costs ~72 KB of heap and a CPU-clock lock — an owner
with no keyboard paired should never pay that. See the heap table in the spike
doc.

Buttons in Create Note / Claude:

| Input | Action |
|---|---|
| Left / Right | move along the keyboard row; **auto-repeats** while held |
| Confirm | type the selected key |
| Confirm (hold) | type its uppercase / alternate |
| **Confirm (hold) on SPACE** | Create Note only: enter **caret mode** — see below |
| Up / Down (short) | move between keyboard rows |
| **Up (hold 1.5 s)** | start Bluetooth and pair a keyboard |
| **Down (hold 1.5 s)** | disconnect the keyboard, **keeping** the bond |
| Back | save and leave |

Row navigation is bound to the **physical** Up/Down, not to PageBack/PageForward.
That matters: those two return false when Side Buttons is set to Disabled, which
left the default 13-Grid able to reach only its number row and nothing else, and
made the pairing hold move the selector the wrong way under the NEXT_PREV swap.

Forgetting a bond entirely is deliberately NOT a gesture — it is
**Settings → Forget Bluetooth Keyboard**, alongside **Pair Bluetooth Keyboard**.

### Caret mode (hold the space bar)

**The problem it fixes.** `TextBuffer::cursorLeft/Right/Up/Down/Home/End` were
reachable only from `handleKey`, and `handleKey` is fed only by a paired BLE
keyboard's arrow keys and by a host keyboard's typed text — which carries no
arrows. So with the on-screen keyboard, the only input most owners have, there
was **no way to move the caret at all**: repositioning mid-note meant deleting
back to the spot and retyping.

**The gesture.** Select the space key and hold Confirm. The keyboard stops
taking input and the four direction buttons drive the text cursor instead —
Left/Right by a character, Up/Down by a line. Confirm or Back returns to typing;
Back does **not** leave the editor while the mode is up, the same contract an
`OptionPopup` has. Auto-repeat works while a direction is held.

While it is up the caret is drawn as a 3px **block** rather than a hairline, the
status band carries `STR_KB_HINT_MOVE_CURSOR` in place of the byte count, and
the Confirm hint reads **Done**.

**Why hold-space and not hold-Up**, which is what the full-screen
`KeyboardEntryActivity` uses for its own `cursorMode`: in this editor hold-Up
and hold-Down are already the Bluetooth pairing gestures. Hold-space was free —
`keyboardAltOutputFor` returns `nullptr` for `KeyKind::Space`, so a long press on
it fell back to `keyboardOutputFor` and typed the same plain space a tap does.
That also makes a resulting `' '` an exact sentinel for the gesture, which is how
the editor detects it without KeyboardPanel needing to know anything about it;
`test/keyboard_panel` pins both halves (space still resolves to a space, and no
other key resolves to one).

**A paired BLE keyboard does not need this** and never enters the mode: it has
real arrow keys already wired at `NoteEditorActivity::handleKey`.

**Two known rough edges, neither device-confirmed.**

Up/Down move by HARD lines, because that is what `TextBuffer::cursorUp/Down`
do — from a soft-wrapped line they jump to the previous or next paragraph, not
the previous or next visual row. Changing that means teaching `TextBuffer` about
the renderer's wrap, which it deliberately knows nothing about.

The repaint is debounced exactly like typing (Settings → Typing Redraw Delay),
so a *held* direction steps the caret several characters between refreshes and
you cannot see where it is until you let go. Caret repeat is therefore slower
than the key-grid repeat — 600 ms then 300 ms, against 450/140 — so that roughly
one step lands per ~570 ms panel refresh. **Whether that is pleasant on real
e-ink is untested**; it was verified only on the simulator, which repaints in
milliseconds and cannot tell you.

## Create Note

Makes a **new** note every time: `/YYYYMMDDHHmmss.md` at the card root.

Seconds come from the C library clock (set by `syncFromNTP`); the RTC only
resolves to minutes. On a device whose clock never synced every stamp would be
identical, so `notenaming::uniqueNotePath()` adds a `-1`, `-2` … suffix on
collision. That suffix — not the timestamp — is what actually guarantees a
fresh file, including two taps in the same second.

Editing: cursor movement (arrows, Home/End, Delete), pagination (PgUp/PgDn and
the side buttons), and markdown rendered as you type — `#`–`###` headings bold,
`**bold**`, `_italic_`, `` `code` ``, `-`/`*`/`1.` lists with a hanging indent,
`>` blockquote. Unclosed markers stay literal, because in an editor half a
marker is the normal mid-keystroke state.

**Manage Files → Edit** opens an existing `.md`/`.txt` in the same editor. A
file larger than the 8 KB buffer is refused on open rather than truncated.

## E-ink refresh limits, and what the redraw delay can and cannot do

**Settings → Typing Redraw Delay** (25 / 50 / 100 / 250 / 500 / 1000 ms,
default 250) sets how long typing must go quiet before the panel redraws. It is
worth understanding what it actually controls, because the intuition from an
LCD does not transfer.

### Where the time goes

Measured on the X4 across 190 refreshes this session:

| Stage | Time | Whose |
|---|---|---|
| Compose the frame in RAM (`clearScreen` → `displayBuffer`) | ~8 ms median | software |
| Panel waveform, `FAST_REFRESH` | **496–498 ms** (186 of 190) | hardware |
| Panel waveform, `HALF`/`FULL_REFRESH` | **1639–1674 ms** (17 of 190) | hardware |
| Total keystroke → glyph | 570–573 ms | |

Drawing is about **1.5%** of a redraw. Everything else is the panel physically
moving pigment: e-ink holds its image with charged particles suspended in
fluid, and changing it means driving a multi-phase voltage waveform for
hundreds of milliseconds. No amount of firmware makes that shorter — it is the
display medium, not the code.

`FAST_REFRESH` uses a reduced custom LUT (fewer waveform phases) to get ~496 ms
instead of ~1670 ms, and pays for it in ghosting: faint remnants of previous
pixels. That debt is settled periodically with a full refresh, which is why
~9% of the refreshes above took 1.7 s. **Settings → Refresh Frequency**
controls how often that happens.

The framebuffer is also single (`EINK_DISPLAY_SINGLE_BUFFER_MODE=1`, 48 KB, no
double buffering on the C3), so it must not be touched while a refresh is in
flight. A redraw is therefore not just slow, it is exclusive.

### What this means for the delay setting

Until 2026-08-06 the delay reached only Bluetooth typing: both editors read it
inside the `blekbd` drain, while the on-screen keyboard repainted
unconditionally. With no keyboard paired — the out-of-the-box state — the
setting did nothing at all. Panel typing now takes the same path; layer switches
and daisy ring swaps still repaint at once, since there is nothing to batch.

The delay is a **batching** control, not a latency control. It decides how many
keystrokes share one ~500 ms refresh:

- **Long (250–1000 ms)** — a burst of typing collapses into one refresh. Text
  appears in chunks, well behind your fingers, but the panel is idle most of
  the time.
- **Short (25–50 ms)** — near one refresh per keystroke. This does **not** make
  a character appear in 50 ms; the character still takes ~500 ms to become
  visible. What changes is that the *next* keystroke lands while the panel is
  mid-waveform, so redraws queue back-to-back and the display is almost
  continuously refreshing.

Short settings therefore cost real things: markedly more panel energy per
character typed, ghosting accumulating faster (so the 1.7 s full refreshes come
round sooner), and no throughput gain, because ~500 ms per refresh is a hard
floor regardless of how eagerly it is requested.

The default is **250 ms**: the value that collapses a normal typing burst into
one refresh, chosen for typing feel over apparent immediacy. Shorter values are
there if you want them, but they buy responsiveness that the panel cannot
actually deliver.

### The lever that would actually help

Partial-region refresh — redrawing only the changed line rather than all
800×480 — is the only thing that meaningfully beats ~500 ms. A couple of 141 ms
refreshes appear in the same logs, so the panel can do shorter cycles for
smaller work. The editor does not use it: it composes a whole frame each time.
That is the real optimisation if typing latency ever needs to improve, and it
is not done.

## Claude

Type a question, press Ask. The exchange appends to `/claude-chat.md`:

```
## 2026-08-06 04:29 UTC — me

Name one risk of running TLS on an ESP32-C3 with 80KB free heap.

## 2026-08-06 04:29 UTC — claude (claude-haiku-4-5)

...
```

Errors are recorded too, so a failed attempt leaves a trace rather than
vanishing. Prior turns replay as `messages` (capped 6 turns / 4000 chars)
because the API is stateless and without it the second question in a thread
gets "I don't have context about what you're asking".

The radios take turns — BLE down, WiFi up, POST, WiFi off, BLE back — because
they do not fit in heap together. Measured on device: TLS completes with
14,780 B free.

**Setup:** put your API key in `/claude-key.txt` on the card (one line), and
join the WiFi network once so the credential is stored.

### Security posture — read before shipping this

`SecureHttpClient::setInsecure()` is used, matching the rest of the firmware's
HTTPS (`HttpDownloader`, OTA): no CA bundle is wired into the wolfSSL transport,
so the peer certificate is **not verified**. That is acceptable for OTA over a
trusted network; it is weaker than it should be for a request carrying an API
key. The key also sits in plaintext on a removable card. Both are known and
neither is fixed. Do not treat this as production-grade until a CA bundle is
wired up and the key has somewhere better to live.

## Where things live

| Concern | File |
|---|---|
| BLE HID host (scan, bond, subscribe, decode) | `src/notes/BleHidHost.{h,cpp}` |
| HID report decode (host-tested) | `src/notes/HidKeymap.h` |
| Editable buffer + cursor (host-tested) | `src/notes/TextBuffer.h` |
| Markdown spans (host-tested) | `src/notes/MarkdownSpans.h` |
| Note filenames (host-tested) | `src/notes/NoteNaming.h` |
| On-screen keyboard panel (host-tested) | `src/notes/KeyboardPanel.{h,cpp}` |
| 13-Grid layout table (shared) | `src/notes/Grid13Layout.h` |
| Daisy rings (shared) | `src/notes/DaisyRings.h` |
| Anthropic exchange | `src/notes/ClaudeChat.{h,cpp}` |
| Editor font group | `src/notes/EditorFonts.{h,cpp}` |
| Activities | `src/activities/util/{NoteEditor,ClaudeChat}Activity.{h,cpp}` |

Host tests: `test/{ble_keymap,text_buffer,markdown_spans,note_naming,keyboard_panel}`.
Run them with `ctest --test-dir build/test`; see the verification tiers in
[ble-editor-spike.md](ble-editor-spike.md).

## Traps

**Adding a home row touches four places.** `menuItems` and `menuIcons` in
`HomeActivity.cpp`, both index maps in `HomeActivity.h`, and
`getMenuItemCount()` which bounds `selectorIndex` — leave that stale and the
row renders but can never be selected. Also add
`lastHomeMenuItem = HomeMenuItem::X;` to the row's `goTo*` wrapper, or Back
will not return the selector to it.

**A key's `output` field can be NULL on a key that still types.** The SDK's
space key is `KeyKind::Space` with `output == nullptr`; `keyboardOutputFor()`
synthesises the `" "`. Reading `key.output` directly therefore compiles, renders
a perfect space bar, and types nothing — which is exactly what shipped in QWERTY
and both symbols pages. Always go through `keyboardOutputFor()` /
`keyboardAltOutputFor()`. Covered by `test/keyboard_panel`.

**`InteractionBuffer<N>` overflows silently.** `addInteraction()` returns false
and sets an `overflowed_` flag nobody reads. Size N from the LARGEST layout the
activity can show — 13-Grid is 55 keys, not QWERTY's 41 — or the trailing keys
lose touch registration with no warning at build or run time.

**Special keys are UPPERCASE.** `DEL`, `SPC`, `RET`, `ASK`, `SHIFT`, `ABC` —
lowercase reads as characters you could type. Any new special key follows it;
`test/keyboard_panel` enforces it over the shared layout tables. Space needs an
explicit label at every draw site, not the character itself: rendering `' '`
draws nothing and the key looks dead.

**An ENUM settings row must be sized with `enumCount()`, never
`enumValues.size()`.** A row supplies EITHER translated `enumValues` or runtime
`enumStringValues`, and a row of the second kind leaves `enumValues` EMPTY.
Three places sized off it directly and all three saw zero for Typing Redraw
Delay and Editor Font: the popup gate never opened a picker, the fall-through
toggle computed `(v + 1) % 0` — undefined behavior that on RISC-V returns the
dividend rather than trapping, so the index walked past the label list until the
value column rendered blank — and `fromJson`'s clamp `val < 0` was never true,
so every boot discarded the saved byte. If you add a runtime-labeled row, the
`valuePtr` branch also needs the `enumStringValues` overload of
`OptionPopup::show`, or the picker opens empty.

**A settings row is invisible unless its category is `STR_CAT_SYSTEM`.**
`rebuildSettingsLists()` drops everything else. Editor Font carried
`STR_CAT_READER` and so existed only in the web API — the row was real,
persisted, and unreachable on an X4 or X3.

**Editor fonts are BUILT IN, and the fallback is monospace.** All three rows
compile into the firmware at TWO sizes — 12 and 14 pt (owner ruling
2026-08-15), four styles each. iA Writer Quattro is OFL and always present;
PragmataPro and Nitti Typewriter are commercial, so their glyph tables are
gitignored and `main.cpp` gates them on `__has_include` — a clone without the
licensed sources still compiles and simply does not register those rows, which
is why Quattro is also `fallbackFontId()`. One `__has_include` covers both
sizes: `convert-builtin-fonts.sh` generates every size in `EDITOR_SIZES`
together, so a tree with the 12 pt headers has the 14 pt ones too.

None are installed to `/fonts` — but the reason has changed, and the old one is
no longer true. It used to be that `SdCardFontRegistry` had no exclusion
mechanism, so an install would surface these writing faces in the READING
picker. There IS one now: `readingfonts::offeredForReading()` filters both
reading pickers (`FontSelectionActivity.cpp:136`,
`EpubReaderActivity.cpp:820`), and `FORMER_WRITING_FAMILIES` keeps retired
faces filtered too. So installing them is *possible*; it simply has not been
ruled on, and `docs/sd-card-fonts.md` requires a ruling before any family joins
a card. `cycleReaderFontFamily()` and the section-cache invalidation it can
trigger are still the reason to be careful about it.

**The SIZE is persisted as a real point size, not a picker index.**
`SETTINGS.editorFontSize` holds 12 or 14. Its `getSettingsList()` row is a
getter/setter entry with `valuePtr` left **null** — `CrossPointWebServer`'s ENUM
case prefers `valuePtr` over the setter and would write the raw index into the
byte — which means the generic `toJson` loop does not carry it, so
`CrossPointSettings` writes and reads `editorFontSize` by hand, exactly as
`fontSizeSlot` and `screenMargin` do. Leave either of those explicit lines out
and the row keeps working perfectly until the next reboot, when it silently
reverts. That is the trap CLAUDE.md documents, and putting the size on an index
would have re-created on the size axis the identical bug the FAMILY axis was
rescued from hours earlier.

A stored size that is not offered is snapped by
`editorfonts::nearestOfferedSize()` (ties go to the smaller size) rather than
clamped away, so the picker and the renderer cannot disagree about what a
stored byte means. The snap is in-memory: a settings.json holding 13 keeps
saying 13 on disk until something else triggers a save, while the device reads,
draws and displays 12.

**13 pt was asked for and declined** in the same ruling that added 14. No size
ramp anywhere carries a 13, and `SdCardFontSystem::loadForDisplay` snaps to the
nearest size it finds (`findNearestSize`) rather than failing — so a 13 row
would have rendered 12 while the label said 13. That is the same
silently-does-nothing shape this file documents twice already, and dropping the
size removed it rather than managing it.

**The list was cut twice on 2026-08-15, and the second cut changed how the
setting is persisted.** First Space Mono went, absorbed by shifting the stored
POSITION. Then the list was cut to three — iA Writer Quattro, PragmataPro, Nitti
Typewriter — dropping iA Writer Duo, iA Writer Mono and IBM Plex Mono.

The position shift was a mistake and is gone. `SETTINGS.editorFont` is a byte
that never recorded which table it indexed, the shift ran unconditionally on
every load, and the corrected value was written back by the ordinary `valuePtr`
loop — so it re-applied on the next boot and walked a saved choice down the list
one row per reboot (pick PragmataPro, get IBM Plex Mono two reboots later).

**The editor font now persists by family NAME**, under the settings key
`editorFontFamily`, exactly as `sdFontFamilyName` and `language` already do. The
`editorFont` byte is still written — the web settings API reads and writes the
row through `valuePtr`, and an older build reads the byte — but `fromJson()`
believes the name whenever it is present. So removing or reordering a writing
face needs no migration at all, and the list is no longer order-frozen.

`editorfonts::migrateLegacyStoredIndex()` is the one-shot rescue of bytes
written before the name key existed. Three things about it are load-bearing:

* It runs in `fromJson()`, not `normalizeRetiredSettings()`, because only
  `fromJson()` can see the document and gate on the name key being ABSENT. It
  sets `needsResave`, so the name is written immediately and the function is
  unreachable for that device afterwards — that gate, not idempotence, is what
  stops the re-application.
* It reads the RAW `doc["editorFont"]`. The generic ENUM loop clamps against the
  current `FAMILY_COUNT` (3) before anything else runs, so by then every value
  worth migrating has already been replaced with the default.
* It decodes the SIX-row table, and lands Duo and Mono on Quattro (one
  superfamily, differing only in spacing) and IBM Plex Mono on PragmataPro (the
  nearest surviving engineered mono). A byte from the seven-row table that
  preceded it can only exist on a device that has not booted since 02:53 that
  day; for those, values 4/5/6 land on PragmataPro, Nitti and the default — all
  surviving writing faces.

Every removed family keeps its recipe, its picker label and its generated
headers; only the wiring goes. `isWritingOnlyFamily()` also keeps answering true
for them, via `FORMER_WRITING_FAMILIES`, so a card that happens to carry one
does not quietly grow the READING picker.

**The device reads `/.crosspoint/settings.json`, not `settings.json` at the card
root.** A file written to the root is silently ignored, which makes a settings
change look like it did not apply.

**Daisy never shows the backspace icon.** Its strip draws the literal text
`del`, and `KeyboardPanel`'s daisy branch returns before the FreeInkUI target is
even constructed. The backspace art only reaches 13-Grid and QWERTY.

**`nimble_port_init()` needs the CPU at full clock.** `HalPowerManager` drops to
10 MHz after 3 s idle and the BT controller cannot start there — the call never
returns and the watchdog reboots the device. `BleHidHost::begin()` holds a
`HalPowerManager::Lock` across it. This looked like heap nondeterminism for a
long time; it is not.

**One serial port owner.** A background capture holding `/dev/cu.usbmodem*`
resets the chip mid-flash, which presents as "chip stopped responding" a few
percent in and is indistinguishable from failing hardware.
