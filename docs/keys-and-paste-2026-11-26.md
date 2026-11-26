# Keys and tokens: Settings rows, storage, paste (2026-11-26)

Owner, 2026-11-26: add a way to paste API keys and tokens into the firmware's
own Settings screen, and make paste work in the iOS app. Three passes: survey
and design, build, adversarial review. Worktree pair `key-paste`, branched from
`picco-fix` at firmware `21eed8e86` / simulator `cd32c0f3` (the primary `main`
was 173 commits behind and lacks the Picco work). Nothing committed.

## Pass 1: survey

Method: `grep -rn -i -E "githubToken|claude-key|api.?key|x-api-key|Authorization|Bearer|password|secret|credential" src lib/hal`,
then every hit read. Simulator: `src/SimHostSettings.h`,
`ios/CrossPointHostSettings.mm`, `ios/Settings.bundle/Root.plist`.

### Every credential the firmware reads

| Credential | Read by | Sent as | Where it is stored today |
|---|---|---|---|
| GitHub token | `network/GithubAuth.h` `githubauth::tokenValue` | `Authorization: Bearer` from `LibraryUpdater.cpp:94,167,516` and `FontUpdater.cpp:162,223,734` | three sources, in order: (1) iOS: NSUserDefaults `githubToken` via `sim_host_settings::githubToken` (Settings.app row, `IsSecure`); (2) `/github-token.txt` on the card root; (3) `SETTINGS.githubToken[104]`, persisted to `/.crosspoint/settings.json` by `CrossPointSettings.cpp:90-91` and read back at `:311-313` |
| Claude API key | `notes/ClaudeChat.cpp:123` `readApiKey` | `x-api-key` header, `ClaudeChat.cpp:310` | (1) iOS: NSUserDefaults `claudeApiKey` (Settings.app row); (2) `/claude-key.txt` on the card root. Not in settings.json |
| Wi-Fi passwords | `WifiCredentialStore` | WPA join | `/.crosspoint/wifi.json`, XOR with the MAC and base64 (`WifiCredentialStore.h`: "not cryptographically secure") |
| HTTP Basic auth | `HttpDownloader.cpp:104-107` | `Authorization: Basic` | no caller passes a username or password (the OPDS client that did was removed); dormant |

The desktop simulator also has env hatches: `CROSSPOINT_SIM_GITHUB_TOKEN`,
`CROSSPOINT_SIM_CLAUDE_KEY` (`SimHostSettings.h:72,74`).

### Where each copy is exposed today (verified against source)

| Copy | HTTP File Transfer `/download` | WebDAV | Web settings API |
|---|---|---|---|
| `/github-token.txt`, `/claude-key.txt` (card root) | **served**: not dot-named, not in `HIDDEN_ITEMS` (`CrossPointWebServer.cpp:77,116-147`) | **served** | n/a |
| `/.crosspoint/settings.json` (`githubToken`) | refused (dot path, `isProtectedPath`) | **served**: WebDAV mirrors dot paths on purpose (`WebDAVHandler.cpp:914-919`) | not served: `githubToken` is not a row in `getSettingsList()` |
| `/.crosspoint/wifi.json` | refused | **served** (obfuscated only) | passwords never returned (`SettingsPage.html:562-564`) |
| iOS NSUserDefaults | not on the card | not on the card | n/a |

Two more exposures, recorded, not in the ask:

- **TLS peer verification is off** for both credentials' requests:
  `HttpDownloader.cpp:90` and `ClaudeChat.cpp:306` call `setInsecure()`; the
  latter's own comment says "NOT shippable with an API key". A man in the
  middle on the Wi-Fi gets the token or key in the header. Proposal, not part
  of this change unless ruled in: pin the CA for `api.github.com` /
  `objects.githubusercontent.com` / `api.anthropic.com`.
- The web settings API serves every `STRING` row's value (`CrossPointWebServer.cpp`
  `handleGetSettings`, `case SettingType::STRING`). So a key row must never be
  a `STRING` setting.

### Paste on the iOS app today: it does not reach a firmware field (proved)

iPhone Air simulator, Release, picco-fix build, real UIKit input through the
scratch XCUITest `Tapper`. Home -> Claude (a multi-line firmware text field),
keyboard chip -> the system keyboard up (`[TEXT] host text input started
(screen keyboard support 1, keyboard attached 0)`).

