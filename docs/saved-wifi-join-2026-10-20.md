# Network screens join a saved network themselves (2026-10-20)

Owner, 2026-10-20: *"update firmware and crosspoint-simulator to not say 'Need
wifi' but just connect to the last wifi on its own smoothly without blocking
prompts."*

Surveyed at `1dd85817e`. Everything below was checked against the source unless
it says otherwise.

## What said "No Wi-Fi connection", and why

`STR_UPDATE_NEEDS_WIFI` ("No Wi-Fi connection") and its hint ("Join a network
in Settings, then try again.") are drawn by three screens:

| Screen | Before | Death point |
|---|---|---|
| Update Library (`LibraryUpdateActivity`) | refused outright when `WiFi.status() != WL_CONNECTED` | `onEnter()`, the old gate at `:58` |
| Update Fonts (`FontUpdateActivity`) | same | `onEnter()`, the old gate at `:112` |
| Update Firmware (`OnlineFirmwareUpdateActivity`) | rejoined the LAST network only, since 2026-08-28 | the store was empty, see below |

**The firmware-update rejoin could not work after a fresh boot.** Nothing
loads `/.crosspoint/wifi.json` at boot: `main.cpp:909-911` loads `SETTINGS`,
`APP_STATE` and `RECENT_BOOKS`, and the only `WIFI_STORE.loadFromFile()` calls in
`src/` were in `WifiSelectionActivity::onEnter()` and `ClaudeChat::connectWifi()`.
So `getLastConnectedSsid()` returned "" until the Wi-Fi picker or Claude had run
once that boot, and the screen showed "No Wi-Fi connection" for a network that
was saved on the card. Verified by grep; not reproduced on a device.

It also tried ONLY the last network, so a card whose last network was out of
range said "No Wi-Fi" while another saved network was in range.

## What changed

- `src/network/SavedWifiPlan.h`: the policy. Pure, clock-free, and
  truth-tabled in `test/saved_wifi_plan/` (10 cases). Order: the last network
  that worked, then every other saved network in saved order, each once. One
  saved network gets 20 s (the old firmware-update window). With several, each
  gets 10 s. The whole search is capped at 30 s. `WL_CONNECT_FAILED` or three
  auth failures move on at once; `WL_NO_SSID_AVAIL` does NOT (see the review
  below).
- `src/network/SavedWifiJoin.h`: the WiFi glue, header-only so the iOS source
  list (generated from the compile database) needs no regeneration. `start()`
  issues the first `WiFi.begin()` and returns; `poll()` runs once per `loop()`.
  Ready means associated AND `hostByName(api.github.com)` answered (the
  two-condition rule from `OtaPreflight.h`, whose constants it reuses).
- All three screens now load the store under their `RenderLock`, start the
  join, and poll it. Library and Font Update do it under the CHECKING frame,
  whose second line reads "Connecting to saved Wi-Fi..." (the existing
  `STR_CONNECTING_SAVED_WIFI`) until the link is up. Back cancels at any time.
  A network other than the recorded last one becomes the last one once it
  works, so the next join tries it first.
- `#ifdef SIMULATOR` with no saved network: one `WiFi.begin()` with no SSID,
  which the simulator answers with the host's own association. This is the
  rule `ClaudeChat::connectWifi()` already follows. It is why the desktop
  simulator no longer stops at "No Wi-Fi" either.

"No Wi-Fi connection" still exists. It is now the screen for a card with
nothing it can join, or a search in which none of the saved networks came up.
That is the one case where there is nothing to connect to.

## Checked and found clean (no change needed)

- **The Wi-Fi picker's auto-connect** (`WifiSelectionActivity`, used by File
  Transfer and Settings > Wi-Fi). It already tries the last network and then
  the visible saved ones, and completes without a prompt on a saved
  credential (`:528`, `onComplete(true)`). Its prompts are only the
  save-password question after a typed password, and the forget question
  after a FAILED saved join.
- **Claude chat** (`ClaudeChat::connectWifi`) loads the store itself and
  tries every saved network. It blocks for up to 20 s inside one call (not
  checked here whether on the main loop or a worker), but it asks nothing.

## Measured

On the desktop `simulator_x3`, with an empty card and no token:

