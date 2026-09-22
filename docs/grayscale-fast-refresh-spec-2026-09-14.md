# TrustyReader's "fast grayscale LUT": clean-room mechanism spec

**Written 2026-09-14.** Clean-room description of the fast grayscale
waveform bank reported in TrustyReader, produced so that an implementer who has
**not** read the GPL-2.0 source can build the equivalent from the controller
datasheet.

**Provenance labels used throughout.** Every claim carries one:

| Label | Means |
|---|---|
| **MEASURED** | A number published by the project, or one I computed from data I read |
| **STATED** | Their README, docs, or a source comment says so |
| **OBSERVED** | I read the source and this is the mechanism it implements |
| **INFERRED** | My reasoning; may be wrong |

**Companion documents.** The ecosystem-level survey of who else runs on this
hardware — including papyrix, pulp-os, SnailOS and the windowed-refresh gap — was
written the same day and is not repeated here:
[ecosystem-survey-2026-09-14.md](ecosystem-survey-2026-09-14.md) (§D-2, §D-3 are
the neighbours of this file). For what our grayscale overlay costs and which
screens earn it, see [two-bit-chrome.md](two-bit-chrome.md); for the measured
refresh timings, [notes-and-claude.md](notes-and-claude.md) §"what a redraw
costs".

**One geometry correction up front.** The X3 is **792 × 528** (W × H) on UC8253
(`freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:812-818`), not
528 × 792; the X4 is 800 × 480 on SSD1677 (`:775-780`). Same panels either way —
only the axis convention differs — but searches below use the tree's order.

---

## 0. The short version

Three findings, in order of how much they matter.

1. **The fast bank is already ours, under MIT, byte-for-byte.** Our tree carries
   `freeink::lut_factory_quality`
   (`freeink-sdk/libs/display/FreeInkDisplay/src/lut/Ssd1677Luts.h:92`). I
   extracted all 110 significant bytes from it and from both GPL copies and
   compared them programmatically: **identical**. There is no clean-room problem
   to solve for the fast bank, because we already have a legitimately-licensed
   copy of the same waveform. (MEASURED — byte comparison, this session.)
2. **The "~60% faster" claim points the other way from how it reads.** The fast
   bank is the *inherited* one; what TrustyReader *added* was a **slower,
   higher-quality** bank for comic pages. Their comment compares the old fast
   bank against their new slow one. We already have the fast side and do not
   have the slow side. (OBSERVED + MEASURED.)
3. **Our gap is plumbing, not waveforms.** `lut_factory_quality` is compiled in
   and reachable from the SDK, but **no CrossPoint call site can select it** —
   our HAL entry point has no parameter for it
   (`lib/hal/HalDisplay.h:124`, `lib/hal/HalDisplay.cpp:193-196`). (OBSERVED.)

**It is X4 only.** Nothing in this lineage touches the X3's UC8253. (OBSERVED.)

---

## 1. What was examined

All accessed **2026-09-14**. Clones taken fresh from GitHub on that date.

| Project | URL | Commit read | License (file I read) |
|---|---|---|---|
| TrustyReader | <https://github.com/HookedBehemoth/TrustyReader> | `3f26e2a34a0945e61a87c2efe3a91cd75621328b` — subject *"update grayscale lut"* | **GPL-2.0**, `LICENSE.txt`, 17,984 bytes, GNU GPL Version 2 June 1991 |
| microreader | <https://github.com/CidVonHighwind/microreader> | `a4adfdb9a37b83bcb724cac3dc8990f335b49e8a` | **GPL-2.0**, `LICENSE`, same text |
| TernOS | <https://github.com/azw413/TernOS> | `6f68433d9bf916a958f764379b970e00ae28e1b6` | **GPL-2.0**, `LICENSE.txt`, same text |

**Canonical repo for the claim: `HookedBehemoth/TrustyReader`.** It is not under
an org. Its README describes it as *"Custom Firmware for the Xteink X4"* and
credits `CidVonHighwind/microreader` and `sunwoods/Xteink-X4` as resources.
(STATED.)

**Lineage, as actually found** — the brief's "microreader → TrustyReader →
TernOS" is correct in substance, with one correction worth recording:

- microreader is the ancestor for the **display driver**, and TrustyReader's
  driver file header points at it by URL. (OBSERVED.)
- TernOS is a descendant of TrustyReader: its X4 driver carries the same four
  waveform banks under the same names, with the harness changed. (OBSERVED.)