- Positive control: two real key taps on the system keyboard (Q, W) appear in
  the firmware's field ("Qw"), so typed text reaches `consumeTypedText`.
- The pasteboard was then set to `FAKE-TEST-0000` and:
  - the system keyboard has **no Paste key** (`app.keyboards.buttons["Paste"]`
    does not exist);
  - Command-V (`typeKey("v", .command)`) added **nothing**; the field still read
    "Qw".
- Not tested: a long press. SDL's text field is hidden, so there is nothing for
  UIKit's edit menu to attach to, and a long press on the page is the Picco's
  hold = Confirm. Not tested: a real Bluetooth keyboard's Command-V (the
  simulator had none attached; `keyboard attached 0`).

So paste needs a control of its own: a Paste button on the keyboard bar
(`ios/CrossPointKeyboardBar.mm`) that reads `UIPasteboard` and injects the
text down the same path typed text takes (`HalGPIO::injectTypedText`).

### A key on the hardware, without a clipboard

The device's web page takes Wi-Fi passwords (`POST /api/wifi`, write-only:
never pre-filled, blank keeps the old one) but has no endpoint for a key or
token. Today the only way is to drop `/claude-key.txt` or `/github-token.txt`
on the card, which is exactly the copy File Transfer and WebDAV serve back.
Proposal: a write-only `POST /api/keys` (`{name, value}` or `{name, clear}`),
and `GET /api/keys` returning only `{name, set, last4}`; a "Keys" section on the
settings page with password inputs, modeled on the Wi-Fi section. Cost to
weigh: the POST travels in plain HTTP over the LAN (no TLS on the device's
server), the same exposure a Wi-Fi password sent through that page has today;
lower if done over the device's own hotspot.

### Storage options (the architecture question)

| Option | Device (X4/X3/Picco) | iOS | Cost |
|---|---|---|---|
| A | NVS via `Preferences` (namespace `keys`); `lib/hal/HalGPIO.cpp:54` already uses NVS | Keychain (`kSecClassGenericPassword`, `AfterFirstUnlockThisDeviceOnly`) | Off the card on both, never served. Keychain: encrypted at rest, out of backups, survives an app delete. Settings.app cannot write the Keychain, so its GitHub Token / Claude API Key rows would retire, and entry is the firmware row only (paste makes that practical). ~150 lines ObjC + a migration from NSUserDefaults |
| B | NVS via `Preferences` | NSUserDefaults, the existing host-settings channel | Off the card on both. NSUserDefaults is a plaintext plist in the app container, included in backups; the Settings.app rows keep working and the firmware rows write the same keys. Least new code |
| C | `/.crosspoint/keys.json`, XOR+base64 like `wifi.json`, and a new block on that path in WebDAV and HTTP | NSUserDefaults | Stays on the card (moves with it); obfuscation is not encryption; relies on a path block staying correct in two servers |

NVS facts for A/B: partition `nvs` is 0x5000 (20 KB) on the C3 and 0xC000 on
the Picco (`partitions*.csv`); plaintext in flash on this unit (no flash
encryption, see the port doc), so readable with the device, USB and esptool.
`initArduino()` erases NVS on `NO_FREE_PAGES` / `NEW_VERSION_FOUND`
(port doc), which would drop the keys with it.

Migration in every option: on first boot, a key found in `/github-token.txt`,
`/claude-key.txt` or `settings.json`'s `githubToken` moves to the new store and
the old copy is removed (the file deleted, the field blanked and the settings
saved); on iOS, NSUserDefaults values move likewise under A.

Later todo (owner, same message): reduce the delay for keyboard entry.

## Rulings

- **2026-11-26, storage: "NVS + NSUserDefaults".** Device: ESP32 NVS
  (`Preferences`), off the card. iOS: the existing host-settings channel
  (NSUserDefaults keys `githubToken`, `claudeApiKey`); the Settings.app rows
  stay, and the firmware's own rows write the same keys. Desktop simulator: a
  file beside its `settings.json`, outside the simulated card. Do not re-ask.
- **2026-11-26, web entry: "Build it now".** A write-only Keys section on the
  device's settings page: `POST /api/keys` sets or clears, `GET /api/keys`
  returns only whether each key is set and its last 4. Do not re-ask.
