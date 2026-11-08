# Picco gesture recognition, multiple passes — 2026-11-07

Owner, verbatim: *"take multiple passes at improving the gesture recognition on
picco touch screen"*.

Branch `lag-reduction` (worktree), on top of `8293ef507`. Code:
`src/components/PiccoInput.h` (`GestureDecoder`), tests
`test/picco_input/PiccoInputTest.cpp`, benchmark
`test/picco_input/PiccoGestureBench.cpp`, one gate in
`src/activities/reader/EpubReaderActivity.cpp`.

**Constraints kept, all owner rulings (docs/picco-port-2026-11-05.md):** tap
≤ 400 ms and ≤ 28 px, fires Right on lift; hold = still at 750 ms, Confirm; the
SDK's swipes (≥ 60 px within 700 ms) left = Right, right = Left, up = Back,
down = Confirm; left-edge swipe = Back and wins; inside keyboards only the edge
Back; between a tap and a hold, or a drift short of a swipe, fires nothing. No
threshold was changed.

**Status: SHIPPED to the branch — UNCONFIRMED on the device.** There is no
device trace of this controller; everything is host-measured. What stands:
pass 4 (the reader stops stalling the touch poll) and pass 5 (a hold needs a
fresh report), plus the benchmark as a gate. Passes 2, 3 and 3b were measured
and reverted, and why is below so they are not tried again blind.

## Pass 1 — a benchmark, because there was nothing to judge by

`PiccoGestureBench` drives the real `GestureDecoder` with modelled human
gestures through a modelled TMA525C and SDK, 2,000 per kind per controller,
seeded. It models what the code shows: the SDK keeps a contact whose lift-off
report was lost "down" for 3 s; a new finger's reports move that stale contact;
`reportMs` is the poll time a report was read at; the loop polls every 10 ms
while a finger is down, with occasional 100–300 ms stalls.

**Every model parameter is an assumption** (report rate, noise, lost lift-off
rate, stall rate, human gesture ranges); four controllers bracket them. One
modelling choice matters and is unverified: after a stall, the SDK is modelled
as reading the LATEST controller state (so a lift and a new touch inside one
stall can merge). If the controller queues reports instead, stalls cost less
than the tables show.

Baseline (the decoder as committed):

| Controller | correct | WRONG | missed | tap, tap |
|---|---|---|---|---|
| clean 100 Hz | 98.3% | 1.2% | 0.5% | 97.5% |
| noisy 60 Hz | 97.4% | 1.5% | 1.1% | 95.0% |
| lossy 100 Hz (5% lift-offs lost) | 94.2% | 1.1% | 4.7% | 87.5% |
| bad 60 Hz (15% lost, noisy) | 83.6% | 1.6% | 14.8% | 68.2% |

Where the losses were: every lost lift-off cost its gesture (and the next tap
inside the SDK's 3 s hold-over); swipes drifting past 45° fired the
perpendicular direction (the WRONG column); main-loop stalls lost the rest.

## Pass 2 — end a contact the controller went silent on (REVERTED after review)

The idea: the stale-contact guard already decides that a poll finding the
controller silent for more than 200 ms means the lift-off was lost, and only
refuses the hold. Pass 2 went further -- the silence ENDED the contact
(classified as released at its last report, the next report a new contact), a
second finger after a 50 ms silence was split off as its own gesture, and tap
and swipe durations were timed from report stamps instead of poll times. On the
original benchmark it looked like the whole answer: with 5% of lift-offs lost,
94.2% → 98.4%, as good as a clean controller.

**The adversarial review refuted it, by running traces, and it is reverted.**
The original benchmark had no gesture whose right answer is NOTHING and never
dropped a report while a finger was still down, so it could not see the cost:

- a report dropout of over 200 ms during a hold or a slow press now FIRED Right
  (a page turn), sometimes Right then Confirm; at a 0.2%-per-report dropout
  rate, 8.7% of holds turned a page and slow drags fired a wrong action 13.8% of
  the time -- where the committed decoder did nothing;
- a 60 ms report gap on a moving finger split one swipe into two gestures,
  including Right-then-Back (out of the book);
- under main-loop stalls, report-stamp timing let 450-700 ms presses read as
  taps (1.7% on the clean controller).

A lost gesture costs a repeat; a page turned or a book left that nobody asked
for costs more, and the owner has confirmed the committed decoder "all works"
on the device. Without a device trace of how often the TMA525C goes quiet under
a resting or moving finger, the trade cannot be judged, so the committed
decoder stands. The review's two extra gestures (450-700 ms presses, slow
drags -- both must fire nothing) and the dropout controller are now in the
benchmark, so a future attempt is scored against them.

## Pass 3 — a dead band around the diagonal (REJECTED)