```
[16690] [ACT] Entering activity: LibraryUpdate
[16790] [INF] [WIFIJOIN] Joining the host's network
[16805] [INF] [WIFIJOIN] Joined the host's network in 115 ms; waiting for the resolver
[16805] [INF] [WIFIJOIN] Resolver answered for api.github.com
[16830] [INF] [LIB] no GitHub token configured
```

Before this change the same walk ended on "No Wi-Fi connection". With
`CROSSPOINT_SIM_WIFI_CONNECT=fail` the join gives up in 114 ms (the 100 ms is the radio cycle) and
shows that screen.

The navigation script, from the firmware root:
`200:QTAP:BACK:2500`, then `RIGHT` thirteen times at 900 ms spacing from
4000 ms, then `LEFT` and `CONFIRM` (selection clamps at Settings, and Update
Library is the row above it).

## Adversarial review, and what was done

A read-only refuting pass ran over the first version. Its findings, each
checked against arduino-esp32 3.3.7's `STA.cpp` before acting:

1. **Stale status blamed on the next attempt** (would-ship-a-visible-bug). The
   core writes `WL_*` only from its event handler and resets it only on
   STA_STOP/STA_START, and plain `disconnect()` triggers neither. So a refusal
   left by the previous network, or by a join a previous screen abandoned
   (Back mid-join on Update Firmware, then Update Library), was read as this
   network's answer on the first poll. **Fixed:** every attempt now cycles the
   radio first, using `disconnect(true, true)` plus `delay(100)`, which is the
   picker's own sequence.
2. **One `NO_AP_FOUND` ended a sole network's attempt.** The core keeps
   rescanning after it, and this path has not scanned first (the picker has),
   so one missed scan was enough to show "No Wi-Fi". **Fixed:**
   `WL_NO_SSID_AVAIL` is Pending now and waits out the cap.
3. **iOS, phone offline:** the screens waited 20-30 s before saying "No Wi-Fi",
   with nothing the app could do to end the wait. **Fixed in the simulator:**
   `WiFi.begin()` on an offline host now reports `WL_CONNECT_FAILED`. The
   refusal clears when the host comes back or the attempt is dropped
   (crosspoint-simulator `src/WiFi.h`, `tests/wifi_host_test.cpp`).
4. **Radio left on after Library/Font Update exit.** These screens never used to
   turn the radio on. **Fixed:** `SavedWifiJoin::release()` in their
   `onExit()` turns it off, but only if the join is what turned it on. Update
   Firmware is unchanged: it has no `onExit()` and always left the radio up.
5. **`state`/`errorMessage` written without the lock** during the join, when
   `afterCheck()` takes it for the same writes. **Fixed:** the join's terminal
   cases now take `RenderLock`.
6. The doc's log line was from an earlier build (cosmetic). **Fixed** above.

**Checked and found CLEAN by the review:**

- **Locks:** `requestUpdateAndWait()` in loop() runs after the lock scope
  closes, with no second waiter.
- **No overlap with the check:** it cannot start while joining. Back mid-join
  leaves nothing to flush and no worker.
- **Sleep and repaint:** keep-awake and preventAutoSleep cover CHECKING, and the
  heartbeat repaint is throttled to 1 s.
- **Update Firmware:** no reference to removed members remains. Back is still
  read in CONNECTING. The results map to the right states.
- **The plan's timing:** `decide()` has no Wait-forever case and no index
  overrun, and survives `millis()` wraparound (a probe started at 0xFFFFF000).
- **The join order:** `joinOrder()` dedupes, and drops empty names and stale
  last-SSIDs.
- **Strings and diagnostics:** `STR_CONNECTING_SAVED_WIFI` exists in both
  translations, and `WifiDiagnostics` resets on every attempt.

## Limits

- **Device feel: UNCONFIRMED.** No X3 on hand. What to watch for: open Update
  Library with Wi-Fi off after a cold boot. The frame should read "Connecting to
  saved Wi-Fi..." for a few seconds, then go to "Contacting GitHub" with no
  button pressed.
- The multi-network fallback has not been exercised against real radios. The
  simulator joins whatever it is handed, so only the policy test covers the
  order and the caps.
