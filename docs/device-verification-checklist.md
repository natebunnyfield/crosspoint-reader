# Device verification checklist

Nine of the twelve open tracker items are not waiting on code. They are waiting
on someone holding an X3 or an X4. This file is the ordered list, so that one
sitting closes them instead of four tracker entries being rediscovered one at a
time.

Written 2026-08-18 against `main` at `2c17295fc`. Owner ruling 2026-08-17
([T-008](../TODO.md)): stage a bin and write one checklist; cards wait for a
mount.

**Order matters below.** It runs cannot-be-tested-any-other-way first, because
if the sitting gets cut short those are the ones that cannot be recovered from a
host. Everything under "Quick confirms" already has headless or rendered proof
and is being re-checked on real e-ink, not discovered.

## The staged binary

| | |
|---|---|
| File | `~/crosspoint-archive/staged/20260818T2146Z-crosspoint-b39eea60.bin` |
| Commit | `b39eea60c` |
| Version string | `1.5.0-B2` (renamed from `1.5.0-BNY` in `da6736f9f`) |
| Size | 4,955,360 bytes |
| sha256 | `b1baea7b916aae81e36c2747207813370f015fb549e9082199be6986139f0329` |
| Flash | 75.4% (4,942,381 of 6,553,600) · RAM 16.5% |

**It was built in a worktree, which is the exact trap [B-029](../BUGS.md)
describes — and as of `b39eea60c` that trap is closed: a release env now REFUSES
to build without the faces rather than shrinking.** The commercial editor faces (`nitti*`, `pragmatapro*`) are
gitignored, and a build without them SUCCEEDS SILENTLY, 430,674 bytes smaller.
The staged bin was checked after linking — `nm firmware.elf | grep -c
"nittitypewriter\|pragmatapro"` reports 100 — so it carries them. Any future
staged bin needs the same check; a green build is not the evidence.

## 0. Before anything else

- Flash the staged bin (SD Firmware Update from the card, or `-t upload`).
- Read the version on the boot screen. It must say **`1.5.0-B2`**, with no
  trailing `+`. That alone closes **[B-006]** — the X4 has been running a build
  stamped `1.5.0-BNY-rc+` with an empty suffix since 2026-08-02.

## 1. One-button firmware update, and the rollback

**Closes: the newest half of [T-008].** `ee6fad7e5`, Home → **Update Firmware**.

This is the item with the least host evidence of anything on the list.
`CROSSPOINT_NO_DEVICE_FLASH` (`HomeActivity.cpp:34-39`) compiles the Home row
out — but **only for iOS**, which is where
`cmake/CrossPointIOSExclusions.cmake` defines it. Corrected 2026-08-19 after
seeing the row in a desktop simulator screenshot: an earlier version of this
file said no simulator build had ever drawn it, which was wrong. The desktop
simulator draws and can enter the screen; **no TestFlight build carries the
row**, and nothing off-device can perform the erase-write-reboot.

Watch, in order:

1. The row appears on Home at all.
2. The release check reaches GitHub and reports a version — or says the repo has
   no releases, which is a real and correct answer (`f2943d736` fixed that
   message; it used to claim "Could not reach GitHub").
3. It asks once before installing.
4. It installs, reboots, and comes up on the new version.
5. **The rollback.** This is the part that did not work before `ee6fad7e5` —
   `network/OtaCommit.cpp` confirming a healthy boot was simply missing, so a
   bad image would have stuck. Testing it deliberately means installing an image
   that does not reach the end of `setup()`; if you are not doing that, at least
   confirm a normal update does NOT get reverted on the second boot, which is
   the same latch seen from the other side.

Full mechanism: [one-button-firmware-update.md](one-button-firmware-update.md).

## 2. The SD-card update path

**Closes: the WebDAV/SD half of [T-008].** Settings → SD Card Firmware Update.

Untouched by the one-button work and still the way to install a `.bin` you
already have. Its erase-write-reboot step is the other thing that cannot be
exercised off-device at all. Confirm the picker lists the bins on the card, that
it flashes the one you choose, and that it comes back up.

## 3. Light sleep

**Closes: [T-017].** `ade9dac91`, upstream #2525. Host evidence is builds and a
headless idle-and-page-turn run; power and felt timing cannot be observed off a
device at all.

- Leave it idle past both thresholds (downclock at 500 ms, sleep at 1 s), then
  press a button. **Input latency after idle** is the thing to feel.