Swipes past 45° fire the perpendicular direction — a page-turn swipe read as
swipe up is Back, out of the book. A dead band (the dominant axis must beat
the other by a ratio, else nothing) was measured at ratios 1.15, 1.25, 1.40.
Each traded about two misses for every wrong action removed (clean: wrong
1.2 → 1.0%, missed 0.4 → 1.0% at 1.15). Not worth it; reverted.

## Pass 3b — refuse a tap whose end was not watched (REJECTED)

Tracing the left-edge Back swipes that fired Right (13 in 2,000): a stall that
starts just after the finger lands hides the whole swipe, and the decoder sees
its first point and then the lift — a tap. Refusing a tap whose lift came more
than 60 ms after the previous poll cost 2.8% of taps (a stall over an ordinary
tap's lift is far commoner). Requiring 1–50 ms of observed stillness first
traded taps for edge swipes about one-for-one at every setting. Timing alone
cannot tell them apart; reverted.

**The fix that would work needs a device trace:** the TMA525C's lift-off
record (event 3) very likely carries the final coordinates, and the SDK
discards them (`InputManager::pollTma525c`, the `lifted` branch keeps the
previous point). With them, a swipe hidden in a stall would be classified from
where it actually ended. Before using them: log a few lift-off records with
`TOUCH_PROBE_DEBUG` to confirm the coordinates are real, not zero.

## Pass 4 — don't stall the loop while a finger is down (KEPT)

With stalls switched off in the model (and pass 2 in place, which handled the
lost lift-offs), everything except the diagonal WRONGs scored 100%. So the remaining losses are the main
loop not polling. The reader's own idle work runs on the loop task: the glyph
prewarm and, since the lag work earlier today, the PSRAM next-page prerender.
On the Picco both now wait while a finger is on the glass and for 1 s after
the last report (`touchActiveNow`, the same `pollFast` signal the idle poll
uses). The chapter build is deliberately not held back: deferring it could run
a reader who turns by tapping into the end of the laid-out pages, a far bigger
stall.
Review note (accepted): a thumb RESTING on the glass keeps both off for as long
as it rests, and a controller wedged with INT low would keep them off for good.
Either costs lag only, never correctness.

## Pass 5 — a hold needs a fresh report (KEPT)

The extended benchmark showed a WRONG action in the committed decoder itself: a
550-749 ms press whose lift-off is lost reaches 750 ms with the controller quiet
only ~150 ms (under the 200 ms gap rule) and still-looking, so the hold fired
Confirm on a finger that had left -- 42 in 2,000 such presses at a 5% loss
rate, 134 at 15%. A hold now fires only on a poll whose latest report is under
50 ms old (`HOLD_FRESH_REPORT_MS`). A resting finger reports every 10-20 ms, so
a real hold fires within one report of 750 ms; a lift-off lost just before 750
never gets a fresh report and goes stale at 200 ms. No threshold of the ruling
moves.

| Controller | correct | WRONG | missed | press 0.5 s WRONG |
|---|---|---|---|---|
| clean 100 Hz | 98.7% | 1.0% | 0.4% | 0.0% |
| noisy 60 Hz | 98.1% | 1.2% | 0.8% | 0.3% |
| lossy 100 Hz | 95.5% | **0.9%** (1.1) | 3.6% | **0.0%** (2.1) |
| bad 60 Hz | 87.2% | **1.4%** (2.2) | 11.3% | **0.3%** (7.0) |
| clean + 0.2%/report dropouts | 95.8% | 1.3% | 2.9% | 0.3% |
| clean, no stalls | 99.3% | 0.7% | 0.0% | 0.0% |

(parentheses: the committed decoder.) The one cost: with report dropouts, a hold
that reaches 750 ms inside a dropout now waits for it and then goes stale --
holds 90.6% → 88.2% on that (hypothetical) controller. Unit tests
`ALostLiftOffJustBeforeTheHoldIsNotAHold`, `AHoldWaitsForAFreshReport`.

## Gate

`PiccoGestureBench.Scorecard` asserts floors a little under the decoder after
pass 5, per controller: overall correct, overall WRONG (the must-do-nothing
presses count), and both taps of a quick pair. The first version of the gate
(pass 2's floors) was verified to fail against the committed decoder; the
current floors fail it on the bad controller's WRONG ceiling (2.2% > 1.7%).

## Not done

- **The diagonal WRONGs (~1–2% of swipes)** remain; the only fix measured cost
  more than it saved.
- **Lift-off coordinates** (pass 3b) wait for a `TOUCH_PROBE_DEBUG` log.
- **Model realism**: report rate, noise, loss rate, dropout rate and stall rate
  are guesses; one `TOUCH_PROBE_DEBUG` session -- how often a resting finger
  goes quiet past 150-200 ms, the report gaps under a moving finger, whether
  reports queue across a stall -- would replace them with numbers, and decide
  whether a pass-2-style recovery is ever worth its risk.
- **Lost lift-offs** still cost their gesture (lossy controller: 3.6% missed).