- **The relevant driver work also exists under MIT.** Our own
  `freeink-sdk/NOTICE:11-18` states the SSD1677 and UC8253 initialization
  sequences and waveform LUTs are derived from the **OpenX4 E-Paper Community
  SDK** (<https://github.com/open-x4-epaper/community-sdk>, **MIT**, "Copyright
  (c) 2025 Open X4 E-Paper Contributors") and credits *"Original e-paper driver
  authorship … to CidVonHighwind"* — the same author as microreader. So the same
  person's driver work is available to us on both licenses, and the MIT branch is
  the one already vendored here. (STATED, from our own NOTICE.)

Also read, as context rather than subject: the SSD1677 datasheet (§5), the
Good Display panel page (§5), and papyrix's public SSD1677 driver notes.

### 1a. What I looked at and found *not* relevant

Recorded so the next pass does not re-open them.

- **`bench.sh` / `baseline.sh` and their firmware images are not display
  benchmarks.** Both harnesses time EPUB parsing, ZIP central-directory reads,
  XML event throughput and image decode. Neither one refreshes the panel or
  touches a waveform. **No published display timing exists in this project.**
  (OBSERVED.)
- **The `sunwoods/Xteink-X4` repo referenced by their README is a hardware
  teardown** — photos, pin mapping, schematic notes. No firmware, no LUTs, and
  **no LICENSE file**, so it is all-rights-reserved and cannot be lifted from
  regardless. (OBSERVED via fetch.)
- **TernOS adds no new waveform.** It inherits the four banks unchanged and adds
  only a capability-driven *policy* (§4f). Its repo contains no "60%" claim at
  all. (OBSERVED.)

---

## 2. The licence position

All three projects are GPL-2.0 and CrossPoint is MIT (`LICENSE:1`), so copying
any of their source — waveform byte tables included, those being creative
selections of values fixed in a source file — into this tree would relicense the
whole thing; that is why this document describes mechanism, frame counts and
register order rather than reproducing anything. The practical resolution here is
better than a clean-room rewrite anyway: **the specific waveform at issue is
already present in our tree under MIT** through the OpenX4 Community SDK
(`freeink-sdk/NOTICE:11-18`), so the fast bank needs no reimplementation, and the
only genuinely new artifact — TrustyReader's slower quality bank — is a set of
timing values that must be **measured on glass** rather than copied (§6c).

> **Note on §0's byte comparison.** I compared our MIT bytes against theirs to
> establish that we already hold the waveform. That comparison is a licence
> finding; it moved nothing into this tree and reproduces nothing here.

---

## 3. The claim, verified

> **Q: is the ~60% figure theirs or someone else's?**

**Theirs, and it is a source-comment estimate rather than a measurement.** The
phrase is a comment in TrustyReader's X4 driver, introduced by commit `dc58311`
(*"add XTH rendering"*) — `git log -S` over all branches returns only that commit
and one later copy into their ESP32-S3 target. It does **not** appear in
microreader, and does **not** appear anywhere in TernOS. (MEASURED — pickaxe
search.)

> **Q: 60% faster than what — a full refresh, their own base grayscale, or the
> vendor waveform?**

**Than their own other grayscale bank**, and specifically the *slower* of the two.
Their driver holds two absolute-grayscale banks and picks between them by
content type: the slower one for grayscale pages inside their comic container,
the faster one for standalone images such as wallpapers and covers. The comment
sits on the fast bank and compares it to the slow one. It is **not** relative to
a full refresh and **not** relative to the vendor/OTP waveform. (OBSERVED.)

**The direction is the part that surprises.** The fast bank is not an
optimization they discovered — it is the bank they inherited, which microreader
had already named for the *factory* firmware it came from. What TrustyReader
*added* was the slow bank. So the honest restatement is: **"the quality bank we
added is about 2.4× slower than the factory bank we already had."**
(OBSERVED + INFERRED.)

### 3a. The figure reproduces exactly from the LUT's own structure

The waveform format (§4b) makes total refresh duration computable:

```
frames(bank) = Σ over the 10 timing groups of  (RP + 1) × (TP_A + TP_B + TP_C + TP_D)
duration     = frames × frame_period
```

Computed from the two banks (MEASURED — arithmetic over data I read; no values
reproduced):

| | Fast bank | Quality bank |
|---|---|---|
| Active timing groups | 3 | 3 |
| Frames per group | 24, 23, 3 | 27, 28, 5 |
| **Total frames** | **50** | **60** |
| Frame-period setting | the shorter one | the longer one; their comment states the ratio is 2× |
| Relative duration | 1.00 | ≈ 2.40 |

- Frames alone: 50/60 → **16.7% fewer frames**.
- With the stated 2× frame-period ratio: 50 × 0.5 / 60 → **58.3% faster**.

**58.3% ≈ "~60%".** The claim is therefore internally consistent and derived
from the table, not from a stopwatch. Treat it as arithmetic, not as a
measurement of a real panel. (MEASURED + INFERRED.)

**Corroborated independently from our own MIT tree.** `Ssd1677Luts.h:89-91`
documents the same bank as *"50 waveform frames"* with the same frame-rate
setting — arrived at by whoever vendored it, without reference to TrustyReader.
Two agreeing derivations of 50 is as close to verification as this gets without
a scope. (STATED, ours.)

**A naming collision that will mislead somebody.** The same 110 bytes are called
the **fast** bank in TrustyReader and *"slower, cleaner"* in our own header
(`Ssd1677Luts.h:91`). Both are right, and they are measured against different
baselines: in their tree it is the faster of two *absolute* banks (50 vs 60
frames); in ours it is slower than the *differential* bank beside it (50 vs 12
frames, §4e). **Never compare a bank's speed without naming what it is being
compared to** — that ambiguity is most of what made the original "60% faster"
claim hard to pin down. (MEASURED.)

> **Caveat that limits this number.** The 2× frame-period ratio is the source
> comment's assertion. The SSD1677 datasheet Rev 1.0 allocates five bytes of the
> LUT to frame rate (§4b) but **publishes no table mapping those settings to a
> frame period in ms or Hz** — I searched the full extracted text. So the 2×
> is unverified against the controller spec, and with it the 58.3%. See §7.

> **Q: is it grayscale at all, or a bilevel fast mode described loosely?**

**Genuine grayscale — 4 levels, one pass.** The controller has two RAM planes;
this path loads one with the low bit and the other with the high bit of a 2-bit
image, and a single waveform with four independent transition tables renders four
levels in one update. Their driver carries distinct entry points for writing the
low plane, the high plane, or both, and their format documents the 2-bit state as
`(plane_B << 1) | plane_A`. This is not a bilevel mode. (OBSERVED.)

**But there are two different grayscale families in that file and they are easy
to conflate** (§4e): a *differential* family that needs a follow-up revert
waveform, and this *absolute* one-pass family that does not. **The 60% claim
belongs only to the absolute family.** (OBSERVED.)

> **Q: does it cost ghosting, contrast, or panel longevity — and do they say so?**

**They say "slightly lower quality" and nothing further.** No ghosting
measurement, no contrast measurement, no longevity discussion anywhere in the
repo. (STATED — and the absence is OBSERVED.)

What the structure itself shows (§4d): the two banks have **identical
voltage-state sections**. Every pixel transition is driven through exactly the
same sequence of source-voltage states in both; only the *durations*, the frame
period, and the VCOM offset differ. So:

- **Contrast / final lightness:** expected to be the visible cost. Shorter
  drive per phase moves less pigment, so the extremes land short of their
  endpoints. (INFERRED.)
- **Ghosting:** expected to be worse, for the same reason, and because this bank
  is *absolute* — it drives to a target state without reference to what was on
  the panel, so residue from the previous image is not actively cancelled.
  (INFERRED.)
- **Longevity / DC balance:** this is the one to be careful about and **nobody
  in this lineage addresses it.** E-paper waveforms are normally designed to be
  charge-balanced so no net DC accumulates in the film. Shortening individual
  phases by unequal amounts changes the impulse integral, and the VCOM offset
  moves with it. Whether the shortened bank remains balanced is **not
  established by anyone**, including me. (INFERRED — flagged in §7.)

> **Q: does it apply to UC8253 (X3), SSD1677 (X4), or both?**

**SSD1677 / X4 only, in all three projects.** TrustyReader's README scopes the
firmware to the X4; its two hardware targets (ESP32-C3 and ESP32-S3) both carry a
driver whose header names SSD1677 and the 800×480 GDEQ0426T82 panel, and the
banks are duplicated between them. microreader's ESP32 display layer is likewise
SSD1677-only. Searching all three trees for the X3's controller or its 528×792
geometry returns nothing in any driver. **There is no X3 waveform work in this
lineage to take.** (OBSERVED.)

---

## 4. Mechanism specification

Written to be buildable from the datasheet (§5) with no sight of any GPL source.
Command opcodes below are the controller's own, from its published command table.

### 4a. Hardware context

- Controller **SSD1677** (Solomon Systech), SPI, 4-wire (data/command select,
  reset, busy). Supports up to 960×680; here 800×480. (Datasheet.)
- Panel **GDEQ0426T82**, 4.26", 800×480, **natively 4-grayscale**. Vendor-quoted
  full refresh 3.5 s, fast refresh 1.5 s, partial 0.42 s. (STATED — vendor
  marketing, §5.)
- Two on-chip RAM planes, conventionally "BW" (`0x24`) and "RED" (`0x26`). For
  grayscale these are not black and red — they are **the two bitplanes of a 2-bit
  image**. (OBSERVED + datasheet.)

### 4b. The waveform LUT format — this is the reusable part, and it is pure datasheet

Datasheet §6.6–6.7 (pp. 15–17). **112 bytes** total, of which command `0x32`
carries the first **105**; the remaining 7 are reached through their own
registers. Layout:

| Byte range | Contents |
|---|---|
| 0–49 | Voltage-state section: **5 tables × 10 groups**, one byte per group |
| 50–99 | Timing section: **10 groups × 5 bytes** — four phase lengths (A–D) then a repeat count |
| 100–104 | 5 bytes of frame-rate control |
| 105 | Gate voltage (also register `0x03`) |
| 106–108 | Source voltages (also register `0x04`) |
| 109 | VCOM (also register `0x2C`) |
| 110–111 | Reserved |

Semantics, verbatim in substance from the datasheet:

- **10 groups, 4 phases each (A–D) = 40 phases.**
- **Phase length `TP[nX]`**: 0–255 **frames**. 0 skips the phase.
- **Repeat `RP[n]`**: 0–255, meaning 1–256 repetitions of the group.
- **Voltage state `VS[nX-LUTm]`**: 2 bits per phase, 4 phases packed per byte
  (D7..D0 = phase A, B, C, D). The four source levels are **VSS, VSH1, VSL,
  VSH2**; for the VCOM table three levels are meaningful (DCVCOM, VSH1+DCVCOM,
  VSL+DCVCOM).
- **The 5 tables are LUT0–LUT4**: LUT0–LUT3 are the four *pixel transition
  classes*, LUT4 is VCOM. Which transition class a pixel takes is chosen by its
  two RAM bits. In a bilevel waveform these are old→new (B→B, B→W, W→B, W→W);
  **in a one-pass absolute grayscale waveform they are the four target gray
  levels.** That reinterpretation is the whole trick, and it costs nothing —
  the controller does not know the difference. (Datasheet + INFERRED.)
- **A LUT may be loaded from OTP or written by the MCU.** (Datasheet.)

Therefore: **total refresh duration = (Σ over groups of (RP+1) × ΣTP) × frame
period**, which is the formula §3a uses.

### 4c. Register order for a one-pass 4-level grayscale update

The sequence an implementer needs. Every step is datasheet-grounded.

**Initialization (once, after reset):**

1. Hardware reset: pull reset high, low, high with short settling delays
   (~20 ms / ~2 ms / ~20 ms is what the field uses).
2. Soft reset `0x12`; wait for busy to clear.
3. Temperature sensor select `0x18` → internal sensor.
4. Booster soft-start `0x0C` (panel-specific parameters — take these from the
   *panel* vendor's reference code, not from any firmware).
5. Driver output control `0x01` — gate count and scan direction.
6. Border waveform `0x3C`.
7. Set the RAM window: data entry mode `0x11`, X range `0x44`, Y range `0x45`,
   X counter `0x4E`, Y counter `0x4F`.
8. Optionally clear both RAM planes with the auto-write commands `0x46` / `0x47`.

**Per grayscale update:**

1. **Set the RAM window** (`0x11`/`0x44`/`0x45`/`0x4E`/`0x4F`) to the full
   panel.
2. **Write both bitplanes**: the low bit of the 2-bit image to `0x24`, the high
   bit to `0x26`. Chunk the transfer (4 KB chunks are what the field uses) — this
   is 48,000 bytes per plane at 800×480.
3. **Write the waveform**: `0x32` with the first 105 bytes; then gate voltage
   `0x03`, source voltages `0x04`, VCOM `0x2C` with the remaining meaningful
   bytes. *(This is the split the datasheet mandates; it is not an invention of
   any firmware.)*
4. **Set update control 1** (`0x21`) to a value that does **not** bypass the
   second RAM plane. A preceding bilevel refresh may have left the bypass set,
   and with it set the high plane is ignored and four-level grayscale silently
   degrades to two. (OBSERVED in both lineages; our own driver carries the same
   guard at `Ssd1677Driver.cpp:576-582`.)
5. **Set update control 2** (`0x22`) to a sequence that **enables clock and
   analog, runs the display stage, and does *not* include the load-LUT step.**
6. **Master activation** (`0x20`), then wait for busy to clear.

**Step 5 is the single most important detail in this document.** The `0x22`
parameter is a bitmask of sequence stages: enable clock, enable analog, load
temperature, **load LUT**, display-mode select, display, disable analog, disable
clock (datasheet p. 27 enumerates the standard combinations). **The load-LUT
stage reloads the waveform from OTP.** If it is left in the sequence, it
overwrites the LUT just written at step 3 and the update silently runs the
factory waveform instead — no error, no busy-timeout, just the wrong picture.
Both GPL projects handle this by selecting a sequence without that bit whenever a
custom LUT is live, and microreader's source comment says so in as many words.
(OBSERVED; mechanism is datasheet.) Our tree does the same at
`Ssd1677Driver.cpp:583-588`.

### 4d. What differs between the fast bank and the quality bank

At the level of §4b's layout, and with no values reproduced (MEASURED — section
comparison over data I read):

| LUT section | Fast vs quality |
|---|---|
| Voltage states (0–49) | **Byte-for-byte identical** |
| Phase lengths / repeats (50–99) | **Differ** — every active phase is shorter in the fast bank; 50 frames vs 60 |
| Frame rate (100–104) | **Differ** — fast selects the shorter frame period; comment states 2× |
| Gate + source voltages (105–108) | **Identical** |
| VCOM (109) | **Differs** — the fast bank sits at a **lower** VCOM setting. Our own header records that setting as **−1.2 V** (`Ssd1677Luts.h:91`), so an implementer has the real figure for the fast side from an MIT source and needs to sweep only the other end |

**So the fast bank is not a different waveform. It is the same waveform run
shorter, at a faster frame rate, with a shifted VCOM.** That is the entire
mechanism. An implementer reproducing it needs to derive three things — a set of
phase lengths, a frame-rate setting, and a VCOM offset — and derive them **by
measurement on the target panel**, which is the right way anyway (§6c).

### 4e. The other grayscale family, so it is not confused with this one

The same driver carries a second, *differential* grayscale pair, and its
mechanism is different in a way that matters:

- It is shorter than the absolute banks — **18 frames**, against 50 and 60.
  (MEASURED.)
- It leaves the panel in a state the next bilevel page turn must not diff
  against, so it is followed by a **revert waveform** — a second, longer pass
  (**25 frames**, MEASURED) whose job is to drive the panel back to a clean
  bilevel baseline. A latch in the driver remembers that a revert is owed.
  (OBSERVED.)
- **The absolute banks need no revert** — they self-clean, because they drive to
  an absolute target rather than a delta. (OBSERVED.)

This is a real architectural fork, and it is the reason a naive "just use the
fast LUT" port goes wrong: the absolute path also wants its own power sequencing
(a self-contained power-up/display/power-down), whereas the differential path
rides whatever rails are already up.

### 4f. Temperature compensation, and the "half refresh" trick worth stealing

The SSD1677 has an internal temperature sensor (±2 °C from −25 to 50 °C), can
read an external one over I²C, or can be **told** a temperature over SPI via
register `0x1A` (12-bit). The OTP holds waveforms per temperature range, and the
load-LUT stage picks the one matching the current temperature register.
(Datasheet §6.8.)

**Both GPL projects exploit this, and so do we.** For a mid-speed "half" refresh
they **write an artificially high temperature to `0x1A`** and then run an update
sequence that loads the LUT *without* refreshing the temperature from the sensor.
The controller therefore selects the OTP waveform intended for a hot panel —
which is shorter, because warm e-paper switches faster — and the refresh
completes sooner than a true full refresh. (OBSERVED; mechanism is datasheet.)
TrustyReader's own type comments this mode as **1720 ms**. (STATED.)

This is a clean, taint-free technique: it uses only the vendor's own OTP
waveforms and two documented registers, and invents no waveform at all. Our tree
already does exactly this — `Ssd1677Driver.cpp:59` records the stock X4 half
sequence and `:61` the temperature value it writes.

> **An implementation caution nobody in either lineage flags.** The datasheet
> defines `0x1A` as a **12-bit** value across **two** data bytes (high 8 bits,
> then the low 4 bits left-aligned in the second). Both GPL drivers send **one**
> byte. Whether the controller latches on the first byte or leaves the low
> nibble at its power-on value is not stated in Rev 1.0. It evidently works in
> the field, so the effect is at worst a small offset in the spoofed
> temperature — but an implementer should send both bytes. (INFERRED; see §7.)

---

## 5. Primary sources

These carry no GPL taint and are strictly better to build from.

| Source | URL | What it gives |
|---|---|---|
| **SSD1677 datasheet, Rev 1.0, Nov 2018** (Solomon Systech) | <https://cursedhardware.github.io/epd-driver-ic/SSD1677.pdf> | **The whole of §4b and §4c.** §6.6 waveform structure (10 groups × 4 phases, TP/RP semantics, the 4 source levels), Table 6-6 voltage-state encoding, §6.7 Figure 6-6 the full 112-byte LUT map, §6.8 temperature sensing, and the command table (`0x22` sequence options at p. 27, `0x32` at p. 31, `0x1A` at p. 26). |
| SSD1677 product page | <https://www.solomon-systech.com/product/ssd1677/> | Vendor landing page; 960×680 max, gate/source counts |
| Good Display GDEQ0426T82 | <https://www.good-display.com/product/957.html> | Panel spec: 800×480, 4.26", **4 gray levels**, 219 PPI; full 3.5 s / fast 1.5 s / partial 0.42 s (marketing) |
| Good Display SSD1677 driver-IC page | <https://www.good-display.com/companyfile/IC-Driver-SSD1677-170.html> | Vendor reference material and sample code for this controller |
| papyrix `docs/ssd1677-driver.md` | <https://github.com/bigbag/papyrix-reader/blob/main/docs/ssd1677-driver.md> | Independent prose description of the same LUT layout; quotes full ~1600 ms / partial ~600 ms; names the "write the image to **both** RAM planes after a refresh so the next differential update has a correct baseline" anti-ghosting technique |

**On papyrix as a source:** its repo is MIT but its README credits GPL-2.0 and
GPL-3.0 upstreams for named subsystems, so — per the ruling already recorded in
`ecosystem-survey-2026-09-14.md` §D-3 — its *documentation* is prose and safe to
read, while any *code* from it needs provenance checked file by file.

---

## 6. How our tree differs today, and what would have to be written

Our commit `37979be146b7330cab5ba96dcfb07a849e5ac2c6`; `freeink-sdk` at
`6b7f2161978c5cde774bc2b43b7bf0d168600a29`.

### 6a. We are ahead of TrustyReader almost everywhere

| Capability | TrustyReader | Us |
|---|---|---|
| One-pass absolute 4-level grayscale | SSD1677 only | SSD1677 (`Ssd1677Driver.cpp:576-590`) **plus** UC8279 X3, which has its own absolute 4-gray bank (`Uc8279Driver.cpp:377`). **Not UC8253 X3** — see the correction below |
| Differential 4-level grayscale ("nudge") | no | yes, on every controller — and it is the path our reader actually uses |
| Differential grayscale + revert | yes | yes, with a cheaper cleanup — we resync the second plane instead of running a revert waveform (`Ssd1677Driver.cpp:601-611`), matching stock behavior, which *"has no revert waveform at all"* |
| Windowed / partial refresh | none | SSD1677 only (`Ssd1677Driver.cpp:506`), still unexposed — see survey §D-2 |
| Tiled/streamed grayscale planes | no | yes (`lib/hal/HalDisplay.h:129`, `Ssd1677Driver.cpp:556-564`) — bands, so a full plane pair need not be resident |
| X3 grayscale preconditioning | no | yes (`lib/hal/HalDisplay.h:110-111`) — an OEM settle pass before the planes are written |
| Per-batch controller variants | no | yes — UC8179/UC8279 substitutions, 4 LUT header files |
| Named waveform banks | 4 (X4 only) | X3 alone has 6 (`Uc8253X3Luts.h:5-10`: normal, half, **fast**, full, **gc**, plus an AA pre-pass at `:133`) |

Note our X3 `_fast` bank is annotated *"papyrix turbo (full voltages, shortened
timing)"* — the same shorten-the-timing idea, already applied on the controller
TrustyReader never touched.

