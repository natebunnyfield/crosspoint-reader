# Open work

Things to do that are not defects. Defects live in [BUGS.md](BUGS.md); this file
is for work that was asked for and has not landed.

It exists for the same reason `BUGS.md` does: todos were being carried in chat,
where they survive only as long as the session does.

Format: `### [T-NNN] Title` then what it is, why, and what "done" looks like.
An item leaves this file when it ships or when it is ruled out — not when it is
started.

---

## OPEN

### [T-008] Everything since 2026-08-06 is staged but unproven on hardware
**scope: verification · opened 2026-08-07**

Build, package and TestFlight are DONE. The device half is not, and the two keep
getting reported together, so this entry exists to keep them apart.

**Done 2026-08-07, from a clean tree at `91b4a8fc`:**

| Artifact | State |
|---|---|
| `gh_release` firmware | built, staged at `~/crosspoint-archive/staged/20260808T0201Z-crosspoint-91b4a8fc.bin`, sha256 `96f77816…` |
| Mac app (`CrossPointX3.app`) | packaged and `verify`-clean (all 3 purpose strings); NOT TestFlighted, per owner ruling |
| iOS TestFlight | **build-38 uploaded**, 19,545,915 bytes, delivery `d3e7a3ba` |

**Not done — needs the hardware in hand:**

- **The SD cards.** None were mounted, so `cpcards` never ran. They are still on
  `bb614f73`, roughly forty commits back. This is the only step blocked on a
  physical act.
- **Confirm on device**, since it will be in hand anyway: the seven Lucide icons
  on Home, the caret advancing a full space in Create Note, the clock preview on
  the offset screen, and the WebDAV update flow — that last one matters most,
  because its erase-write-reboot step is the ONLY part that cannot be exercised
  off-device at all.
- **Older debt, still unconfirmed:** power-off-while-typing (build-33+), iOS file
  transfer (build-37), B-018 back-nav, B-017's notes half.

**What this loop taught, worth keeping:** the iOS archive found a link error that
neither the device nor the desktop build could see — `SdFirmwareUpdateActivity`
is excluded from the iOS source set, and a new call into it from a shared TU only
fails when that target links. The iOS archive belongs IN the loop, not after it.
`xcodebuild -target CrossPointX3 -sdk iphoneos build CODE_SIGNING_ALLOWED=NO` is
the cheap version and needs no keychain.

### [T-007] Upstream issue #2863 reproduces here and is tracked nowhere
**scope: fork sync · raised 2026-08-06 · confirmed 2026-08-07**

The short-press power behaviour upstream reports is present in this fork's code:
`lib/hal/HalGPIO.cpp:207` carries a `TODO` describing the same thing. Upstream
issues are not visible from this repo's tracker, so it would otherwise be
rediscovered rather than remembered.

**Close by:** decide whether the fork wants upstream's fix or its own behaviour,
then either take the patch or write the divergence down in `docs/fork-sync.md`.

---

## Finished

### [T-010] settings.json / state.json writes are not atomic — FIXED
**scope: data durability · found by the 2026-08-08 P0 audit · fixed 2026-08-08**

Every settings and state save went through `SDCardManager::writeFile`, which
removes the target and then opens it `O_TRUNC` — so power lost in that window
left NO file, and the device booted having forgotten its Wi-Fi, reading position
and owner name.