- **2026-11-26, display: "do not wrap long secret inputs or outputs".** Anywhere
  a key or token is shown masked, it stays on ONE line, inputs and outputs
  alike. The Secret entry field shows a fixed mask and the length, never one
  `*` per byte. Do not re-ask.
- **2026-11-26, compatibility: "never worry about backwards compatibility
  because i am the only user and i do not care. spending any time on that is
  a waste while this is in alpha".** Supersedes the original "existing
  installs keep working: migrate, then remove the old copy" constraint. Do not
  re-ask.
- **2026-11-26, old key files on the card: "Delete at boot".** With no
  migration, an old `/github-token.txt` or `/claude-key.txt` is ignored but
  would still be served by File Transfer and WebDAV, so every boot deletes both
  if present, unread (`keystore::removeLegacyCardFiles`, called right after the
  settings load). An old `githubToken` in settings.json is dropped at the next
  settings save, since the field no longer exists. Do not re-ask.
- **2026-11-26, the BLE report dump: "Remove the dump".** `BleHidHost.cpp`
  hex-dumped every Bluetooth keyboard report at `LOG_DBG`, so a key typed on a
  BLE keyboard was readable from a development build's serial log. Removed;
  the notify count stays. Do not re-ask.
- **2026-11-26, cross-origin reads of `/api/keys`: "Leave it".** The server
  allows every origin, so any page open on the LAN can read whether each key is
  set and its last 4. Accepted as is; cross-site writes stay blocked (415 on a
  non-JSON POST, 403 on the preflight). Do not re-ask.
- **2026-11-26, the desktop key file: "Keep it, ignored".** `host-keys.txt`
  stays beside the desktop simulator's settings.json (in a repo root when run
  from one), git-ignored in both repos and gated by `tools/key_store_check.sh`.
  Do not re-ask.

## Pass 2: what was built

Firmware (`key-paste` worktree):

- **Key store**: `src/util/KeyStoreCore.h` (pure: key names, trimming and
  refusal, the last-4 rule, the entry mask, the migration plan and runner) and
  `src/util/KeyStore.h` (storage: NVS namespace `cpkeys` on the device; the
  host settings surface in the simulator). Header-only, so no change to the
  simulator's iOS source list.
- **Every reader now reads the store**: `network/GithubAuth.h`
  (`tokenValue`, used by Update Library and Update Fonts) and
  `notes/ClaudeChat.cpp` (`readApiKey`). The card files and the settings field
  are no longer read anywhere else.
- **Never on the card**: `CrossPointSettings::toJson` no longer writes
  `githubToken`; `fromJson` still reads an old value only for the migration.
- **Migration** (REMOVED in pass 3 by the no-backwards-compatibility ruling;
  this is the record of what pass 2 built) (`keystore::migrateLegacyCopies`, called in `setup()` right
  after `SETTINGS.loadFromFile()`): `/github-token.txt`, `/claude-key.txt` and
  settings.json's `githubToken` move into the store and are removed. A card file
  wins (the old precedence); settings.json only fills an empty store; nothing
  is removed unless the store holds the value afterwards. It runs every boot,
  so a key file dropped on the card later (by the old instructions) is picked
  up and removed too.
- **Settings rows**: GitHub Token and Claude API Key, after Device owner.
  Value column: "Not set", "Saved" (under 12 characters), or "Ends in WXYZ".
  Nothing set: the row opens the text entry; set: a popup, Enter or paste /
  Clear. A value the store refuses (empty, a space or control character
  inside, over 255) is not saved and a popup says why.
- **Text entry**: `InputType::Secret` in both keyboards
  (`KeyboardEntryActivity`, `DaisyEntryActivity`): every byte shown as `*`
  except the last 4 once 12 or longer, no reveal toggle, no last-typed reveal,
  no cursor mode (it draws the character under the cursor).
- **Web page**: `GET /api/keys` (name, set, last4 per key) and
  `POST /api/keys` (`{name, value}` or `{name, clear: true}`; the reply carries
  set/last4 only, errors never echo the body), and a "Keys and Tokens" section
  on the device's settings page with write-only password inputs.