**Correction to an earlier draft of this section, worth recording because it is
the easy mistake here.** UC8253 X3 accepts the `factoryMode` flag but has **no
absolute grayscale bank** — it falls back to the bilevel full bank, and the
source says why: the OEM standalone grayscale flow *"needs a different panel init
(PSR/PWR/VCOM rails) and DTM framing that isn't ported"*
(`Uc8253X3Driver.cpp:424-430`). So the flag being plumbed on a driver does **not**
mean the capability exists there. On UC8253 the only real grayscale is the
differential nudge bank. (OBSERVED.)

### 6a-bis. The capability difference that actually matters: one-way vs absolute

Our differential grayscale **cannot darken a pixel.** It lifts a black pixel
toward white and has no path in the other direction — the relevant transition
cell is deliberately passive (`Uc8253X3Luts.h:117-123` for X3, `Ssd1677Luts.h:11-20`
for X4), and the constraint is stated with citations at
`lib/GfxRenderer/GlyphAaPlanes.h:11-33` and `src/TextAntiAliasing.h:13-25`. It is
why the renderer never emits a low-plane-only pixel
(`GlyphAaPlanes.h:74-77`) and why grayscale must always be layered on a bilevel
base frame.

**An absolute bank has no such restriction** — it drives each pixel to a target
level regardless of what was there. That, not speed, is the strongest reason to
finish the plumbing in §6b: it is the difference between an antialiasing nudge
and real 4-level image rendering. It also explains the awkward workaround our
image path currently carries — a double fast refresh with selective blanking,
because a HALF *"sets particles too firmly for the grayscale LUT to adjust"*
(`src/activities/reader/EpubReaderActivity.cpp:1773-1802`). An absolute bank
would not need that. (OBSERVED + INFERRED.)

