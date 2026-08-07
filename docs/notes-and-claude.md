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

All three `SETTINGS.keyboardLayout` choices are honoured:

| Layout | In the panel |
|---|---|
| QWERTY | the SDK's `builtinKeyboardLayout`, number row on |
| 13-Grid | `grid13::SL_LAYOUT` — **extracted** to `src/notes/Grid13Layout.h` so the full-screen activity and the panel share ONE table |
| Daisy | its character groups as flat rings, same multi-tap idiom |

Daisy is the one that is not literally the same UI: its wheel geometry assumes
a full screen and does not fit a half-height strip. The letters, grouping and
order are unchanged; only the arrangement differs. Full-screen text entry
(rename, Wi-Fi password) still shows the real wheel.

**Bluetooth is opt-in.** BLE only starts when a keyboard bond already exists,
because bringing it up costs ~72 KB of heap and a CPU-clock lock — an owner
with no keyboard paired should never pay that. See the heap table in the spike
doc.

Buttons in Create Note / Claude:

| Input | Action |
|---|---|
| Left / Right | move along the keyboard row |
| Confirm | type the selected key |
| Up / Down (short) | move between keyboard rows |
| **Up (hold 1.5 s)** | start Bluetooth and pair a keyboard |
| **Down (hold 1.5 s)** | disconnect the keyboard, **keeping** the bond |
| Back | save and leave |

Forgetting a bond entirely is deliberately NOT a gesture — it is
**Settings → Forget Bluetooth Keyboard**, alongside **Pair Bluetooth Keyboard**.

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
| On-screen keyboard panel | `src/notes/KeyboardPanel.{h,cpp}` |
| 13-Grid layout table (shared) | `src/notes/Grid13Layout.h` |
| Anthropic exchange | `src/notes/ClaudeChat.{h,cpp}` |
| Editor font group | `src/notes/EditorFonts.{h,cpp}` |
| Activities | `src/activities/util/{NoteEditor,ClaudeChat}Activity.{h,cpp}` |

Host tests: `test/{ble_keymap,text_buffer,markdown_spans,note_naming}`.
Run them with `ctest --test-dir build/test`; see the verification tiers in
[ble-editor-spike.md](ble-editor-spike.md).

## Traps

**Adding a home row touches four places.** `menuItems` and `menuIcons` in
`HomeActivity.cpp`, both index maps in `HomeActivity.h`, and
`getMenuItemCount()` which bounds `selectorIndex` — leave that stale and the
row renders but can never be selected. Also add
`lastHomeMenuItem = HomeMenuItem::X;` to the row's `goTo*` wrapper, or Back
will not return the selector to it.

**`nimble_port_init()` needs the CPU at full clock.** `HalPowerManager` drops to
10 MHz after 3 s idle and the BT controller cannot start there — the call never
returns and the watchdog reboots the device. `BleHidHost::begin()` holds a
`HalPowerManager::Lock` across it. This looked like heap nondeterminism for a
long time; it is not.

**One serial port owner.** A background capture holding `/dev/cu.usbmodem*`
resets the chip mid-flash, which presents as "chip stopped responding" a few
percent in and is indistinguishable from failing hardware.