- **Strings**: 9 new `STR_*` in English and Spanish; the two "token needed"
  hints now point to Settings > GitHub Token; Claude's no-key error is
  `tr(STR_CLAUDE_NEEDS_KEY)`.

Simulator / iOS app:

- `src/SimHostSettings.h`: `setGithubToken` / `setClaudeApiKey` (null or empty
  clears). Desktop: `host-keys.txt` beside the simulated card (never inside it),
  owner-only, the QA env vars still win. iOS (`ios/CrossPointHostSettings.mm`):
  the same NSUserDefaults keys the Settings.app rows write.
- `ios/CrossPointKeyboardBar.mm`: a Paste chip left of Hide. It reads
  `UIPasteboard` (iOS shows its own Allow Paste prompt), trims surrounding
  whitespace and line breaks, and injects through `HalGPIO::injectTypedText`.
  Logs the length only.
- **A leak closed**: `HalGPIO::injectTypedText` logged dropped text in full
  (`[TEXT] dropped injected text ... "%s"`), which the iOS diagnostics tee
  writes to `diagnostics/firmware.log`; a paste with no field open would have
  written a key there. Now the byte count only.

## Verification (fake values only)

- `test/key_store` (gtest, 14 cases): names and NVS-length limits, trimming and
  refusal, last-4 and the entry mask, the migration truth table and runner
  against fakes in every order (file only, settings only, store kept, a failed
  write removes nothing, unusable legacy values removed), a capturing
  `Logging.h` that proves no fake value (or a 16-character prefix of one)
  reaches any log line, and a source gate that `toJson` cannot write the token.
- Simulator `tests/host_settings_test.cpp`, new `testDesktopKeyStore`: set,
  read, clear one keeping the other, env hatch still winning, a line break
  refused, the file outside the card and mode 0600. `run_all.sh -k
  host_settings`: both arms pass; `chip_tint`, `panel_source` pass.
- `tools/key_store_check.sh` (desktop Picco simulator, fresh card per case),
  12/12: migration removes both card files and the settings field, the store
  holds the values (the card file's token winning), the store is 0600, no fake
  value anywhere on the card or in the log; Settings > GitHub Token typed entry
  saves, the row then reads "Ends in QRST", nothing on the card or in the log.
  Screenshots looked at: the row's last 4, the popup, the masked field.
- Web, desktop simulator via File Transfer (hotspot mode): GET returns only
  set/last4; a value with a space is refused with a message that does not echo
  it; an unknown name is 400; POST set and clear work; `/api/settings` mentions
  neither key; the settings page carries the Keys section; no fake in the log.
- **iOS paste, real UIKit input** (iPhone Air simulator, Release, the scratch
  XCUITest): Home -> Settings -> a key row -> the keyboard chip -> the
  pasteboard set to a fake with leading spaces and a trailing newline -> the
  Paste chip (exists, tapped) -> iOS's Allow Paste prompt (shown, allowed).
  The harness logged `paste: 41 bytes` (the fake, trimmed), the firmware field
  showed only asterisks and the last 4, and `diagnostics/firmware.log` holds no
  fake value. The run landed on the Claude API Key row (iOS has one row fewer
  above it than the desktop); both rows are the same code.
- Builds: `simulator_x3`, `simulator_picco`, `simulator` and the iOS Picco app.

## Pass 3: adversarial review (2026-11-26)

A read-only agent that did not write the code tried to leak a fake key through
every path and to disprove its own findings before reporting. Six findings
survived. Each one below was reproduced (or confirmed in source) before
anything was changed. Two owner rulings arrived mid-pass and changed the
scope: "do not wrap long secret inputs or outputs" and "never worry about
backwards compatibility" (both under Rulings above). Fake values only,
throughout.

Device builds of the pass-2 tree, before any pass-3 change (for comparison):
`picco` RAM 72,240 B / Flash 4,677,362 B (+48 B / +9,428 B on the `picco-fix`
baseline); `default` RAM 59,780 B / Flash 4,870,473 B (+0 B / +9,988 B).

### Findings and what was done

1. **Desktop keys landed in a repo root, not git-ignored** (would-ship-a-leak;
   both forks are public). Reproduced: `git check-ignore host-keys.txt`
   returned nothing (exit 1) in the firmware worktree. `desktopKeyFilePath()`
   puts the file beside the card, and with the default `./fs_` card that is the
   current directory, which is the repo root when the simulator is run from
   there. **Fixed**: `host-keys.txt` is ignored in the firmware `.gitignore`
   (unanchored, so it is ignored at the root and in any subdirectory) and in the
   simulator's `.gitignore`. Gate: `tools/key_store_check.sh` case 0 runs
   `git check-ignore` in both repos. The location stays "beside its
   settings.json", as the storage ruling says. A per-user location (never a
   repo directory) was considered and NOT adopted, because it would contradict
   that ruling. See open question 1.