### 6b. The one real gap: the fast absolute bank is unreachable from CrossPoint

This is the actionable finding.

- The waveform exists: `Ssd1677Luts.h:92`.
- The SDK can select it: `FreeInkDisplay.h:246` takes an optional LUT pointer and
  a `factoryMode` flag; `Ssd1677Driver.cpp:576` chooses `lut_factory_quality`
  when `factoryMode` is set, and `:578-590` runs the correct self-contained
  absolute sequence (CTRL1 reset, one power-cycling update, no revert owed).
- `Ssd1677Luts.h:90` even documents the intent: *"…for standalone wallpapers/
  covers"* — precisely TrustyReader's use case.
- **But our HAL drops both parameters.** `lib/hal/HalDisplay.h:124` declares
  `displayGrayBuffer(bool turnOffScreen = false)`, and
  `lib/hal/HalDisplay.cpp:193-196` forwards only that one argument, so the SDK's
  `lut` and `factoryMode` always take their defaults (`nullptr`, `false`).
- I grepped every `displayGrayBuffer(` call site in `src/`, `lib/` and
  `freeink-sdk/libs/`: **six callers, none passing either parameter** —
  `TextAntiAliasing.cpp:66,90`, `EpubReaderActivity.cpp:1879,1934,1979`,
  `XtcReaderActivity.cpp:296`, `SleepActivity.cpp:369`, all via
  `GfxRenderer.cpp:3121`. **Nothing in CrossPoint can reach factory mode.**
  (OBSERVED.)

