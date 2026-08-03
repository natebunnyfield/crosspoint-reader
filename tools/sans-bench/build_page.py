#!/usr/bin/env python3
"""Assemble the sans-serif bench page: device renders + measurements + copy."""
import base64
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
PNG = os.path.join(HERE, "png")
OUT = os.path.join(HERE, "sans-bench.html")

stats = json.load(open(os.path.join(HERE, "sans_stats.json")))
cover = json.load(open(os.path.join(HERE, "coverage.json")))
slots = json.load(open(os.path.join(HERE, "sans_slots.json")))
fontcss = open(os.path.join(HERE, "fonts.css")).read()

# harness output: family -> slot -> (lineH, alphabetW, bodyLines, cut)
measure = {}
for line in open(os.path.join(HERE, "measurements.txt")):
    m = re.match(r"(\S+)\s+slot (\d)\s+line\s+(\d+)px\s+lc-alphabet\s+(\d+)px\s+body lines\s+(\d+)(\s+\(cut\))?", line)
    if m:
        fam, slot, lh, aw, bl, cut = m.groups()
        measure.setdefault(fam, {})[int(slot)] = {
            "line": int(lh), "alpha": int(aw), "lines": int(bl), "cut": bool(cut)}

FAMS = [
    # key, display name, group, designer/origin, licence, note
    ("AtkinsonNext", "Atkinson Hyperlegible Next", "access",
     "Braille Institute with Applied Design Works, 2025", "OFL",
     "The best-known accessibility brief in type: every letterform drawn to be "
     "unmistakable for another. Seven weights, a real italic, and the 2025 "
     "revision claims 150+ languages — but the static TTFs the recipe pulls "
     "carry only 73% of Latin Extended-A, so accented European text falls "
     "back to Noto glyphs mid-word."),
    ("Luciole", "Luciole", "access",
     "Laurent Bourcellier & Jonathan Fabreguettes with CTRDV, 2019", "CC-BY 4.0",
     "The only candidate with a published reading study behind it: 145 French "
     "readers, 73 of them with low vision, against Arial, Verdana, Frutiger, "
     "Eido and OpenDyslexic. Largest x-height per em in the set (0.545), so it "
     "reaches any given apparent size at the lowest point size. CC-BY, not OFL "
     "— attribution is a condition, which the picker's designer credit "
     "already satisfies."),
    ("LexicaUltralegible", "Lexica Ultralegible", "access",
     "Jacob Perez, building on Atkinson Hyperlegible", "OFL",
     "Atkinson's letterforms with the coverage gap closed: 100% of Latin-1 and "
     "Latin Extended-A against Atkinson's 98% / 73%, plus ligatures and a "
     "slashed zero. On this device that makes it the strictly better way to "
     "get the Atkinson design. INSTALLED (S tier) since 3 August 2026, rebuilt "
     "on the tier's uniform slots at 11/13/15/17 pt with 2x hi-res companions."),
    ("Andika", "Andika", "access",
     "SIL International, literacy brief", "OFL",
     "Drawn for new readers: one unambiguous form per letter, wide open "
     "counters, and the broadest coverage here — 100% Latin-1, 99% Latin "
     "Extended-A, 98% of the punctuation block, so almost nothing falls back "
     "to Noto. Pays for it in width: second-widest set in the bench."),
    ("SourceSans3", "Source Sans 3", "humanist",
     "Paul D. Hunt for Adobe, 2012–2021", "OFL",
     "The efficient choice. Narrower than every serif CrossPoint ships while "
     "holding the same x-height, with open apertures, a real drawn italic, and "
     "leading that lands exactly on 33/39/45/51. Nothing about it is "
     "distinctive, which for 300 pages of body text is the argument for it."),
    ("FiraSans", "Fira Sans", "humanist",
     "Erik Spiekermann & Carrois Type Design, 2013", "OFL",
     "The only candidate drawn against this exact problem — small sizes on "
     "cheap, low-resolution screens, for Firefox OS. Sturdy stems and firm "
     "terminals are what survive a hard 1-bit threshold; hairlines are what "
     "break up. Costs width, and its size ramp is the least even in the bench "
     "(35/38/44/51 against a 34/40/46/51 target)."),
    ("IBMPlexSans", "IBM Plex Sans", "humanist",
     "Mike Abbink & Bold Monday, 2017", "OFL",
     "A neo-grotesque skeleton with humanist italics. Clean and even on the "
     "panel, but its Latin-1 forms are the only thing it does better than "
     "Source Sans here, and it sets 5% wider."),
    ("AlegreyaSans", "Alegreya Sans", "humanist",
     "Juan Pablo del Peral, Huerta Tipográfica, 2013", "OFL",
     "The literary sans: drawn as the companion to a book serif, and by a wide "
     "margin the narrowest here — 266px where Edgar needs 335px for the same "
     "alphabet at the same x-height. That is roughly a quarter more text per "
     "line. The catch is weight: its stems are the lightest in the bench, and "
     "light stems are exactly what a 1-bit threshold renders unevenly."),
    ("OpenSans", "Open Sans", "humanist",
     "Steve Matteson, 2011", "OFL",
     "Large x-height, unremarkable everywhere else. Here mostly as the "
     "familiar humanist baseline — if a candidate cannot beat Open Sans on "
     "this panel it has no argument."),
    ("PublicSans", "Public Sans", "humanist",
     "US Web Design System, from Libre Franklin", "OFL",
     "A plain-language brief rather than a legibility one. Widest set in the "
     "bench and the thinnest punctuation coverage; the specimen is the only "
     "case for it."),
    ("PTSans", "PT Sans", "humanist",
     "ParaType (Korolkova, Yefimov, Matveeva), 2009", "OFL",
     "Compact and even — third-narrowest — and drawn for a public-signage "
     "programme, so the letterforms are conservative in a useful way. Latin "
     "Extended-A is only 78% covered, which is the reason it is not higher."),
    ("Inter", "Inter", "control",
     "Rasmus Andersson, 2016–", "OFL",
     "CONTROL, not a proposal. The default screen-UI sans of the last decade, "
     "so it is the texture most readers will recognise. It reads as interface "
     "rather than book — tight spacing, closed apertures, uniform rhythm."),
    ("NotoSans", "Noto Sans", "control",
     "Monotype for Google", "OFL",
     "CONTROL — the family CrossPoint already carries in flash, so it is what "
     "any candidate has to beat. Broadest coverage of anything here and "
     "perfectly competent; it was drawn to be neutral across 800 scripts, not "
     "to be read for six hours."),
]