Fixed at the fork's `PersistableStore`, not in the submodule. FAT cannot replace
a file in one step (SdFat's rename fails if the destination exists), so the order
is: write the temp in full, remove the target, rename the temp over it. The
window shrinks to the remove→rename gap, and — the part that matters — a
COMPLETE copy of the data is on the card throughout it.

`readDocFromFile` is what makes that recoverable rather than merely narrower: a
temp beside a missing target is promoted, a temp beside an intact target is
stale and deleted. A promoted temp is parsed first, because a crash during the
temp write itself leaves a truncated one and promoting that would turn a
recoverable state into a corrupt file.

**Verified against the simulator, all three paths:** a normal save leaves no
temp; a complete temp with the target deleted is recovered with the data intact
(`deviceOwner = RECOVERED-FROM-TEMP`, which under the old code would simply have
been gone); a truncated temp is discarded and not promoted. 235 host tests.

### [T-009] A 0 ms redraw delay for iOS — DONE (option), measurement still owed
**scope: iOS display · asked 2026-08-08 · shipped 2026-08-08**

`Typing Redraw Delay` now offers **0 ms**, and it is honoured off-device only.

Two things this had to get right, both recorded here because they are the traps:

- **0 is appended, not inserted.** The INDEX is what `settings.json` persists,
  so putting 0 at the front — where it belongs numerically — would silently
  re-map every card in the field, turning a saved 250 ms into 100 ms. The picker
  reading `… 500 ms, 1000 ms, 0 ms` is the price of not corrupting settings.
- **The device clamps it.** On e-ink a refresh is ~570 ms with a real waveform,
  ghosting and battery cost, so 0 would mean a full refresh per keystroke. A
  card carrying 0 (written on a phone, then moved to hardware) is clamped to the
  shortest real step rather than obeyed. Verified per platform: index 6 resolves
  to 0 ms in the simulator and 25 ms on device, the 250 ms default is unchanged
  on both, and an out-of-range index still falls back to the default.

**Still owed, and deliberately not claimed:** the measurement half of this entry
— whether the debounce is what makes typing feel slow on the phone at all. If a
keystroke already coalesces into the next frame, 0 ms changes nothing and the
lag is elsewhere. The option is now there to A/B against, which is the cheapest
way to answer it.

### [T-004] Make the simulator stop lying about the device — TRACKED IN S-001
**scope: simulator fidelity · closed as duplicate 2026-08-08**

This entry and `S-001` in the simulator's [BUGS.md](../crosspoint-simulator/BUGS.md)
are the same work, tracked twice in two repos. S-001 carries the actual table of
six reversals and their status, so it is the one to read. Two are now fixed (the
heap, and battery/USB); four remain (async refresh, the panic path, the OTA
partition stub, the pinned OTA check).

Closed here rather than kept in parallel, because two trackers for one piece of
work is how a thing gets half-done twice.

---

## Carried over, still owner decisions

These were raised in the audit and have not been ruled on. They are not defects,
so they are not in `BUGS.md`.

### [T-005] Cruft with zero references — RULED and actioned
**scope: repo hygiene · raised 2026-08-06 · ruled 2026-08-08**

Ruling: delete the QA captures and the spike scripts, keep the rest.

**Deleted:**

| Thing | Note |
|---|---|
| `qa_x4/`, `qa_x4b/`, `qa_sleep/`, `qa_west/`, `qa_west2/` | 492 KB of PNGs from one debugging session on 2026-08-03. Byproducts of a run |
| `spike-build.sh`, `spike-capture.py`, `spike-drive.py`, `spike-run-exchange.py` | BLE spike scaffolding |

**Kept:**

| Thing | Why |
|---|---|
| `src/util/StringUtils`, `UrlUtils`, `HtmlToPlainText` | library-shaped and small; `HtmlToPlainText` is what an OPDS description or a Claude response would want |
| `docs/flared-semiserif.html` | a typography specimen, not a byproduct — 220 KB because the faces are embedded |

**The one thing worth saying about the deletion:** only `spike-build.sh` was
actually superseded — `7fee9a8c` removed the `lib_ignore = BLE` entry that made
its `-I` injection necessary, and `docs/ble-editor-spike.md` had already
recommended deleting it. The other three were **working device automation**
(unattended capture, driving `CMD:BLEEDIT`, running a whole Claude exchange from
one port owner), removed as spike scaffolding rather than as dead code. What
each did is recorded in that doc's Reproducing section, and they are one
`git revert` away if unattended capture is wanted again.

### [T-006] `freeink-sdk` bumped, and made repeatable — DONE (host half)
**scope: dependency · ruled and actioned 2026-08-08**

Pin moved `e514a868` → `24fbab75`: **71 commits, 79 files, +17.3k lines**, a
clean fast-forward with nothing local to preserve. (It was 54 when filed two
days earlier — it drifts about a commit a day, which is the argument for
`scripts/update_sdk.sh` rather than for doing this by hand.)

Several of them are fixes this fork wants: *"Fix grayscale waveform LUTs for
CrossPoint rendering"*, *"fix(x3): one forced full sync after begin(), not
two"*, *"preserve pixels outside partial-width image blits"*, *"correct
voltage-to-percentage conversion for battery"*. There is also new hardware
support (M5Stack Paper Mono, UC8279 for X4 800x480) which is purely additive
here.

**Verified off-device:** `gh_release` and `simulator_x3` build, 235 host tests,
9 simulator tests, `test_read_aloud_capture` including the rect-geometry
assertion — and Home and a book page render **pixel-identical** before and
after (0 of 418,176 differ).

**NOT verified, and this is most of what the bump carries:** waveform LUTs, the
GPIO/PWM ordering change, holding the display RESET pin high in deep sleep, and
the battery curve. None of those execute on a host at all. Flash it, read a few
pages watching for ghosting or a changed refresh, sleep and wake, and look at
the battery reading.

### [T-006a] Submodule bumps are now one command
**scope: process · added 2026-08-08**

[scripts/update_sdk.sh](scripts/update_sdk.sh) — `--dry-run` to see the gap and
what it touches, no argument to fast-forward and prove it, or a ref to pin
somewhere specific.

It gates on: submodule initialised (a fresh worktree does not inherit it),
submodule clean, **fast-forward only** (a pin ahead of the target means someone
committed to the SDK locally — that is a merge with a decision in it, and the
script refuses), device build, desktop build, host tests, and a pixel diff of
Home and a book page against a pre-bump reference. Any failure puts the pin back
where it was.

What it deliberately does NOT do is claim success. It ends by naming what a host
gate cannot execute and listing what to look at on hardware, because the
tempting failure here is a green run being read as "verified". Both guards were
tested: the non-fast-forward refusal fires, and the render comparison is the
gate that would catch a layout regression.


Kept for the reasoning, not the status. Each records something that would
otherwise have to be rediscovered.

### [T-001] Switch all icons to the Lucide set — DONE
**scope: UI · asked and ruled 2026-08-07 · SHIPPED `see below`**

All 14 icons are Lucide now, at both 24px and 32px, generated by
`scripts/gen_lucide_icons.py` from the SVGs already vendored in the SDK.

| UIIcon | Now |
|---|---|
| Transfer | `arrow-right-left` |
| Folder | `folder` |
| Book | `book` |
| Recent | `clock` |
| Settings | `settings-2` |
| ManageFiles | `folder-cog` |
| CreateNote | `square-pen` |
| Library | `library-big` |
| Wifi | `wifi` |
| Hotspot | `radio` |
| ClaudeMark | authored `claude-mark.svg` |
| Text | `file-text` |
| Image | `image` |
| File | `file` |

**The rotation trap, resolved.** `GfxRenderer::drawIcon`
(`lib/GfxRenderer/GfxRenderer.cpp:1450`) plots stored `(row, col)` to screen
`(x + size-1-row, y + col)` — a 90° turn baked into the draw, which its own
comment says reproduces what the old byte-aligned blit did. So assets must be
stored **pre-rotated 90° CCW** for the two to cancel, and the generator does
that before packing.

Note this is the OPPOSITE of the SDK's own `libs/assets/Icons/tools/gen_icons.py`,
whose header states its `freeink::Icon` bits are deliberately not pre-rotated
because that renderer maps logical coordinates itself. Both are right for their
own draw path; using either generator with the other's renderer ships sideways
icons. That is why this firmware got its own script rather than adopting the
SDK's — recorded so nobody "simplifies" it later.

**Also done:** 16 superseded headers deleted (all confirmed zero-reference
first), including the five size-variant duplicates. `src/components/icons/` is
now `lucide_icons.h`, `claude-mark.svg`, plus `cover.h` and `BackspaceIcon.h`
which are different assets still in use. ManageFiles no longer borrows the plain
file glyph at 24px — every icon exists at both sizes now.

**Verified:** decoded three generated bitmaps back through drawIcon's exact
transform before trusting them; Home and Manage Files screenshotted after;
device `gh_release` and desktop both build; 215/215 host tests.

**Not confirmed on hardware** — rendering was checked on the desktop panel, not
on e-ink.

### [T-002] `src/notes/` versus `SCOPE.md` — DONE
**scope: docs · ruled 2026-08-07 · keep the capability, amend the docs**

Ruling: notes and Claude chat stay in device builds. Nothing about the binary
changed; the documentation was what was wrong.

`SCOPE.md` and `ROADMAP.md` are **upstream's** contribution policy, and they now
say so, with a fork banner naming the two divergences and the reason. Upstream's
own words are what make it coherent — it says these features "belong in other
forks" and that it "generally defers to that fork." This is one of those forks.

Upstream's text is left verbatim under the banner rather than edited in place.
Rewritten bullets would conflict on every upstream change to those sections, and
until someone noticed, this repo would be publishing a distorted copy of another
project's policy under that project's filename.

The three-way contradiction is resolved by picking the narrow reading and
writing it down: no always-on radio and no feed readers, but Wi-Fi the owner
starts, uses once and stops is in scope here. Recorded in
[docs/fork-sync.md](docs/fork-sync.md) so a later sync does not "fix" it back.

### [T-003] The published SSID and the API key — DONE
**scope: security · ruled and executed 2026-08-07**

Both halves are closed.

**The key — protected, then deliberately UNprotected.** `claude-key.txt` was
added to `HIDDEN_ITEMS` in both `src/network/CrossPointWebServer.cpp` and
`src/network/WebDAVHandler.cpp`, which took the download endpoint and WebDAV GET
from **200 with the key in the body** to 403, took DELETE to 403, and removed it
from the listing.

**That was then reverted on owner ruling the same day, and the file is servable
again by design.** The ruling: the key being readable over Wi-Fi is acceptable,
and it will be rotated as needed. What decided it was measuring the cost —
`HIDDEN_ITEMS` blocks `PUT` too (403, against 201 for an ordinary file), so the
protection removed the only way to WRITE a rotated key over File Transfer.
Rotation would have meant pulling the SD card. Blocking the write while the read
is acceptable is pure cost.

Recorded rather than dropped because the exposure is real and someone will
rediscover it: the file transfer server has **no authentication of any kind**,
and AP mode is an open network (`AP_SSID = "CrossPoint-Reader"`,
`AP_PASSWORD = nullptr`, "Open network for ease of use"). Anyone in Wi-Fi range
can join and read the key while the File Transfer screen is up. That is a known,
accepted risk on this fork — not an oversight. Do not "fix" it without asking.

For reference if it is ever revisited: a leading dot would NOT protect it, since
WebDAV deliberately serves dot-paths so a card mirror can sync `/.crosspoint`
and `/.fonts`. `HIDDEN_ITEMS` is the only list that covers WebDAV. A write-only
arrangement (PUT allowed, GET blocked) was offered and declined.

**The SSID.** Gone from the code — `connectWifi` walks saved credentials like
the Wi-Fi picker's auto-connect — and scrubbed from published history on
2026-08-07. 166 commits rewritten (2026-08-03 forward), force-pushed to the
fork. Root commit, commit count and shared ancestry with `upstream/develop` all
verified unchanged afterwards.

**Two things worth remembering from doing it:**

- `git filter-repo` strips GPG signatures. Run unscoped, it rewrote the *root*
  commit — which upstream signed — and cascaded to all 1361 commits, severing
  the fork from upstream entirely. Scope any future rewrite with
  `--refs <first-bad>^..main --partial`.
- `--replace-text` rewrites file contents only. Four commit **messages** still
  carried the SSID until `--replace-message` was added with the same rules file.

Cost paid: 7 of the rewritten commits were GPG-signed and are now unsigned.
Pre-scrub history is preserved at
`~/crosspoint-archive/crosspoint-reader-prescrub-20260807-1555.bundle` and as
`refs/prescrub/main` locally.

**Still outstanding, and it is not a code change:** the API key was served over
the network by every build up to today, and the SSID was public from 2026-08-03.
A scrub limits future exposure and retracts nothing — anyone who cloned still
has both. **Rotate the Anthropic key.**