**What an implementer would write** — and it is small:

1. Widen `HalDisplay::displayGrayBuffer` to carry a mode (`lib/hal/HalDisplay.h:124`,
   `lib/hal/HalDisplay.cpp:193-196`), defaulting to today's behavior so every
   existing caller is unchanged.
2. Pass it through `GfxRenderer::displayGrayBuffer` (`GfxRenderer.h:550`,
   `GfxRenderer.cpp:3121`).
3. Select factory mode at the **image** call sites — covers and full-page images
   in `XtcReaderActivity.cpp:296` and `EpubReaderActivity.cpp:1979` — and leave
   the **text antialiasing** sites (`TextAntiAliasing.cpp:66,90`) on the
   differential path, which is shorter (18 frames vs 50) and is the right trade
   for a page of glyph edges.
4. Check the interaction with `cleanupGrayscaleBuffers`: the absolute path
   self-cleans and must **not** also be handed a cleanup pass, or the saving is
   spent twice over.

No waveform work, no new bytes, no licence exposure. Whether it is *worth* doing
is a measurement question — see §7.

### 6c. If the quality bank is ever wanted, it must be measured, not copied

The one thing TrustyReader has that we do not is a **slower, better** absolute
bank for image-heavy pages. Copying their values is both a licence violation and
technically wrong: our `BoardConfig.h` already carries per-batch UC8179/UC8279
substitutions precisely because panels from different batches do not share a
waveform, and their values were tuned on their panel.

