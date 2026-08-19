# The hardware sitting

Every open item across both repos is blocked on a physical device. This is the
one page that says which device, and what to do with it — the firmware checklist
([device-verification-checklist.md](device-verification-checklist.md)) covers
the e-reader only, and the phone and iPad items live in the simulator repo's
trackers, so the work was spread across three documents and two repositories.

Written 2026-08-19, when the trackers reached **8 open, none of them workable at
a desk**.

## Three devices, three sittings

| Device | Items | Artifact to install |
|---|---|---|
| **X3 or X4 e-reader** | [T-008], [T-017], [B-006] (+ [T-012], [T-021], [T-015] confirmations) | `~/crosspoint-archive/staged/20260819T2050Z-crosspoint-84846a41.bin` |
| **iPhone** | `ST-004`, `ST-009`, `ST-010`, `S-016` | a TestFlight build newer than 101 — see below |
| **iPad** | `ST-005` | the same build; one screenshot closes it |

### 1. The e-reader

Work through [device-verification-checklist.md](device-verification-checklist.md)
in its order — it is arranged so a sitting cut short loses the recoverable
checks rather than the unrecoverable ones. Reading the version on the boot
screen alone closes **B-006**.

The two that cannot be observed any other way are the **one-button firmware
update and its rollback** (no host build has ever run it) and **light sleep**
([T-017]: input latency after idle, page turns still immediate, USB serial alive
while connected).

### 2. The iPhone

**TestFlight is at build-101, which pins firmware `f80b140b6`** — before tables,
before the rotated page, before the memory sweeps. So a new build is needed
before the phone items mean anything about today's code, and cutting it is the
simulator repo's job (`testflight.sh`, build number from the highest `build-*`
tag plus one).

What to look at once it is on the phone:

* `S-016` — the full-screen flash on some CRT palettes. Fixed 2026-08-19,
  unconfirmed; the owner reported it from the phone, so only the phone closes it.
* `ST-009` / `ST-010` — the CRT glow, and Page Fade (Off / 15 s / 30 s / 1 min /
  2 min / 5 min).
* `ST-004` — the page as accessibility elements: VoiceOver, Speak Screen and
  Braille against a panel that is otherwise one opaque GPU texture.

### 3. The iPad

`ST-005` needs **one screenshot**: portrait, Create Note, keyboard up, dark, to
match the 2026-08-08 evidence so the two compare directly. Then check the three
overlap areas against it, fix whatever survived the 2026-08-17 pad fix and the
tablet keyboard lift, and close with the screenshot as the evidence.

**Do not substitute mockups.** That was considered and declined on 2026-08-19:
a drawing of the intended geometry is not a capture of the real one, and this
project has a standing rule against prose where pixels were asked for.

## Why none of it can be done at a desk

Recorded so the next session does not re-derive it: rotation and margins on real
e-ink, felt input latency, power draw, an erase-write-reboot, a rollback that
must actually revert, VoiceOver, and a screen flash reported from a phone are
all physical. The simulator models a framebuffer and a constant heap; it does
not model a panel, a battery, a radio, or a hand.
