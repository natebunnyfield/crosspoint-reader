# Raw measurements — Inknut Junicode step-evenness re-fit, 2026-08-28

Write-up: `docs/inknut-step-evenness-2026-08-28.md`.

| file | what it is |
|---|---|
| `arms.txt` | every `wpp1` line for all ten built `(k, ramp)` arms plus the anchor and the shipped build, both corpora. Arm keys: `ARMA` k=0.800, `ARMI` **0.805 (adopted)**, `ARMB` 0.809, `ARMG` 0.813, `ARMC` 0.818, `ARMH` 0.822 — all on `8 10 12 14 16 18`; `ARMD` 0.883 on `7 9 11 13 15 17`; `ARME` 0.905 on `7 9 11 13 14 16`; `ARMF` 0.917 on `7 9 10 12 14 16` (the "revert S" arm); `ARMJ` 0.898 on `8 10 12 14 16 18` (the arm that restores the even x-height ramp and costs 16-21 % of the anchor fit, §8). `SHIPPED` is the pre-change build, `FINAL` the rebuilt seed tree, `ANCHOR` Almendra. |
| `tier.txt` | all eight families x six slots x both corpora, off the rebuilt seed tree — the tier-level effect in §7. |
| `rendered_ramps.txt` | advanceY, x-height, cap, ascender, descender per slot for every arm, read off the built `.cpfont` headers. |
| `per_style.txt` | the same four styles side by side, before and after — the italic-vs-roman x-height column is §7's second finding. |
| `leading.txt` | the leading-floor check of §5: worst-style plain-text ink span, clearance, and the pad as a fraction of both readings of the em. |
| `search.py` | the model sweep used ONLY to shortlist: all 12,376 integer ramps x 401 values of k against the k=1 response curve in `docs/data/almendra-anchored-2026-08-27/sweep.txt`. It prints its own validation against the shipped configuration first. |
| `mkyaml.py` | writes a variant `sd-fonts.yaml` for one `(k, ramp)` — the only thing that generated the arms. |
| `cpfont_ramp.py`, `leading.py` | the two `.cpfont` header readers. |
| `sm_old.png` / `sm_new.png` / `sm_alm.png` | the S-and-M proof crops: shipped, adopted, anchor. 502x248, native device pixels, 1x. |
| `ramp_old.png` / `ramp_new.png` / `ramp_alm.png` | all six slots stacked: shipped, adopted, anchor. 502x732, native pixels, 1x. |

The renders came from `tools/calendar_preview/render_harness reading InknutJunicode`
with `CPFONT_DIR` pointed at each tree in turn, cropped to the first body line of
each page. No resampling anywhere.