The derivation is mechanical and needs no sight of their source:

1. Start from `lut_factory_quality`, which is already ours.
2. Leave the voltage-state section **alone** — §4d establishes the two banks
   share it, so the quality bank is purely a timing and VCOM change.
3. Lengthen the phase durations in the three active timing groups and select a
   longer frame period; sweep both and photograph the result.
4. Sweep VCOM for the best black level without edge artifacts.
5. Judge against a real page on our panel, at more than one temperature.

That is a bench job of a few hours, and the result is ours outright.

---

## 7. Open questions, and what I could not establish

1. **No display timing was ever published by this lineage, and we have no figure
   for a grayscale waveform either.** The ~60% is arithmetic (§3a), not a
   stopwatch, and their two benchmark harnesses time EPUB parsing rather than
   the panel (§1a). **Nobody has measured this on glass — including me.**

   We are better instrumented than they are on the bilevel side and no better on
   the grayscale side. What we have: X4 measured over 190 refreshes — FAST
   **496–498 ms** (186 of 190), HALF/FULL **1639–1674 ms** (17 of 190), compose
   ~8 ms median (`docs/notes-and-claude.md:198-248`); whole-panel 570 ms across
   five samples (`docs/ble-editor-spike.md:41-43`); a `~77 ms` DU-shortcut
   estimate (`Ssd1677Driver.cpp:109`). What we do **not** have, and
   `docs/two-bit-chrome.md:156-168` says so explicitly: **any wall-clock figure
   for the grayscale waveform itself.** That is the measurement to take first,
   because both this document's §6b and the whole "is the fast bank worth
   wiring up" question rest on it. The apparatus exists; the number does not.