REFS = [
    ("Edgar", "Edgar", "Tobias Frere-Jones & Nina Stössinger, 2025",
     "Installed today. Caslon-adjacent text serif, S tier."),
    ("TeXGyreSchola", "TeX Gyre Schola", "GUST, after Century Schoolbook",
     "Installed today. The widest-setting face CrossPoint ships."),
]

PAID = [
    ("Lucida Sans / Lucida Grande", "Bigelow &amp; Holmes", "lucidafonts.com",
     "The strongest theoretical fit in existence: designed from 1984 onward "
     "specifically for low-resolution printers and displays — large x-height "
     "(~53% of body), open counters, simplified forms chosen to survive coarse "
     "rasterisation. Everything this bench measures, Lucida was drawn to win.",
     "Desktop licence; conversion to bitmaps for your own devices is the "
     "normal reading, redistribution is not."),
    ("Neue Frutiger", "Monotype / Linotype", "linotype.com",
     "The wayfinding legibility benchmark, and one of the five comparison "
     "faces in the Luciole low-vision study. Open apertures, unambiguous "
     "forms, enormous language coverage.",
     "Monotype EULA; embedding tiers are sold separately."),
    ("Skolar Sans PE", "Rosetta", "rosettatype.com",
     "Built for dense editorial text and data — large x-height, sturdy joins, "
     "and a Pan-European cut whose Latin Extended coverage would remove the "
     "Noto-fallback problem that limits Atkinson and PT Sans here.",
     "Per-style desktop licence; ask Rosetta directly about device embedding."),
    ("Adelle Sans", "TypeTogether", "type-together.com",
     "A book-and-magazine sans with real italics from a foundry whose whole "
     "catalogue is aimed at long-form reading. The closest paid analogue to "
     "what Alegreya Sans attempts for free, with the weight to survive 1-bit.",
     "Desktop licence; TypeTogether sells app/device embedding separately."),
    ("Fedra Sans", "Typotheque", "typotheque.com",
     "Explicitly drawn to hold up under bad screen rendering — the brief was a "
     "corporate face that would not fall apart on low-resolution output.",
     "Typotheque licences are per-use; device embedding is a named tier."),
    ("Whitney", "Hoefler &amp; Co.", "typography.com",
     "Drawn for museum signage and small-size text at once: narrow enough to "
     "be economical, open enough to stay legible small.",
     "H&amp;Co desktop licence; embedding requires a separate agreement."),
    ("Benton Sans", "Font Bureau", "fontbureau.com",
     "News Gothic rebuilt for screen text — the newspaper answer to the same "
     "question this bench asks.",
     "Desktop licence."),
    ("FF Meta", "Monotype (FontFont)", "fontshop.com",
     "Spiekermann's small-size legibility classic, drawn for the German post "
     "office because Helvetica failed at small sizes on bad paper. Fira Sans "
     "is its descendant, which is a reason to try Fira first.",
     "Monotype EULA."),
    ("Verdana / Tahoma", "Matthew Carter, via Monotype", "monotype.com",
     "The bilevel-screen champions: hand-hinted for the pixel grid, wide, "
     "unambiguous. Verdana is arguably still the most legible face ever made "
     "for a low-resolution raster. Very wide, so pages get longer.",
     "Bundled with operating systems but NOT licensed for redistribution or "
     "embedding — this is the trap. A separate Monotype licence is required."),
    ("Amazon Ember", "Amazon", "not licensable",
     "The Kindle's own sans. Listed only to close the question: Amazon does "
     "not license it, and Bookerly was pulled from this project's font list "
     "for exactly that reason.",
     "Cannot be licensed. Do not ship it."),
    ("APHont", "American Printing House for the Blind", "aph.org",
     "Free, drawn for low-vision readers, and genuinely good at it — but the "
     "licence restricts redistribution, so it behaves like a paid face for "
     "this project's purposes.",
     "Free download, restricted redistribution."),
]