- Turn pages normally. They should still feel immediate.
- Plug in USB and confirm serial still works. Light sleep is deliberately
  suppressed on USB because it kills the CDC link — if serial dies, that
  suppression is not firing.

Upstream measured idle 9.68 mA → 2.78 mA on an X3. Nothing here reproduces that
number; the question is only whether it costs responsiveness.

## 4. A progressive-JPEG cover

**Closes: the device half of [T-015].** The fix is in a JPEGDEC patch, and the
iOS target substitutes stb_image entirely, so **no TestFlight build exercises
it** — this needs the hardware.

Test material is known: `illustration-108.jpg` in the Standard Ebooks Beatrix
Potter, the one non-interleaved file among ~1,900 JPEGs on this machine. Put
that book on the card and look at it twice:

- its cover on **Home**, and
- its cover as the **sleep screen**.

The failure it fixes is silent on small images — no error, just scrambled
pixels — so "no error appeared" is not the pass condition. The picture has to
look right.

## 5. Quick confirms

Each of these already has headless or rendered proof. Re-check on real e-ink,
where contrast and refresh can still make something wrong that was right on a
desktop panel.

| What | From |
|---|---|
| Seven Lucide icons on Home | [T-008] |
| The caret advances a full space in Create Note | [T-008] |
| The clock preview on the UTC-offset screen | [T-008] |
| Page-down on the last Home page lands on the last row (Settings), and does not wrap | `2c17295fc` |
| Editor font size on the side rocker, on the Editor Font screen | `125612530` |
| Power off while typing | build-33+, still unconfirmed |
| Back navigation | [B-018] |
| The notes half of [B-017] | still unconfirmed |

## 5a. One number to capture while you are there

Closes the last open item from the matcha audit
([matcha-heap-audit.md](matcha-heap-audit.md)), and costs one serial capture.

`writeDocToFile` logs contiguous heap either side of every settings/state save.
Those lines are `LOG_DBG`, so they need a **`default` build** — `gh_release` is
`LOG_LEVEL=1` and does not carry them. Flash `-e default`, run
`python3 scripts/debugging_monitor.py`, open a book so the SD fonts are
resident, then change any setting and read:

```
[PERSIST] save /.crosspoint/settings.json: before serialize maxAlloc=? free=?
[PERSIST] save /.crosspoint/settings.json: json=843B maxAlloc=? free=?
```

The JSON is 843 B, measured on the host. The question is only what `maxAlloc`
is at that moment. Comfortably above a few KB closes the item as unnecessary;
anything near it makes matcha's free-the-font-cache-before-saving fix real work.

## 6. The cards — only if one is mounted

Not part of the staged-bin step, by the same ruling. When a card is in hand:

- `cpcards` builds `gh_release` from a clean tree and deploys to every mounted
  card. Both cards are still on `bb614f73`, roughly forty commits back.
- **Verify by hash, never by filename.** A card once carried a
  `Rosarivo_16.cpfont` with the right name, the right byte length, and one 16 KB
  cluster overwritten with unrelated ASCII.
- **OWEN_BNF (X4) is the stale one** — last hash-verified 2026-08-06, before
  three font rulings changed the installed set. BUNNYFIELDS was reprovisioned
  and hash-verified 2026-08-15.
- Delete superseded bins rather than leaving them. Every one left behind is a
  chance to flash the wrong firmware.
- If you copied fonts from macOS: `find /Volumes/<CARD>/.fonts -name '._*'
  -delete && sync`.

## Not on this list

**[B-003]**, the ten exploded `.epub` directories, is a content decision — re-zip
them or discard them. Opening one on the device proves nothing: `FileBrowserActivity`
branches on the trailing `/` and navigates into them, so they can never open, and
that branch is unambiguous.

## The phone is a different sitting

These need an iPhone with TestFlight, not the reader. Recorded here only so the
two are not conflated.

| Item | Where it is |
|---|---|
| [ST-008] moire in the selection dots | build-80 and later — testable in **build-82** |
| [ST-004] the page as UIAccessibility elements | build-41 and later — testable in **build-82** |
| [ST-010] text fading away after a page turn | **not in any build yet** |
| The page-colour button and the palette rework | **not in any build yet** — simulator `main` is ahead of the `build-82` tag |

Build-82 is simulator `2e9e4c3` + firmware `c36dba242`. The firmware half of it
is still current; the simulator half is not.
