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

## Device report, same day: "Connection Failed / Error: Connection timeout [8 ASSOC_LEAVE]"

The owner flashed `a050ca1e` from the card and reported the picker stuck on
that screen. Traced from the code; there is no device log to read, which is
the first thing fixed below.

**Where the screen comes from.** `STR_CONNECTION_FAILED` is drawn only by
`WifiSelectionActivity`, and its `CONNECTION_FAILED` state is reached only from
a join the reader started from the list (`autoConnecting == false`). A failed
automatic join drops back to the list without a prompt. So the saved-network
auto-join failed first, then the hand-picked join ran out its 15 s.

**The reason on the screen was our own.** IDF reason 8, `ASSOC_LEAVE`, is
what the station sends when it leaves. Two things wrote it over the real
cause:

- **The picker itself.** Its timeout and auth-failure branches called
  `WiFi.disconnect()` before `appendDisconnectReason()`, and the event task
  outranks the loop, so the leave event arrives first.
- **The core's own retry.** arduino-esp32 `STA.cpp` `_onStaArduinoEvent`:
  `DoReconnect` disconnects before it reconnects, so every retry cycle ends on
  a reason 8.

**What a 15 s timeout CAN mean** (the same source). `NO_AP_FOUND` sets
`WL_NO_SSID_AVAIL`, which fails at once as "Network not found". A WPA3
`AUTH_FAIL` after the first retry sets `WL_CONNECT_FAILED`. Three auth reasons
fail fast through the counter. That leaves:

1. **Joined but no address.** The core sets `WL_CONNECTED` only on `GOT_IP`,
   so a slow or failing DHCP lease looks exactly like this.
2. **A wrong password failing slowly.** WPA2 4-way timeouts, fewer than three
   inside 15 s.
3. **A loop of non-auth reasons.**

Which of these it was is NOT known. Checked and ruled out: the stored passwords
cannot have been decoded with a wrong key, because `ObfuscationUtils` keys on
`esp_efuse_mac_get_default`, which does not depend on the radio being up.

**Changed:**

- **The reason stays real.** `WifiDiagnostics` no longer records reason 8 as
  the last reason (it still logs it), and it now tracks association
  (`isAssociated()`). The picker builds its error text BEFORE disconnecting
  and says "joined, no IP" when the radio had associated.
- **More time for a slow lease.** If the radio is associated when the 15 s
  timeout lands, the join gets 15 s more (`DHCP_GRACE_MS` in the picker,
  `kDhcpGraceMs` in `SavedWifiPlan.h`, `Link::Associated`, tested). That is a
  fix for cause 1 only, if cause 1 is what it was.
- **The trail goes on the card.** Every failed join writes `/wifi.log`: a
  header naming the screen and network, the last reason, whether it was
  associated, then the log ring (its last 16 lines; the `EVT` lines are INF, kept in
  `gh_release`). It is overwritten each time, the same pattern as
  `/fontsync.log`. **This is the instrument.** If the join still fails, the
  file names the cause.

Device behavior: UNCONFIRMED.

**Second adversarial review (same day), and what was done:**

- **The log's "associated" field was read after the disconnect** that clears
  it, so it said "not associated" on exactly the DHCP case under test.
  **Fixed:** the state is captured before any disconnect (the picker's
  `joinedAtFailure`, `SavedWifiJoin::joinedAtGiveUp()`) and passed to
  `writeFailureLog`.
- **The 160-byte header truncated** with a long SSID or a dev version string.
  **Fixed:** 320.
- **`associated` survived `resetCounters()`.** **Fixed:** it is cleared there.
- **Manual CONNECTING had no Back.** That was pre-existing, but the grace
  stretched the inescapable wait to 30 s. **Fixed:** Back returns to the list.
- **Not changed:** a successful join after a failed one leaves the failed
  join's `/wifi.log` on the card. Read its header, which names the screen and
  network.

**Checked CLEAN by the review:**

- **Locks:** no double `RenderLock`. `checkConnectionStatus` and `preflight`
  run only from an unlocked `loop()`.
- **Sizes and timing:** the 64-byte suffix fits, the picker always gives up by
  30 s, and `decide()` ends by 45 s.
- **Reason 8:** nothing branches on it.
- **Builds:** the simulator build links the stubs.

## Third pass (same day)

- **The counters were reset before the radio cycle.** In both the picker and
  `SavedWifiJoin`, `resetCounters()` ran before `disconnect(true, true)` and
  `delay(100)`. Auth-failure and association events still queued from the
  previous attempt arrived during the cycle and were counted against the next
  network. **Fixed:** the reset now runs after the cycle, just before
  `begin()`.
- **The picker's auto-join tried the last network only once.** `onEnter()`
  tries it directly, before any scan, and marks it attempted. If that one try
  failed, the scan-ordered pass skipped it even when the scan showed it in
  range. With one saved network, that meant the list, a hand-picked join, and
  the "Connection Failed" screen the owner reported. **Fixed:** a failed
  pre-scan attempt clears the attempted set, so the scan pass can try it once
  more. This runs once, because after a scan `networks` is never empty.
- **`WiFi.begin()`'s return value is now logged** in both paths. It is not acted
  on, because right after the cycle it can still hold the previous attempt's
  status. But `WL_CONNECT_FAILED` there is also what a radio that never
  started returns, and that join then ends as a silent timeout. The line lands
  in the ring, and from there in `/wifi.log`.

**Checked and clean this pass:**

- **The forget prompt:** after "Connection Failed" it defaults to Cancel
  (`showForgetPrompt`), so a double Confirm cannot delete a saved password.
- **iOS offline:** a direct attempt fails at once, the scan fails, and the list
  appears with no wait.
- **The update screens' release-on-exit:** it runs after the worker is joined.
