# Settings reduction plan — every setting, a hardcode proposal, and the odds

Date: 2026-08-21, firmware at 3573e1dca. Owner ask: *"look at every setting and
determine if it could be hardcoded instead and what that value would be and the
likelihood of me agreeing."* Analysis only — nothing here is changed yet.

**Scope note:** the iOS "Page Colors" and "Page Colors — Custom" groups
(pageFadeSeconds, pageFadeDepthPercent, beamPaintMs, presentFlash, both grain
percents, grainCoverage, blotchDepth, palettePreset, four custom hex fields)
are being worked by another agent right now and are deliberately NOT triaged
here.

**STATUS: EXECUTED 2026-08-21** — owner ruling, verbatim: *"yes to all of
these"*, landed as 8ff603517 and released the same night in 1.5.4-BD. The
places the executed change differs from the table below:

* **lineSpacing stayed a live field** (row deleted, value persisted manually).
  The plan called it web-only; the compiler found a designed on-device chord —
  Confirm held + side button steps it (EpubReaderActivity ~:526). Deleting a
  live gesture on a mischaracterization would be silent capability removal, so
  the chord and field stay; only the settings surface is gone.
* **sleepScreen options trimmed** (second ruling, same day, off live renders:
  *"keep calendar and westside calendar"*): CALENDAR_FOUR/FIVE/SIX withdrawn —
  FIVE was a literal duplicate of CALENDAR, both drawing the 5-week Spanish/CR
  screen. Enum values stay frozen as the persisted encoding; stale saves remap
  to CALENDAR on load, and the render switch folds the three onto the classic
  screen. The trim surfaced a mechanism gap caught by SCREENSHOT, not tests:
  `settingorder::resolve` demanded a full permutation, so the shortened order
  silently degraded to identity and the picker showed all twelve. Withdrawal
  is now explicit (`withDisplaySubset`); the permutation gate stays the
  default so an accidentally short table still cannot hide a choice.