2. **The frame-rate encoding is undocumented in datasheet Rev 1.0.** It
   allocates 5 bytes to frame rate but publishes no setting→period table; I
   searched the full extracted text and found none. The 2× ratio is a source
   comment. Until that is confirmed — against a later revision, a Good Display
   application note, or an oscilloscope — **the 58.3% figure is unverified**, and
   the frames-only bound (16.7%) is the part that is solid.
3. **DC balance is unaddressed by everyone.** Whether shortening the phases
   unequally leaves the waveform charge-balanced is not established by
   TrustyReader, by microreader, by the datasheet, or by me. This is the one
   finding here with a plausible route to *hardware* harm, and it should gate any
   long-term use of a hand-tuned short waveform. I could not establish it and did
   not try to model it.
4. **Two ghosting/contrast claims are inferred, not measured** (§3). Their only
   statement is "slightly lower quality".
5. **A documentation conflict in the ancestor, unresolved.** microreader's
   header comment for this bank states one mapping from the 2-bit RAM state to
   gray levels, and its own per-table comments immediately below state the
   **reverse** mapping (which end is black). One of them is wrong. TrustyReader
   dropped the per-table annotations entirely and inherited neither. **An
   implementer must establish plane polarity empirically** — render a known
   4-level ramp and look at it. Our own tree has already been bitten by this
   class of bug: `Uc8279 grayscale plane inversion bug` appears in the SDK
   history cited in the survey.
