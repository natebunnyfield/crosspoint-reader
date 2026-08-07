# Daisywheel text entry — interaction spec (draft, 2026-08-04)

Replacement for the QWERTY on-screen keyboard: a Steam-Big-Picture-style daisywheel
adapted to the X4/X3's actual controls — **two front rockers in a row plus the side
rocker; there is no d-pad**. Iterated interactively in the design playground:
[docs/daisywheel-playground.html](daisywheel-playground.html) (self-contained HTML;
open in any browser — arrow keys drive it, odometer counts actions).

## Controls during wheel entry

| Physical control | Role | Long-press |
|---|---|---|
| Front **Left / Right** (a rocker) | Rotate petal focus, wraps, auto-repeats | — (rotation repeats; no long-press role) |
| **Side Up** | Pick **top** char of focused petal | Uppercase of that char |
| **Select** (Confirm) | Pick **middle** char | Uppercase |
| **Side Down** | Pick **bottom** char | Uppercase |
| **Back** | Exit the wheel | — |

Rules settled in the session:

- **Long-press means uppercase and nothing else.** No hold-to-flip, no hold-for-space.
- **Letters always read as an upright vertical column** in every petal, at every wedge
  angle; top char = side Up everywhere. No icons on the wheel.
- **Two rings** — letters and one consolidated numbers+symbols ring — toggled by the
  ⇄ slot on the utility petal.
- **Ring flip stays on the same petal**: ⇄ is pressed on the utility petal and lands on
  the other ring's utility petal (defensive rule for other paths: index preserved,
  clamped to the last petal).
- **Two rings, abc and 123** (owner ruling 2026-08-06). Coverage is now every
  printable ASCII character: a-z with uppercase on long-press, 0-9, all 32
  punctuation marks, and space. The wheel previously had no
  `< > [ ] { } \ | ^` and no backtick at all, and `(` and `)` were two
  rotations apart — so a bracket could not be matched without crossing the ring,
  and a WiFi password containing one was untypable.
- **The 123 ring is ordered by QWERTY, left to right.** Digits lead, then the
  symbols follow the keyboard: the `` ` `` key, the shifted number row, then
  `- = ; ' , . /` in the order your hand finds them. Not alphabetical, not
  frequency — an order you can predict from a keyboard you already know.
- **Bracket petals keep a pair together: open above close.** Up types the open
  bracket, Down the close, and Confirm a related character — so a pair is always
  one button apart. The related character is the QWERTY-adjacent key, which for
  three of the four is simply the next one right: `[ ] \`, `{ } |`, `< > ?`.
  `( )` is the exception: the key right of `)` is `- _`, already its own pair,
  so it takes `*` from the immediate left, which is also far more useful beside
  parentheses in markdown.
- **Both rings end in SPACE in the bottom slot**, so the space target does not
  move when you swap. The 123 ring's last petal is `. , space`; the two above
  space are deliberate duplicates of the commonest characters in prose, since
  every character already has a home earlier in the ring.
- **Utility petal, top→bottom: ⌫ ⇄ OK** — ⌫ and OK non-adjacent (accidental-submit
  finding from the AAC literature). Utility petal is the last petal of each ring,
  wrap-adjacent to petal 1.
- **The third slot follows the HOST in the split-screen panel**: it reads `ret`
  and types a newline in Create Note, `ask` in Claude, and stays `ok` on the
  full-screen wheel, which is a single-line field. Before that it was `ok`
  everywhere and always meant Done, which is why the daisywheel had no way to
  type a line break — and made OK in the note editor look like it discarded
  work. Back still saves and leaves.
- **The ⇄ label names the ring it takes you to.** It was hardcoded to `123`, so
  it still said `123` while you were already on the number ring.
- ~~Touch (X4 Pro): tap petal = focus, tap char = type, hold char = uppercase.~~
  **Superseded 2026-08-05: this fork never supports touch or the X4 Pro** — the
  wheel is buttons-only.

## Rings

Petals listed clockwise from 12 o'clock; each petal is `[top, middle, bottom]`.

**ABC ring (10 petals):**
`abc` `def` `ghi` `jkl` `mno` `pqr` `stu` `vwx` `yz␣` `⌫⇄OK`

**123 ring (12 petals):**
`123` `456` `789` `0.,` `-_/` `:;@` `'"!` `?&(` `)+=` `#$%` `*~␣` `⌫⇄OK`

Coverage: a–z, A–Z (long-press), 0–9, `. , - _ / : ; @ ' " ! ? & ( ) + = # $ % *`,
space, `~` — the full WiFi-password requirement, verified char-by-char in the
playground's headless test.

## Cost and redraw

- ≈3.5 actions/char on uniform letters (avg 2.5 rotations over 10 petals + 1 pick);
  long-press counts as 2 actions. Space: bottom slot of `yz␣` / `*~␣`.
- Focus change redraws **two wedges only** (~11% of the 480×800 screen at 10 petals);
  typing redraws the text field only; ring flip is the only full-wheel redraw.
  No animation anywhere — discrete refresh.
- ~~Hub (inner circle): ring tag + live text preview with block cursor.~~
  **Superseded 2026-08-05: the hub is empty.** The underlined text field above the
  wheel is the single preview; the active ring reads from the petals themselves
  (the utility petal's swap slot names the *other* ring). The wheel screen also
  draws no on-screen side-button hint chrome — the side rocker's pick role is
  spec'd in the table above and learned once.
- Petal char labels are sized to fit their wedge (ruled 2026-08-05): one UI font
  step below the text field's, with the column's slot spacing clamped to the
  wedge's tangential half-width at the label radius (`rm·sin(π/n)`) so the
  top/bottom chars never cross a spoke — the 12-petal 123 ring is the narrowest
  case.

## Open items

1. ~~**Column arrangement** — three mock-ups in the playground: columns in petals /
   columns + hub echo / compact petals with the column fixed in the hub.~~
   **Resolved 2026-08-05: columns in petals** — the empty-hub ruling retires both
   hub-echo variants.
2. **Space home** — bottom of `yz␣` vs Back-short = space (space is ~1 char in 5).
3. **Petal order** — alphabetical vs frequency-grouped.
4. **123-ring grouping** — inventory settled, ordering unreviewed.

## Implementation notes

- Remap surface: only front Left/Right and the side pair change roles during entry;
  Back and Confirm keep their identities. Logical buttons via `MappedInputManager`
  (`src/MappedInputManager.cpp`); activity shape reference:
  `src/activities/util/KeyboardEntryActivity.{h,cpp}`.
- Published playground artifact (same content as the HTML here):
  https://claude.ai/code/artifact/18f87963-986d-48c1-8cc0-7d71626aafe9