SOURCES = [
    ("Luciole, a new font for people with low vision — Acta Psychologica (2023)",
     "https://www.sciencedirect.com/science/article/pii/S0001691823001026"),
    ("Braille Institute — Atkinson Hyperlegible Next launch (2025)",
     "https://www.brailleinstitute.org/about-us/news/braille-institute-launches-enhanced-atkinson-hyperlegible-font-to-make-reading-easier/"),
    ("Atkinson Hyperlegible Next Expands on Low Vision Accessibility — PRINT Magazine",
     "https://www.printmag.com/type-tuesday/atkinson-hyperlegible-next-applied-design/"),
    ("Typeface features and legibility research — Vision Research (2019)",
     "https://www.sciencedirect.com/science/article/pii/S0042698919301087"),
    ("Bigelow &amp; Holmes — How and Why We Designed Lucida",
     "https://lucidafonts.com/blogs/bigelow-holmes-blog/how-and-why-we-designed-lucida"),
    ("Notes on Lucida designs — TUG",
     "https://tug.org/store/lucida/designnotes.html"),
    ("Lexica Ultralegible — project page",
     "https://jacobxperez.github.io/lexica-ultralegible/"),
    ("Rosetta — On Skolar Sans",
     "https://rosettatype.com/articles/2014-11-26-on-skolar-sans/"),
    ("CrossPoint upstream — “Fonts: All The Things” discussion #1693",
     "https://github.com/crosspoint-reader/crosspoint-reader/discussions/1693"),
]


def img_uri(fam, slot):
    p = os.path.join(PNG, f"reading_{fam}_{slot}.png")
    return "data:image/png;base64," + base64.b64encode(open(p, "rb").read()).decode()


def build():
    specimens = {}
    for key, *_ in FAMS:
        specimens[key] = [img_uri(key, s) for s in range(4)]
    for key, *_ in REFS:
        specimens[key] = [img_uri(key, s) for s in range(4)]

    # ── nav rail + panels ────────────────────────────────────────────────
    rail, panels = [], []
    groups = [("access", "Drawn for legibility"), ("humanist", "Humanist text sans"),
              ("control", "Controls")]
    for gid, glabel in groups:
        rail.append(f'<li class="rail-head">{glabel}</li>')
        for key, name, group, origin, lic, note in FAMS:
            if group != gid:
                continue
            m0, m2 = measure[key][0], measure[key][2]
            rail.append(
                f'<li><button class="fam" data-fam="{key}" role="tab" aria-selected="false">'
                f'<span class="fam-name" style="font-family:\'S{key}\',serif">{name}</span>'
                f'<span class="fam-num">{m0["alpha"]}<span class="u">px</span></span></button></li>')
    rail.append('<li class="rail-head">Installed today</li>')
    for key, name, origin, note in REFS:
        m0 = measure[key][0]
        rail.append(
            f'<li><button class="fam ref" data-fam="{key}" role="tab" aria-selected="false">'
            f'<span class="fam-name">{name}</span>'
            f'<span class="fam-num">{m0["alpha"]}<span class="u">px</span></span></button></li>')

    meta = {}
    for key, name, group, origin, lic, note in FAMS:
        meta[key] = {"name": name, "origin": origin, "lic": lic, "note": note,
                     "sizes": slots[key]["sizes"],
                     "x": stats[key]["x_height_em"], "n": stats[key]["n_width_em"],
                     "cov": cover[key],
                     "m": measure[key], "ref": False}
    for key, name, origin, note in REFS:
        meta[key] = {"name": name, "origin": origin, "lic": "installed",
                     "note": note, "sizes": None, "x": None, "n": None, "cov": None,
                     "m": measure[key], "ref": True}

    # ── measurement table ────────────────────────────────────────────────
    rows = []
    ordered = sorted([k for k, *_ in FAMS], key=lambda k: measure[k][0]["alpha"])
    for key in ordered + [k for k, *_ in REFS]:
        d = meta[key]
        sizes = "/".join(str(s) for s in d["sizes"]) if d["sizes"] else "—"
        x = f'{d["x"]:.3f}' if d["x"] else "—"
        n = f'{d["n"]:.3f}' if d["n"] else "—"
        c1 = f'{d["cov"]["latin1"]:.0f}' if d["cov"] else "—"
        cA = f'{d["cov"]["latinA"]:.0f}' if d["cov"] else "—"
        cP = f'{d["cov"]["punct"]:.0f}' if d["cov"] else "—"
        cls = ' class="is-ref"' if d["ref"] else ""
        warnA = " low" if d["cov"] and d["cov"]["latinA"] < 90 else ""
        rows.append(
            f'<tr{cls}><th scope="row">{d["name"]}</th>'
            f'<td class="num">{x}</td><td class="num">{n}</td>'
            f'<td class="num strong">{measure[key][0]["alpha"]}</td>'
            f'<td class="num">{measure[key][2]["alpha"]}</td>'
            f'<td class="num">{sizes}</td>'
            f'<td class="num">{c1}</td><td class="num{warnA}">{cA}</td><td class="num">{cP}</td>'
            f'<td>{d["lic"]}</td></tr>')

    paid_rows = "".join(
        f'<tr><th scope="row">{n}<span class="foundry">{f}</span></th>'
        f'<td>{why}</td><td class="risk">{risk}</td></tr>'
        for n, f, _url, why, risk in PAID)

    src_items = "".join(f'<li><a href="{u}">{t}</a></li>' for t, u in SOURCES)

    html = TEMPLATE.format(
        fontcss=fontcss,
        rail="".join(rail),
        rows="".join(rows),
        paid_rows=paid_rows,
        sources=src_items,
        specimens=json.dumps(specimens),
        meta=json.dumps(meta),
    )
    open(OUT, "w").write(html)
    print(f"wrote {OUT}  ({os.path.getsize(OUT)/1024:.0f} KB)")