* **iOS renderScale RESOLVED by ruling, not hardcode** (2026-08-21: "keep 2x
  and 3x"): the Sharpness row survives with two options; Panel (1x) left the
  picker, stored 1s floor to 2 in the getter, and desktop QA keeps 1x via
  CROSSPOINT_SIM_RENDER_SCALE. Landed as crosspoint-simulator e93605d. No
  phone eyeball needed — the choice stays with the owner.
* **iOS Settings.bundle rows NOT edited yet** — deferred behind the
  color-pages agent so Root.plist is edited once.
* Executed removals also deleted their dead plumbing: the sleep-timeout
  picker, the quickResume auto-enable machine (sleepscreen::reconcile +
  initialState + 7 of 10 policy tests), the frontButton remap
  parsing/validator (the "RemapFrontButtons sub-activity" it credited never
  existed), the web page's quickResume/shortPwrBtn JS coupling, and the
  per-board hasTouch() list filters.

Likelihood calibration comes from standing rulings: four of five themes
deleted; mottleCells removed as a setting the day it was asked; Display /
Controls / Reader tabs withdrawn; "dedicated e-reader, not a Swiss Army knife";
no-touch ruling 2026-08-05; but also deliberate KEEPS — Screen Margin was one
of exactly two Reader rows the owner kept, the 13-grid keyboard setting was
built on request, the editor faces are personally curated.

## The shape of the estate

| surface | rows | of which already effectively hardcoded |
|---|---|---|
| Device Settings UI (System rows + actions) | 13 rows + 9 actions | — |
| Hidden but persisted (web API only) | 23 | 6 pinned every load |
| iOS Settings.bundle (excl. Page Colors) | 10 | — |
| Desktop settings.json | mirrors the above + sim dials | — |

The hidden-but-persisted block is the big win: six of those are ALREADY pinned
by `normalizeRetiredSettings()` — a web write survives until next boot, then
reverts. They are hardcoded in all but storage. Finishing the job (delete
field + row + pin) is bookkeeping, not a behavior change.

## A. Hidden-but-persisted (web API only) — delete candidates

These have NO on-device control today. "Hardcode" means: delete the field, the
list row, the JSON key, and any pin; consumers read the constant.

| setting | default today | hardcode to | odds | why |
|---|---|---|---|---|
| `systemFont` | pinned LIBREFRANKLIN | Libre Franklin | **HIGH** | Your ruling 2026-08-07 already made it one-value; the row is a decode surface only. |
| `clockFormat` | pinned 24h | 24h | **HIGH** | B-019: NOTHING reads it. A setting with zero consumers. |
| `sleepScreenCoverMode` | pinned FIT | FIT | **HIGH** | You pinned it yourself (T-019, 2026-08-15). |
| `sleepScreenCoverFilter` | pinned NO_FILTER | NO_FILTER | **HIGH** | Same T-019 pin. |
| `touchReaderControls` | ON | delete outright | **HIGH** | No-touch ruling: existing touch surfaces are removal candidates, not precedent. |
| `fadingFix` | 0 | 0 (off) | **HIGH** | Sunlight-fading workaround for a panel variant; no way to turn it on from the device; you have never asked for it. |
| `hideBatteryPercentage` | pinned ALWAYS | always hide | **HIGH** | Already pinned; the reader chrome you kept has no battery %. |
| `refreshFrequency` | 15 pages | 15 pages | **MED-HIGH** | E-ink ghosting cadence. 15 is fine on the X4 panel; a different panel might want 5. Cheap to hardcode, cheap to regret. |
| `shortPwrBtn` | pinned SLEEP | SLEEP | **MED-HIGH** | Pinned already — but pinned TO a choice you made; deleting the enum also deletes the footnotes/page-turn modes' plumbing. |
| `longPressButtonBehavior` | pinned FONT_SIZE_STEP | FONT_SIZE_STEP | **MED-HIGH** | Pinned already; the gesture itself stays, only the choice of what it does goes. |
| `pwrBtnFootnoteBack` | 1 | 1 | **MED-HIGH** | Sub-option of a pinned setting. |
| `backShortToFileBrowser` | 0 | 0 | **MED-HIGH** | Navigation tweak nobody can reach. |
| `sideButtonLayout` | PREV_NEXT | PREV_NEXT | **MED** | You are right-handed and ruled CCW out; Up=Prev matches. But it also carries DISABLED, which a pocket-carry user genuinely wants. Deleting kills that escape hatch. |
| `lineSpacing` | NORMAL | NORMAL | **MED** | Reading-taste, but you withdrew the Reader tab yourself. Risk: cache-invalidation key — hardcoding simplifies the invalidation matrix. |
| `paragraphAlignment` | JUSTIFIED | JUSTIFIED | **MED** | Same class. BOOK_STYLE exists because someone cared once; was it you? |
| `extraParagraphSpacing` | 1 (on) | 1 | **MED** | Same class. |
| `embeddedStyle` | 1 (on) | 1 | **MED** | Whether EPUB CSS is honored. Off is the escape for a badly-styled book — a real reader need, reachable only via web today. |
| `hyphenationEnabled` | 1 (on) | 1 | **MED** | You invested in the hyphenation trie; off exists for debugging. |
| `textAntiAliasing` | STANDARD | ? | **LOW-MED** | Four modes and you personally compared AA looks during the CRT work. Likelier: trim to 2 (Off/Standard) than hardcode 1. |
| `imageRendering` | DISPLAY | DISPLAY | **LOW-MED** | Suppress/placeholder are real memory-pressure escapes on 380KB. I would keep the enum internal even if the row dies. |
| `clockHasBeenSynced` | 0 | make internal | **HIGH** | Not a preference at all — an NTP debounce flag wearing a toggle costume. Persist it outside the settings list. |
| `frontButtonLayout` + 4 remap bytes | defaults | delete | **MED-HIGH** | Not even in getSettingsList — pure dead persistence unless the web UI reaches them. Verify consumers, then delete. |
| `fontSlotNeedsMigration` | false | internal | **HIGH** | Migration bookkeeping, not a setting. |

## B. Device-visible System rows — mostly keeps

| setting | default | proposal | odds | why |
|---|---|---|---|---|
| Text Settings (font family + size) | LibreFranklin / 14pt | **KEEP** | — | The product. You curate .cpfont families weekly. |
| `screenMargin` | 5 px | **KEEP** | — | One of exactly two Reader rows you kept on 2026-08-04. Proposing its removal would be re-litigating your ruling. |
| `darkMode` | 0 | **KEEP** | — | Core polarity toggle; you just spent a session fixing its iOS mirror. |
| `sleepScreen` | DARK (12 options) | **KEEP row, TRIM options** | **MED** | You built the calendars and use them. But 12 options is a menu of museums: DARK/LIGHT/CUSTOM/COVER_CUSTOM/QUICK_RESUME could plausibly fold to 6-7. Which calendars survive is your call, not mine. |
| `sleepTimeoutMinutes` | 10 | hardcode 10 | **MED** | Value row, 1..60. If you have never moved it off 10, it is a constant with a UI. |
| `quickResumeSleepScreen` | off | hardcode off? | **LOW-MED** | You un-pinned this deliberately (it "was worse than no row" pinned). Recent deliberate surfacing = keep. |
| `displayDebounce` | default ms | hardcode default | **MED-HIGH** | Typing redraw delay for BT keyboards. Feels like a tuning dial that found its value; if you have not touched it since the editor work, freeze it. |
| `editorFont` / `editorFontSize` | curated / 12 | **KEEP** | — | You personally licensed PragmataPro and Nitti for this. |
| `keyboardLayout` | GRID13 | **KEEP** | **LOW** | You designed the 13-grid and kept Daisy/QWERTY on purpose (2026-08-05). |
| `clockUtcOffsetQ` | UTC-5 (28) | **KEEP** | — | Feeds the calendar sleep screens; Costa Rica travel means it moves. |
| `keepScreenAwake` (sim) | 0 | see iOS section | MED | Overlaps allowSleep* toggles — three controls for one behavior across two surfaces. |
| `ownerName`, `language` | "" / EN | **KEEP** | — | Identity + i18n actions, not dials. |

## C. iOS Settings.bundle (excluding Page Colors — other agent)

| setting | default | proposal | odds | why |
|---|---|---|---|---|
| `allowSleepOnBattery` | true | collapse | **MED-HIGH** | Two toggles + firmware keepScreenAwake = THREE controls for "may the phone sleep". One toggle (or none: always allow, rely on iOS) would do. |
| `allowSleepWhileCharging` | true | collapse into above | **MED-HIGH** | Same decision, split by power source nobody asked for. |
| `renderScale` | 3 | **hardcode — but measure first** | **MED** | Measured: 2x is pixel-exact on iPhone Air (1.0000), 3x always minifies (0.7955). If a side-by-side on the phone confirms 2x reads better, hardcode 2 and delete the row. Needs your eyes before any change. |
| `padContrastPreset` | 4 | hardcode preset 4 | **MED-HIGH** | Five rows of pad-contrast tuning that converged during the outline work. |
| `padOutline/FillContrast` ×4 | -1/-1/1/1 | delete with preset | **MED-HIGH** | The custom escape hatch for a dial that has settled. |
| `readAloudEnabled` | false | **KEEP** while experimental | **LOW** | Labeled experimental; the gate IS the feature flag. |
| `readAloudRatePercent` | 100 | hardcode 100 | **MED** | If read-aloud graduates, rate is the one voice dial people do use — trim later, not now. |
| `diagnosticsEnabled` | false | **KEEP** | — | The a11y.log lesson: 12 builds burned without diagnostics. This toggle is the instrument-first rule made flesh. |

## D. Desktop settings.json (fs_/.crosspoint + sim template)

Mirrors the firmware keys plus the sim dials the other agent owns. One
sim-only key worth a look once Page Colors lands: `zen` has no key (env only)
— correct, gestures are not settings. No independent candidates here.

## Suggested execution order, if the rulings land

1. **The already-pinned six + the two non-settings** (systemFont, clockFormat,
   coverMode, coverFilter, hideBattery, shortPwrBtn/longPress if ruled, plus
   clockHasBeenSynced and fontSlotNeedsMigration going internal) — zero
   behavior change, pure deletion, one commit.
2. **touchReaderControls + fadingFix + frontButton bytes** — small, each needs
   a consumer grep first (orphan-check rule: grep the simulator repo too).
3. **The reading-taste block** (lineSpacing, alignment, spacing, embeddedStyle,
   hyphenation, AA, images) — one ruling session, since each deletion also
   simplifies the section-cache invalidation matrix.
4. **iOS pad/sleep rows** — after the Page Colors agent lands, so the bundle
   is edited once.

Every deletion follows the never-silently-remove rule: cite the consumer graph,
keep anything the web UI or another repo reads, and the JSON keys of deleted
fields must still PARSE (ignored, not error) so old settings.json files load.