6. **The `0x1A` single-byte write** (§4f) — works in the field, not explained by
   Rev 1.0. Low confidence that it matters; worth one experiment.
7. **Not chased:** `HookedBehemoth/microreader` branch `research`, referenced
   from TrustyReader's README, which may hold the reverse-engineering notes that
   produced the factory bank. It would be the most interesting *provenance*
   find — it might show the bank came from the stock firmware, which would raise
   a separate question about its licence status in *both* the GPL and the MIT
   lineages. Flagged rather than opened, because it changes nothing about §6b.
8. **The bank is stock-firmware-derived, and that is a separate licence question
   from the GPL one.** Partly resolved since this document's first draft: our own
   `Ssd1677Luts.h:89` states plainly that it is *"from OEM firmware"*, and the
   X3 driver describes the OEM standalone grayscale flow it could not port
   (`Uc8253X3Driver.cpp:425-427`) — so the extraction is acknowledged on our side,
   not merely suspected. What remains open is what that implies: **"we have it
   under MIT" is a statement about the OpenX4 SDK's licence, not about the bytes'
   ultimate provenance**, and the same caveat applies with equal force to the GPL
   lineage, which took the same bytes from the same place. This is not a new
   exposure created by anything in this document — the bank has been in our tree
   and shipping for some time — but it is the reason §6c's measure-don't-copy
   route is the right one if anyone ever wants certainty rather than parity.
   Not a question I can settle; flagging it as legal, not technical.

### What I checked and found clean

So the next pass does not re-open these:

- All three GPL repos' licence files read in full — GPL-2.0 v2 June 1991, all
  three, byte-identical size. No dual-licensing, no exceptions header.
- Searched all three trees for UC8253, UC8179, 528×792, and X3: **no X3 display
  support anywhere.** (§3, last question.)
- Searched all three trees for the "60%" string: **one origin commit in
  TrustyReader, zero in microreader, zero in TernOS.**
- Their two benchmark binaries: **not display benchmarks.** (§1a)
- TernOS's X4 driver: **same four banks, no new waveform.** (§1a)
- `sunwoods/Xteink-X4`: **hardware teardown, no LUTs, no LICENSE.** (§1a)
- Our `lut_factory_quality` vs both GPL copies: **all 110 significant bytes
  identical** — established by extraction and comparison, not by eye.
- Our six `displayGrayBuffer` call sites: **none reaches factory mode.** (§6b)
- Our frame-count derivation vs our own vendored header: **agree at 50 frames**
  (`Ssd1677Luts.h:90`), reached independently. (§3a)
- `lib/hal/HalDisplay.h` line citations in §6b re-verified directly against the
  working tree after a parallel pass reported different numbers; **mine are the
  current ones** (`:110-111`, `:117`, `:124`, `:129`). The submodule was moving
  during this investigation, so re-verify before acting on any `freeink-sdk`
  citation here.

---

*No source code, pseudocode, identifier, or waveform/register byte table from any
GPL-2.0 project is reproduced in this document. Frame counts, group counts,
section-level equality results and register opcodes are facts about how the
hardware is driven; the opcodes and the LUT layout come from the controller
datasheet in §5.*

## Update 2026-09-14 — the plumbing half landed

`HalDisplay::displayGrayBuffer` now forwards the SDK's `lut` and `factoryMode`
arguments instead of dropping them (`dabda89f9`), and
`HalDisplay::supportsAbsoluteGrayscale()` answers the question this document
says a caller must be able to ask: **false on UC8253/X3**, where the flag is
accepted and there is no absolute bank behind it. No call site changed, so the
commit is behaviour-neutral; what changed is that the capability is now
reachable and askable.

Unchanged and still outstanding: **nobody has a wall-clock figure for any
grayscale waveform on our own panel**, which is the measurement this document
says to take before acting on §6b, and which cannot be taken off-device.