2. **iOS: a stale card file replaced the working Settings.app key.**
   Reproduced as a failing test (`SimulatorHostKeyIsNotReplacedByAStaleCardFile`
   failed with `importFile = fileHas`). A fix was written and proved by
   mutation. Then **dropped by the 2026-11-26 no-backwards-compatibility
   ruling**: the migration is gone, so no card file is read at all.
3. **The iPhone keyboard autocapitalized and autocorrected a typed key,
   hidden by the mask.** Confirmed in source: `SDL_StartTextInput(window)` with
   no properties gets `SDL_TEXTINPUT_TYPE_TEXT`, whose defaults are sentence
   capitalization and autocorrect ON (`SDL_video.c:5745-5764`). Pass 1's probe
   typed q, w and got "Qw". **Fixed**: a third text-entry kind,
   `TextEntryLines::SingleSecret` (firmware `lib/hal/HalGPIO.h`, a no-op on
   device; simulator `TextEntryKeyRouting.h` `textentry::Lines`). Both keyboards
   pass it for `InputType::Secret`. The simulator's `pumpHostTextInput` then
   starts SDL text input with type `PASSWORD_VISIBLE`, capitalization NONE and
   autocorrect off (`textentry::literalKeyboard`). Return routing is unchanged:
   `SingleSecret` routes like `Single` (`tests/text_entry_enter_test.cpp`,
   six states, pass). Why VISIBLE and not HIDDEN: HIDDEN sets UIKit's
   `secureTextEntry`, and a secure field clears itself on the first keystroke
   after it regains focus. SDL's `textFieldTextDidChange`
   (`SDL_uikitviewcontroller.m:568-585`) reads that clear as up to 64
   backspaces, so after a keyboard re-raise it would erase the key being
   typed. The firmware masks the field itself.
   **Proved on the iPhone Air simulator with real UIKit input** (the scratch
   XCUITest; the app's own input script navigates to the row): with the field
   empty, the system keyboard shows lowercase keys (sentence capitalization
   shows capitals there) and no suggestion bar. Typing t, e, h, then space,
   then committing stored exactly `teh`: the first letter stayed lowercase and
   the space did not autocorrect it. The firmware log carries `text entry
   active (secret)`. Not run: an autocorrect control on the old code (pass 1's
   "Qw" is the capitalization control). Not checked: whether iOS keyboard
   learning is fully off without `secureTextEntry`. Apple ties guaranteed
   no-learning to secure fields, so this is UNCONFIRMED.
4. **The migration deleted a card file it could not import.** Reproduced as
   failing tests: (a) a store that already held an old key plus a failed write
   deleted the file; (b) an unusable file (two lines) was deleted. Also found
   in this pass: `cardsecret::readOneLine` read only 255 bytes, so an over-long
   file would have been imported truncated and then deleted. Fixes were
   written and each proved by mutation. Then **dropped by the 2026-11-26
   no-backwards-compatibility ruling** (no migration).
5. **The settings.json fallback did not protect the token.** Confirmed in
   source: `PersistableStore::loadFromFile` resaves (`requestResave`) before
   the migration ran (`lib/Serialization/PersistableStore.h:125`), and pass 2's
   `toJson` no longer wrote the field, so a failed store write left the token
   only in RAM. A fix (`toJson` writing the field only while non-empty) was
   written and proved by mutation. Then **dropped by the 2026-11-26
   no-backwards-compatibility ruling**. The field is gone (below).
6. Smaller:
   - **Desktop env var blanked an unstored settings token** (QA path only).
     A fix (skip a key whose hatch is set) was written. Then **dropped by the
     2026-11-26 no-backwards-compatibility ruling** (no migration).
   - **Rolling reveal in the entry field** (all but the first 8 characters
     across frames). **Fixed**, together with the no-wrap ruling: the Secret
     field shows `keystore::secretEntryText`, which is 8 `*`, two spaces and
     the length through `tr()` (`STR_KEY_LENGTH_ONE` "1 character",
     `STR_KEY_LENGTH_FORMAT` "%u characters"; Spanish "1 carácter" /
     "%u caracteres"). No character of the key appears, the length counts code
     points, and the text is bounded (at most 8 + 2 + "319 characters"). The
     cursor stays at the end. In a Secret field a tap on the text field no
     longer places the cursor (`KeyboardEntryActivity::loop`), and `render()`
     draws the cursor at the end of the display string (`shownCursor`), so the
     display need not line up with the text byte for byte. Backspace and
     Hold DEL are unchanged. If a narrow panel cannot fit the text,
     `displayTextForCurrentState` drops leading `*`. The Daisy wheel's field was
     already one line and clips from the front. The saved row still shows "Ends
     in WXYZ" (the owner's spec).
   - **Cross-site writes to /api/keys** (integrity). Confirmed, and the review
     undersold it: the device answered EVERY preflight, because
     `WebDAVHandler::canHandle` claims all OPTIONS (`WebDAVHandler.cpp:30-45`)
     and answers 200, with `enableCORS(true)` adding `Allow-Origin`,
     `Allow-Methods` and `Allow-Headers: *`. So requiring JSON alone would have
     closed nothing. **Fixed in two parts**: `POST /api/keys` refuses anything
     but `Content-Type: application/json` with 415, which stops the
     no-preflight "simple" request (Content-Type is now in `collectHeaders`).
     And WebDAV no longer claims `OPTIONS /api/keys`, so it reaches
     `handleNotFound`, which answers 403, and the browser stops. Measured on the
     desktop simulator: text/plain POST 415, no Content-Type 415, JSON POST 200;
     preflight `/api/keys` 200 before this change, 403 after; preflight
     `/api/settings` 200 and WebDAV `OPTIONS /` (`DAV: 1`) unchanged. Left
     open: `GET /api/keys` (set and last 4 only) is still readable from another
     origin (open question 2).
   - **BLE keyboard hex dump** (`BleHidHost.cpp:589-592`, `LOG_LEVEL >= 2`
     only). Documented, not changed: every keystroke from a BLE keyboard,
     including a typed key, is decodable from the serial log of a `default`
     (development) build. Skipping it only for Secret fields means wiring
     text-entry state into the BLE task, which is not one line. Release builds
     (`gh_release`, LOG_LEVEL 0) do not compile it. Open question 3.
   - **`autocomplete="off"` is ignored on a password input.** **Fixed**:
     `autocomplete="new-password"`. The key's state cell is also
     `white-space:nowrap` (the no-wrap ruling). Measured in the served page.
   - **The iOS clipboard still holds the key after a paste.** Documented, not
     changed (clearing someone's clipboard is a surprise of its own).

### What the no-backwards-compatibility ruling removed

Removing the migration was less code than keeping it, so it was removed:
`keystore::migrateLegacyCopies`, `migrateKey`, `planMigration`,
`MigrationPlan`, `legacyCardFile`, the `main.cpp` call, the
`CrossPointSettings::githubToken` field and its `fromJson` read and `toJson`
write, `src/util/CardSecret.h` (no users left), the migration tests and their
log-capturing stub, and the migration half of `tools/key_store_check.sh`.
`KeyStoreCore.h` now logs nothing.

What that means on a card: `/github-token.txt`, `/claude-key.txt` and a
`githubToken` in `settings.json` are **ignored**. Nothing reads a key from the
card, and nothing writes one there. An old `githubToken` drops out of
`settings.json` at the next settings save, because `toJson` builds the file
afresh. The two old card FILES stay where they are until someone deletes them,
and File Transfer and WebDAV still serve them (see open question 4). The key
must be entered again in Settings, on the web page, or (iOS) the Settings app,
whose rows write the same NSUserDefaults keys.

### Tests and gates (pass 3)

- `test/key_store` (8 cases): names and NVS limits, normalize, last 4, the
  one-line entry text (for every length 1..319: no newline, no key byte, the
  length shown, bounded; code points), and two source gates.
  `SettingsCarryNoToken`: neither `CrossPointSettings.cpp` nor `.h` names
  `githubToken` or `claudeApiKey`. `NothingReadsTheOldCardFiles`: no key-store
  file, `GithubAuth.h`, `ClaudeChat.cpp` or `main.cpp` names either card file
  or `cardsecret::`, and the key-store files never touch `Storage.`.
  Mutations, run while the migration still existed, each failed the test meant
  to catch it: precedence (finding 2), removal after a failed write (4a), an
  unusable or over-long file (4b), `toJson` without the guard (5), settings
  blanked unconditionally. Those tests went with the migration.
- `tools/key_store_check.sh`, desktop Picco simulator, fresh card per case,
  13/13: both repos ignore `host-keys.txt`; a card carrying both old files
  and a settings token reads nothing from them (the row says "Not set", no old
  value in the store or the log); a 41-character value typed through Settings
  is stored exactly, the file is 0600, nothing on the card or in the log; then
  a 255-character value over it, through the popup, is stored whole.
- `crosspoint-simulator/tests/text_entry_enter_test.cpp`: `SingleSecret`
  routes Return like `Single`, and only it asks for a literal keyboard.

### Screens looked at (fake values only)

- Desktop Picco (`key_store_check.sh` captures): the empty Secret field; the
  field after a 41-character TYPE ("******** 41 characters"); after a
  255-character TYPE ("******** 255 characters"); the row after each ("Ends in
  QRST", "Ends in LONG"). One line every time; the field's height and the
  keyboard's position are the same for empty, 41 and 255. (Only the tips line
  "Hold DEL to clear all text" appears once there is text, as before.)
- iPhone Air simulator, Release, real UIKit input: the GitHub Token field with
  the system keyboard up (lowercase keys, no suggestion bar), a paste of
  `"   " + a 40-character fake + "\n"` through the Paste chip and iOS's Allow
  Paste prompt ("******** 40 characters", one line), and the Settings row
  after the commit ("Ends in MnOp", one line). The app's NSUserDefaults held
  exactly the trimmed fake (40 characters). No fake anywhere under the app's
  Documents (the card and `diagnostics/firmware.log`).
- Outputs: the row value is "Not set", "Saved" or "Ends in" plus 4 characters.
  The Enter/Clear popup carries no key. The rejected-key popup is a sentence
  about the rule and carries no key (it may wrap, which the ruling allows).
  The web Keys section's state cell is `nowrap`, and its password input is one
  line by nature.

### CHECKED AND FOUND CLEAN (the review's list, kept so the next pass need not re-read)

- **Logs**: `KeyStore.h` and (pass 2's) `KeyStoreCore.h` `LOG_*` print a key
  name, a length or a path only; the review's probe output had no fake.
  ClaudeChat logs the key's length only. HttpDownloader logs URLs only. The
  wolfSSL trace is compiled out unless `FREEINK_WOLFSSL_DEBUG`. The simulator
  web server logs method and URI only, and the key travels in the body. The
  paste and dropped-injection `SDL_Log` lines print byte counts only. No
  `SDL_EVENT_TEXT_INPUT` text is logged. The fake-key run's log had zero
  `FAKE` hits.
- **Web API**: `/api/settings` iterates setting rows. The key rows are
  `ACTION` rows with no key and no `STRING` type (measured again in pass 3: it
  names neither key). `/api/keys` GET and POST return only name, set and last4,
  with fixed 400 strings and no ArduinoJson error echo.
- **Card**: pass 2's `toJson` did not write the token (pass 3: the field no
  longer exists). `PersistableStore`'s `.tmp` is removed or promoted, with no
  backup copy. ClaudeChat transcript errors carry the API status and body,
  never an `x-api-key` echo. iOS NSUserDefaults lives in `Library/Preferences`,
  outside the `Documents` card root. The desktop `.app` key file is in
  Application Support beside `fs_`.
- **Screen**: the Secret mask runs first in both keyboards. Cursor mode, the
  only path that draws the raw character under the cursor, cannot be entered
  for Secret (`KeyboardEntryActivity.cpp` `loop`, the Up-hold guard). A
  text-field tap moved the cursor with no reveal (pass 3: in Secret it no
  longer moves it at all). Daisy's last-character reveal sits after the Secret
  return, and no other text reaches its render. The iOS accessibility and
  a11y dumps are fed only by `EpubReaderActivity`. The row shows "Not set",
  "Saved" or "Ends in" with 4 characters, and the 4 only for keys of 12 or more.
- **NVS**: `begin(ns, true)` fails cleanly when the namespace does not exist.
  `putString` returns strlen on success and 0 on failure. NUL is rejected by
  normalize, so `== clean.size()` holds. 255 is far under the NVS string limit.
- **Other**: the `KeyboardResult` reference stays valid for the callback.
  Desktop `curl -H` on a command line is pre-existing. `setInsecure` is
  unchanged and nothing new makes it worse.
- **Not examined by the review**: WebDAV and `/download` dot-path handling
  (moot once no key is under the card root); `getSettingsList` membership
  beyond the comment; deleted-file remnants on the FAT card.

Checked in pass 3 itself and found clean: the simulator's `WebServer` shim
keeps every request header, so `header("Content-Type")` works there without
`collectHeaders`. `SDL_StartTextInputWithProperties` re-applies the traits on
every call, and a plain `SDL_StartTextInput` (props 0) restores SDL's defaults,
so the next ordinary field gets autocorrect back (`SDL_video.c:5801-5822`).
Nothing else used `cardsecret::` (grep), so deleting `CardSecret.h` removes no
capability. clang-format 21 changed only the touched hunks of each file.

### Deliberately left unfixed

- The iOS clipboard keeps the key after a paste (above).
- The BLE hex dump in development builds (above; open question 3).
- `GET /api/keys` cross-origin readable (set and last 4; open question 2).
- iOS keyboard learning without `secureTextEntry`: unconfirmed (finding 3).
- TLS peer verification is still off for both credentials' requests (pass 1;
  unchanged and not in this ask).

### Open questions for the owner

1. **Desktop key file location.** It stays beside the desktop `settings.json`
   (your storage ruling), now git-ignored in both repos and gated. A per-user
   location (`~/Library/Application Support/...`), never inside any repo,
   would remove the hazard instead of guarding it, but it would change that
   ruling. Keep as is, or move?
2. **Cross-origin reads.** `enableCORS(true)` puts `Access-Control-Allow-Origin:
   *` on every response, so any page on the LAN can read `GET /api/keys` (set
   and last 4, never a value), and the same is true of every other API.
   Dropping CORS, or narrowing it, changes what other origins can do with the
   device. Is any cross-origin client relied on?
3. **BLE keystroke hex dump in development builds.** Remove it, or keep it as
   a debugging aid with the risk noted?
4. **Old key files on the card.** Under the no-backwards-compatibility ruling
   they are ignored, not deleted. A card that still carries `/github-token.txt`
   or `/claude-key.txt` keeps serving them over File Transfer and WebDAV until
   they are deleted by hand. Delete them yourself, or should the firmware
   delete (not read) them at boot?

### Verification at the end of pass 3

Run once, after the last code change:

- `cmake --build build/test --target KeyStoreTest && ./build/test/key_store/KeyStoreTest`:
  8/8 pass.
- `pio run -e simulator_picco && tools/key_store_check.sh`: build OK, 13/13.
- `pio run -e simulator_x3`: OK.
- `pio run -e picco`: OK. RAM 22.0% (72,136 B), Flash 30.9% (4,676,254 B).
  Against pass 2: -104 B RAM, -1,108 B Flash (the migration and the settings
  field went). Against the `picco-fix` baseline: -56 B RAM, +8,320 B Flash.
  `default` was not rebuilt after pass 3.
- Simulator `CROSSPOINT_FIRMWARE_DIR=<fw> tests/run_all.sh -k host_settings`:
  `host_settings_host` and `host_settings_desktop` pass (2 passed, 0 skipped).
  `tests/text_entry_enter_test.cpp` built and run on its own: pass (six states).
- iOS: `cmake --build build/ios-sim-picco --config Release --target CrossPointX3`:
  BUILD SUCCEEDED, 0 errors. The iPhone Air runs above used this build's app,
  before the WebDAV preflight change. That change is server-only and was
  measured on the desktop simulator.
- No new translation unit (`CardSecret.h` was a header), so the simulator's
  iOS source list is unchanged.
- Nothing committed. Both trees keep their uncommitted changes.
