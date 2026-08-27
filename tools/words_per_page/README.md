# words_per_page — the instrument

Measures **words per full page** by running the real firmware paginator
host-side: the slim HTML parser, the CSS parser, `ParsedText`, the Liang
hyphenator, `GfxRenderer` and `SdCardFont`. The pages it counts are the pages
the device emits, not a model of them.

It exists because words per page is the metric the font ramps are normalised
against — see `docs/words-per-page-2026-08-26.md` for why (x-height and ink per
character were both tried first and both mislead), and
`docs/almendra-anchored-sizing-2026-08-27.md` for the ramp it produced.

Kept in the repo because it had already been rebuilt from scratch twice, and
the second time the scratch tree that held it was lost to a killed run. An
instrument that lives in `/tmp` gets rebuilt, not reused, and two rebuilds of
the same instrument are two chances to measure differently.

```bash
cd tools/words_per_page
./build.sh                      # SCALE=2 for the 2x tier; layout is tier-independent
./wpp1 <seed-tree> <Family> passageA.xhtml [slot]
```

`<seed-tree>` is a seed-font root such as `crosspoint-simulator/build/seedfonts`.
Omit `slot` for all six.

**Two corpora, and use both.** `passageA` and `passageB` are independent prose.
A ramp change that helps on one and hurts on the other is fitting, not fixing —
that is exactly how the Coelacanth L/XL candidate was caught and rejected. Any
conclusion drawn from one passage alone is provisional.

**A page here is a FULL page.** The chapter's sinkage page and the trailing
partial page are both dropped; the figure is a mean over the rest.

**Layout is tier-independent.** Every render scale lays out from the 1x cut, so
`wpp1` answers for 2x as well — verified bit-identical across all 48 cells.