TEMPLATE = r"""<meta charset="utf-8">
<title>Sans-serif bench — CrossPoint Reader</title>
<style>
{fontcss}

:root {{
  color-scheme: light dark;
  /* Neutrals biased toward the panel's own warm grey-green, not pure grey. */
  --paper:      #FBFAF6;
  --paper-2:    #F2F1EA;
  --rule:       #DCDACE;
  --ink:        #1B1C19;
  --ink-2:      #55574D;
  --ink-3:      #85877A;
  --panel:      #E9E9E3;   /* measured off the Xteink panel white */
  --panel-ink:  #17181A;
  --accent:     #A8321C;   /* proof-mark oxide red */
  --accent-dim: #C9836F;
  --shadow: 0 1px 0 rgba(27,28,25,.05), 0 12px 32px -18px rgba(27,28,25,.45);
}}
@media (prefers-color-scheme: dark) {{
  :root {{
    --paper:   #14150F;  --paper-2: #1C1E16;  --rule: #33362B;
    --ink:     #E8E7DA;  --ink-2:   #A9AB9B;  --ink-3: #74776A;
    --accent:  #E8735A;  --accent-dim: #8E4436;
    --shadow: 0 1px 0 rgba(0,0,0,.4), 0 16px 40px -20px rgba(0,0,0,.8);
  }}
}}
:root[data-theme="dark"] {{
  --paper: #14150F; --paper-2: #1C1E16; --rule: #33362B;
  --ink: #E8E7DA; --ink-2: #A9AB9B; --ink-3: #74776A;
  --accent: #E8735A; --accent-dim: #8E4436;
  --shadow: 0 1px 0 rgba(0,0,0,.4), 0 16px 40px -20px rgba(0,0,0,.8);
}}
:root[data-theme="light"] {{
  --paper: #FBFAF6; --paper-2: #F2F1EA; --rule: #DCDACE;
  --ink: #1B1C19; --ink-2: #55574D; --ink-3: #85877A;
  --accent: #A8321C; --accent-dim: #C9836F;
  --shadow: 0 1px 0 rgba(27,28,25,.05), 0 12px 32px -18px rgba(27,28,25,.45);
}}

* {{ box-sizing: border-box; }}
body {{
  margin: 0;
  background: var(--paper);
  color: var(--ink);
  font-family: 'ChromeText', Georgia, serif;
  font-size: 17px;
  line-height: 1.62;
  -webkit-text-size-adjust: 100%;
}}
.wrap {{ max-width: 1180px; margin: 0 auto; padding: 0 24px 96px; }}
h1, h2, h3 {{ font-weight: 600; text-wrap: balance; line-height: 1.2; margin: 0; }}
p {{ margin: 0; }}
a {{ color: var(--accent); text-decoration-thickness: 1px; text-underline-offset: 2px; }}
.mono {{ font-family: 'ChromeMono', ui-monospace, monospace; }}

.eyebrow {{
  font-family: 'ChromeMono', monospace;
  font-size: 11px; letter-spacing: .14em; text-transform: uppercase;
  color: var(--ink-3);
}}

/* ── masthead ───────────────────────────────────────────────────── */
header.top {{
  border-bottom: 2px solid var(--ink);
  padding: 56px 0 22px;
  display: flex; flex-direction: column; gap: 18px;
}}
header.top h1 {{ font-size: clamp(34px, 6vw, 58px); letter-spacing: -.015em; }}
header.top h1 em {{ font-style: normal; color: var(--accent); }}
.standfirst {{ font-size: clamp(18px, 2.2vw, 21px); color: var(--ink-2); max-width: 62ch; }}
.byline {{ display: flex; flex-wrap: wrap; gap: 8px 22px; }}

/* ── constraint callout ─────────────────────────────────────────── */
.constraint {{
  margin-top: 40px;
  display: grid; grid-template-columns: minmax(0,1.1fr) minmax(0,1fr);
  gap: 28px; align-items: start;
  padding: 26px; background: var(--paper-2);
  border: 1px solid var(--rule); border-left: 3px solid var(--accent);
}}
.constraint h2 {{ font-size: 21px; margin-bottom: 10px; }}
.constraint p + p {{ margin-top: 12px; }}
pre.code {{
  margin: 0; padding: 14px 16px; overflow-x: auto;
  background: var(--paper); border: 1px solid var(--rule);
  font-family: 'ChromeMono', monospace; font-size: 12.5px; line-height: 1.7;
  color: var(--ink-2);
}}
pre.code b {{ color: var(--accent); font-weight: 400; }}
.caption {{ font-size: 13px; color: var(--ink-3); margin-top: 10px; }}

section {{ margin-top: 72px; }}
section > h2 {{
  font-size: clamp(24px, 3.4vw, 32px); letter-spacing: -.01em;
  padding-bottom: 12px; border-bottom: 1px solid var(--ink);
}}
.lede {{ margin-top: 18px; max-width: 68ch; color: var(--ink-2); }}

/* ── the bench ──────────────────────────────────────────────────── */
.bench {{
  margin-top: 28px;
  display: grid; grid-template-columns: 260px minmax(0, 1fr);
  gap: 32px; align-items: start;
}}
.rail {{ position: sticky; top: 16px; }}
.rail ul {{ list-style: none; margin: 0; padding: 0; }}
.rail-head {{
  font-family: 'ChromeMono', monospace; font-size: 10.5px;
  letter-spacing: .13em; text-transform: uppercase; color: var(--ink-3);
  padding: 18px 0 7px; border-bottom: 1px solid var(--rule); margin-bottom: 4px;
}}
.rail-head:first-child {{ padding-top: 0; }}
button.fam {{
  width: 100%; display: flex; align-items: baseline; justify-content: space-between;
  gap: 10px; padding: 7px 9px; background: none; border: 0; border-radius: 2px;
  color: var(--ink); cursor: pointer; text-align: left; font: inherit;
}}
button.fam:hover {{ background: var(--paper-2); }}
button.fam:focus-visible {{ outline: 2px solid var(--accent); outline-offset: 1px; }}
button.fam[aria-selected="true"] {{ background: var(--ink); color: var(--paper); }}
button.fam[aria-selected="true"] .fam-num {{ color: var(--paper); opacity: .75; }}
.fam-name {{ font-size: 17px; line-height: 1.25; }}
button.fam.ref .fam-name {{ font-family: 'ChromeText', serif; font-style: italic; color: var(--ink-2); }}
button.fam.ref[aria-selected="true"] .fam-name {{ color: var(--paper); }}
.fam-num {{
  font-family: 'ChromeMono', monospace; font-size: 11px;
  font-variant-numeric: tabular-nums; color: var(--ink-3); flex: none;
}}
.fam-num .u {{ font-size: 9px; opacity: .7; margin-left: 1px; }}

.stage {{ display: grid; gap: 20px; }}
.stage-head {{ display: flex; flex-wrap: wrap; gap: 16px 24px; align-items: flex-end; justify-content: space-between; }}
.stage-title {{ font-size: 26px; letter-spacing: -.01em; }}
.slotbar {{ display: flex; gap: 0; border: 1px solid var(--rule); }}
.slotbar button {{
  font-family: 'ChromeMono', monospace; font-size: 11px; letter-spacing: .06em;
  padding: 8px 13px; background: var(--paper); border: 0; border-right: 1px solid var(--rule);
  color: var(--ink-2); cursor: pointer;
}}
.slotbar button:last-child {{ border-right: 0; }}
.slotbar button[aria-pressed="true"] {{ background: var(--ink); color: var(--paper); }}
.slotbar button:focus-visible {{ outline: 2px solid var(--accent); outline-offset: -2px; }}

.viewer {{ display: grid; grid-template-columns: minmax(0,auto) minmax(0,1fr); gap: 30px; align-items: start; }}
.device {{
  background: var(--panel); padding: 10px; border: 1px solid var(--rule);
  box-shadow: var(--shadow); max-width: 100%;
}}
.device img {{
  display: block; width: 528px; max-width: 100%; height: auto;
  image-rendering: pixelated;
}}
.device-note {{
  font-family: 'ChromeMono', monospace; font-size: 10px; letter-spacing: .1em;
  text-transform: uppercase; color: var(--ink-3); margin-top: 9px;
}}
.readout dl {{ margin: 0; display: grid; grid-template-columns: auto 1fr; gap: 5px 16px; }}
.readout dt {{
  font-family: 'ChromeMono', monospace; font-size: 10.5px; letter-spacing: .08em;
  text-transform: uppercase; color: var(--ink-3); padding-top: 3px;
}}
.readout dd {{ margin: 0; font-family: 'ChromeMono', monospace; font-size: 13px; font-variant-numeric: tabular-nums; }}
.readout .note {{ margin-top: 20px; color: var(--ink-2); font-size: 15.5px; }}
.origin {{ font-size: 13px; color: var(--ink-3); margin-top: 4px; }}
.live {{ margin-top: 22px; padding-top: 16px; border-top: 1px solid var(--rule); }}
.live-line {{ font-size: 27px; line-height: 1.35; margin-top: 6px; }}

/* ── tables ─────────────────────────────────────────────────────── */
.scroller {{ overflow-x: auto; margin-top: 24px; }}
table {{ border-collapse: collapse; width: 100%; font-size: 14px; }}
caption {{ text-align: left; color: var(--ink-3); font-size: 13px; padding-bottom: 10px; }}
th, td {{ padding: 8px 12px; text-align: left; vertical-align: top; border-bottom: 1px solid var(--rule); }}
thead th {{
  font-family: 'ChromeMono', monospace; font-size: 10px; letter-spacing: .09em;
  text-transform: uppercase; color: var(--ink-3); font-weight: 400;
  border-bottom: 1px solid var(--ink); white-space: nowrap;
}}
tbody th {{ font-weight: 600; white-space: nowrap; }}
td.num {{ font-family: 'ChromeMono', monospace; font-variant-numeric: tabular-nums; text-align: right; white-space: nowrap; }}
td.strong {{ color: var(--ink); font-weight: 600; }}
td.low {{ color: var(--accent); }}
tr.is-ref th, tr.is-ref td {{ color: var(--ink-3); font-style: italic; }}
.foundry {{ display: block; font-weight: 400; font-size: 12px; color: var(--ink-3); font-style: normal; }}
td.risk {{ color: var(--ink-2); font-size: 13px; }}
table.paid th[scope="row"] {{ width: 22%; }}

/* ── verdict cards ──────────────────────────────────────────────── */
.picks {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 20px; margin-top: 26px; }}
.pick {{ border: 1px solid var(--rule); padding: 22px; background: var(--paper-2); }}
.pick.first {{ border-color: var(--accent); border-top: 3px solid var(--accent); }}
.pick .rank {{ font-family: 'ChromeMono', monospace; font-size: 10.5px; letter-spacing: .12em; text-transform: uppercase; color: var(--accent); }}
.pick h3 {{ font-size: 21px; margin: 8px 0 10px; }}
.pick p {{ font-size: 15px; color: var(--ink-2); }}
.pick p + p {{ margin-top: 10px; }}

ol.steps {{ margin: 20px 0 0; padding-left: 22px; }}
ol.steps li {{ margin-bottom: 10px; }}
ol.steps code, p code, li code {{
  font-family: 'ChromeMono', monospace; font-size: 12.5px;
  background: var(--paper-2); padding: 1px 5px; border: 1px solid var(--rule);
}}
ul.sources {{ margin: 18px 0 0; padding-left: 20px; font-size: 14.5px; color: var(--ink-2); }}
ul.sources li {{ margin-bottom: 7px; }}
footer {{ margin-top: 72px; padding-top: 22px; border-top: 1px solid var(--rule); color: var(--ink-3); font-size: 13px; }}

@media (max-width: 1180px) {{
  .viewer {{ grid-template-columns: 1fr; }}
  .device {{ justify-self: start; }}
  .readout .note {{ max-width: 62ch; }}
}}
@media (max-width: 900px) {{
  .constraint {{ grid-template-columns: 1fr; }}
  .bench {{ grid-template-columns: 1fr; }}
  .rail {{ position: static; }}
  .rail ul {{ display: grid; grid-template-columns: repeat(auto-fill, minmax(150px, 1fr)); gap: 2px 10px; }}
  .rail-head {{ grid-column: 1 / -1; }}
  .viewer {{ grid-template-columns: 1fr; }}
}}
@media (prefers-reduced-motion: reduce) {{ * {{ transition: none !important; }} }}
</style>

<div class="wrap">

<header class="top">
  <span class="eyebrow">CrossPoint Reader &middot; typeface evaluation &middot; 3 August 2026</span>
  <h1>Every sans on this page was rendered <em>by the reader itself</em>.</h1>
  <p class="standfirst">Fifteen faces, built to <code>.cpfont</code>, loaded through the real
    <span class="mono">SdCardFont</span> path and drawn by the real <span class="mono">GfxRenderer</span>
    into the device's 528&times;792 one-bit framebuffer. Same x-height, same leading, same column.
    The only thing that changes between two specimens is the typeface.</p>
  <div class="byline eyebrow">
    <span>13 candidates + 2 installed serifs</span>
    <span>x-height 12/14/16/18&thinsp;px</span>
    <span>bench: lib/EpdFont/scripts/sans-candidates.yaml</span>
  </div>
</header>

<div class="constraint">
  <div>
    <h2>Why a sans is worth testing at all</h2>
    <p>The panel has no grey. A glyph is rasterised by FreeType with antialiasing,
      stored in the <code>.cpfont</code> at two bits per pixel, and then, in the reader's
      normal black-and-white mode, <strong>every pixel carrying any ink at all is painted
      solid black</strong>. Partial coverage does not become grey; it becomes a full pixel.</p>
    <p>That single line of code is the whole argument. Hairlines fatten to full stems, tight
      counters fill in, and delicate serif brackets turn into blobs — while sturdy, open,
      low-contrast letterforms come through nearly intact. It is also why comparing faces at
      the same <em>point size</em> would be meaningless: what matters is what survives at a
      given number of pixels.</p>
  </div>
  <div>
<pre class="code">// GfxRenderer.cpp:506
if (renderMode == GfxRenderer::BW &amp;&amp; bmpVal &lt; 3) {{
  <b>// Black (also paints over the grays in BW mode)</b>
  plotGlyphPixel(renderer, screenX, screenY, pixelState);
}}</pre>
    <p class="caption">Every candidate below was therefore built to a measured
      x-height of 12/14/16/18&nbsp;px and a leading of 34/40/46/51&nbsp;px — the same uniform
      slots the curated serif tier already uses — so no face gets an unfair size advantage.</p>
  </div>
</div>

<section>
  <h2>The bench</h2>
  <p class="lede">Pick a face on the left and a size slot on the right. The number beside each
    name is how many pixels its lowercase alphabet occupies at slot&nbsp;0 — the set width, and
    the single best predictor of how much text fits on a page.</p>

  <div class="bench">
    <nav class="rail" aria-label="Typefaces"><ul role="tablist">{rail}</ul></nav>

    <div class="stage">
      <div class="stage-head">
        <div>
          <h3 class="stage-title" id="specName">—</h3>
          <p class="origin" id="specOrigin">—</p>
        </div>
        <div class="slotbar" role="group" aria-label="Size slot">
          <button data-slot="0" aria-pressed="true">12&thinsp;px</button>
          <button data-slot="1" aria-pressed="false">14&thinsp;px</button>
          <button data-slot="2" aria-pressed="false">16&thinsp;px</button>
          <button data-slot="3" aria-pressed="false">18&thinsp;px</button>
        </div>
      </div>

      <div class="viewer">
        <figure class="device" style="margin:0">
          <img id="specImg" alt="Device render of a reading page" width="528" height="792">
          <figcaption class="device-note">528 &times; 792 &middot; 1-bit &middot; unretouched framebuffer</figcaption>
        </figure>
        <div class="readout">
          <dl>
            <dt>Point size</dt><dd id="rPt">—</dd>
            <dt>Line height</dt><dd id="rLine">—</dd>
            <dt>Set width</dt><dd id="rSet">—</dd>
            <dt>x-height</dt><dd id="rX">—</dd>
            <dt>Latin-1 / Ext-A</dt><dd id="rCov">—</dd>
            <dt>Licence</dt><dd id="rLic">—</dd>
          </dl>
          <p class="note" id="rNote">—</p>
          <div class="live">
            <span class="eyebrow">The outline, before the panel gets it</span>
            <p class="live-line" id="rLive">Handgloves &amp; fjord 148</p>
          </div>
        </div>
      </div>
    </div>
  </div>
</section>

<section>
  <h2>Measurements</h2>
  <p class="lede">Sorted by set width, narrowest first. Set width is measured on the built
    <code>.cpfont</code> through the firmware's own <span class="mono">getTextWidth()</span>,
    kerning resident — not estimated from the source outlines. Coverage is the share of each
    Unicode block the face draws itself; whatever it misses is silently filled from Noto Sans
    at build time, which is how a family ends up with two textures in one word.</p>
  <div class="scroller">
    <table>
      <caption>x/em and n-width are source metrics; set width, in pixels, is the rendered
        lowercase alphabet at matched x-height. The last two rows are the serifs installed today.</caption>
      <thead>
        <tr>
          <th scope="col">Typeface</th>
          <th scope="col">x/em</th>
          <th scope="col">n&nbsp;width</th>
          <th scope="col">Set 12px</th>
          <th scope="col">Set 16px</th>
          <th scope="col">Point sizes</th>
          <th scope="col">Latin-1 %</th>
          <th scope="col">Ext-A %</th>
          <th scope="col">Punct %</th>
          <th scope="col">Licence</th>
        </tr>
      </thead>
      <tbody>{rows}</tbody>
    </table>
  </div>
</section>

<section>
  <h2>What I would build first</h2>
  <p class="lede"><strong>Ruled and shipped:</strong> Lexica Ultralegible was promoted to the
    installed S tier on 3 August 2026 — rebuilt on the tier's uniform slots (11/13/15/17&nbsp;pt,
    x-height 12/14/16/18&nbsp;px, leading 33/39/45/51), given its 2x hi-res companions, and
    installed to the simulator's <code>fs_/fonts/</code> and the iOS seed bundle. The simulator
    reports <span class="mono">5 families discovered</span> and the picker lists it first.
    The two below stay on the bench.</p>
  <div class="picks">
    <div class="pick first">
      <span class="rank">Build first</span>
      <h3>Source Sans 3</h3>
      <p>The efficiency argument. At a matched 12&thinsp;px x-height it sets the alphabet in
        285&thinsp;px against Edgar's 335 — about 17% more text per line, on a device where a
        page turn costs a full panel refresh. Real italic, exact 33/39/45/51 leading, 100% of
        Latin-1 and Latin Extended-A so nothing falls back to Noto.</p>
      <p>It is also the least opinionated face in the bench, which over three hundred pages is
        the point rather than a criticism.</p>
    </div>
    <div class="pick">
      <span class="rank">Build second</span>
      <h3>Fira Sans</h3>
      <p>The only candidate drawn against precisely this problem: small text on cheap
        low-resolution screens. Its stems are heavy enough to survive the threshold intact
        where lighter faces go ragged, and its large x-height (0.527&thinsp;em) buys apparent
        size cheaply.</p>
      <p>Costs 6% more width than Source Sans, and its size ramp is the least even here —
        35/38/44/51 against the 34/40/46/51 target.</p>
    </div>
    <div class="pick first">
      <span class="rank">Shipped &mdash; S tier, 3 Aug 2026</span>
      <h3>Lexica Ultralegible</h3>
      <p>If an accessibility face belongs on the card, this is the one to build rather than
        Atkinson Hyperlegible Next itself: identical design intent, but 100% of Latin-1 and
        Latin Extended-A against Atkinson's 98% and 73%, so European accents stay in the face
        instead of switching to Noto mid-word.</p>
      <p>Luciole is the serious alternative and the only face here with a published low-vision
        reading study — take it if evidence outranks coverage.</p>
    </div>
  </div>
  <ol class="steps">
    <li>Build the bench: <code>python3 lib/EpdFont/scripts/build-sd-fonts.py --config lib/EpdFont/scripts/sans-candidates.yaml --output-dir out/</code></li>
    <li>Re-render any specimen: <code>CPFONT_DIR=/fonts ./render_harness reading SourceSans3</code> from <code>tools/calendar_preview</code></li>
    <li><code>installed_families:</code> in <code>sd-fonts.yaml</code> now reads Edgar, Coelacanth,
      Rosarivo, TeX&nbsp;Gyre&nbsp;Schola, LexicaUltralegible. The other twelve candidates here are
      still installed nowhere. Physical device SD cards are the one surface a script cannot
      reach — copy the family plus its <code>2x/</code> directory by hand.</li>
  </ol>
</section>

<section>
  <h2>Paid faces worth the licence</h2>
  <p class="lede">Read this first: converting a licensed font into <code>.cpfont</code> bitmaps and
    putting them on your own SD card is normally within a desktop licence; <strong>redistributing
    those bitmaps is not</strong>. That is already how this fork treats Edgar, GT Alpina, Venetian 301
    and Caledonia CC — the recipe is committed, the font files never are. Any face below would
    ship the same way: buildable by whoever holds the licence, absent for everyone else.</p>
  <div class="scroller">
    <table class="paid">
      <thead>
        <tr><th scope="col">Typeface</th><th scope="col">Why it is on this list</th><th scope="col">Licensing reality</th></tr>
      </thead>
      <tbody>{paid_rows}</tbody>
    </table>
  </div>
</section>

<section>
  <h2>How the specimens were made</h2>
  <p class="lede">Every number and every pixel on this page came out of the repository, not out
    of a browser. The one thing a browser did render is the single large line in the readout,
    which is there precisely to show the gap between the outline a designer drew and what a
    one-bit panel returns.</p>
  <ol class="steps">
    <li>Each family's hinted x-height was measured at every point size from 7 to 29 at 150&nbsp;DPI
      — the converter's own resolution — and the four sizes closest to 12/14/16/18&nbsp;px chosen.
      Hinting moves x-height by up to 3&nbsp;px off the source-unit estimate, so this is measured,
      never derived.</li>
    <li>A single ascent/descent span per family was solved for the 34/40/46/51&nbsp;px leading
      targets, floored by the worst-of-regular-and-italic plain-text ink span plus 0.13&nbsp;em so
      lines cannot overlap, with the ascender clearing accented capitals. Verified for all 13.</li>
    <li><code>build-sd-fonts.py</code> patched those metrics onto a cached copy of each source and
      converted all four styles at the four sizes.</li>
    <li><code>render_harness reading FAMILY</code> — a new mode added for this evaluation — wrapped
      the same passage against the firmware's <span class="mono">getTextWidth()</span> with kerning
      prewarmed, drew it with <span class="mono">GfxRenderer</span>, and dumped the framebuffer.</li>
  </ol>
  <h3 style="margin-top:34px;font-size:19px">Sources</h3>
  <ul class="sources">{sources}</ul>
</section>

<footer>
  Bench definition: <span class="mono">lib/EpdFont/scripts/sans-candidates.yaml</span> &middot;
  specimen mode: <span class="mono">tools/calendar_preview/render_harness.cpp</span> &middot;
  branch <span class="mono">claude/readable-sans-serif-test-d58603</span>.
  Lexica Ultralegible shipped to the installed S tier on 3 August 2026; every other
  candidate here is installed nowhere.
</footer>
</div>

<script>
const SPEC = {specimens};
const META = {meta};
let fam = 'SourceSans3', slot = 0;

const $ = (id) => document.getElementById(id);

function render() {{
  const d = META[fam];
  $('specImg').src = SPEC[fam][slot];
  $('specImg').alt = d.name + ' rendered on the device at size slot ' + slot;
  $('specName').textContent = d.name;
  $('specName').style.fontFamily = d.ref ? '' : "'S" + fam + "', serif";
  $('specOrigin').textContent = d.origin;
  $('rPt').textContent = d.sizes ? d.sizes[slot] + ' pt' : '—';
  $('rLine').textContent = d.m[slot].line + ' px';
  $('rSet').textContent = d.m[slot].alpha + ' px';
  $('rX').textContent = d.x ? d.x.toFixed(3) + ' em' : '—';
  $('rCov').textContent = d.cov ? d.cov.latin1.toFixed(0) + '% / ' + d.cov.latinA.toFixed(0) + '%' : '—';
  $('rLic').textContent = d.lic;
  $('rNote').textContent = d.note;
  $('rLive').style.fontFamily = d.ref ? "'ChromeText', serif" : "'S" + fam + "', serif";
  document.querySelectorAll('button.fam').forEach((b) =>
    b.setAttribute('aria-selected', String(b.dataset.fam === fam)));
  document.querySelectorAll('.slotbar button').forEach((b) =>
    b.setAttribute('aria-pressed', String(Number(b.dataset.slot) === slot)));
}}

document.querySelectorAll('button.fam').forEach((b) =>
  b.addEventListener('click', () => {{ fam = b.dataset.fam; render(); }}));
document.querySelectorAll('.slotbar button').forEach((b) =>
  b.addEventListener('click', () => {{ slot = Number(b.dataset.slot); render(); }}));

render();
</script>
"""

if __name__ == "__main__":
    build()
