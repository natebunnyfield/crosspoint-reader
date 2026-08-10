#!/usr/bin/env python3
"""Assemble the text-grotesque bench page."""
import base64
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
PNG = os.path.join(HERE, "png")
OUT = os.path.join(HERE, "grotesque-bench.html")

slots = json.load(open(os.path.join(HERE, "grot_slots.json")))
ctrl = json.load(open(os.path.join(HERE, "grot_controls.json")))
fontcss = open(os.path.join(HERE, "fonts.css")).read()

measure = {}
for src in (os.path.join(HERE, "measurements.txt"),
            os.path.join(HERE, "..", "measurements.txt")):
    for line in open(src):
        m = re.match(r"(\S+)\s+slot (\d)\s+line\s+(\d+)px\s+lc-alphabet\s+(\d+)px", line)
        if m:
            fam, s, lh, aw = m.groups()
            measure.setdefault(fam, {})[int(s)] = {"line": int(lh), "alpha": int(aw)}

# key, display, group, origin, note
FAMS = [
    ("LibreFranklin", "Libre Franklin", "grotesque",
     "Impallari Type, after Morris Fuller Benton's Franklin Gothic (1902)",
     "The best-rounded grotesque in the bench and the only one that matches "
     "Lexica on every axis at once: counters intact, 99% of Latin Extended-A, "
     "a real drawn italic, and a normalized set width of 23.8 against Lexica's "
     "23.9. It is also the ancestor of Public Sans, which came LAST on width in "
     "the sans bench — the difference is Public Sans's wider optical fitting, "
     "not the skeleton."),
    ("FamiljenGrotesk", "Familjen Grotesk", "grotesque",
     "Letters from Sweden",
     "Narrowest set in the bench once x-height is normalized (23.8), and by a "
     "wide margin the most open 'a' of anything measured here — 27 counter "
     "pixels at a 12px x-height against Lexica's 8 and Edgar's 17. Costs "
     "letterform conventionality: the forms are idiosyncratic enough to read as "
     "a voice rather than a default, and Latin Extended-A stops at 97%."),
    ("WixMadeforText", "Wix Madefor Text", "grotesque",
     "Wix, drawn as the TEXT optical cut of Madefor",
     "The one candidate whose name is a promise about running text, and the "
     "counters back it up — the most open 'o' in the bench at 40 pixels. Then "
     "it loses on the two things that decide this: 80% of Latin Extended-A, so "
     "a fifth of accented European text falls back to Noto glyphs, and a set "
     "width of 26.1 normalized, joint-widest here."),
    ("HankenGrotesk", "Hanken Grotesk", "grotesque",
     "Hanken Design Co.",
     "Competent and unremarkable, which for body text is not an insult. "
     "Counters hold, 99% Extended-A, real italic, exact 33/39/45/51 leading. "
     "It simply does nothing Libre Franklin does not do slightly better, and "
     "sets 4% wider."),
    ("HostGrotesk", "Host Grotesk", "grotesque",
     "Host",
     "Open counters and a clean roman, undercut by 88% Latin Extended-A "
     "coverage and a normalized width of 25.8 — wider than the installed sans "
     "with nothing to show for it."),
    ("InstrumentSans", "Instrument Sans", "grotesque",
     "Instrument",
     "Widest tie in the bench at 26.1 normalized, and the thinnest coverage of "
     "anything here: 77% of Latin Extended-A. Its counters are excellent — the "
     "largest 'e' eye measured, 13 pixels — but a face that hands a quarter of "
     "its accented characters to Noto is not a reading face for this device."),
    ("Roboto", "Roboto", "grotesque",
     "Christian Robertson for Google, 2011",
     "The neo-grotesque everybody has read a million words in, and it holds up "
     "better than its reputation suggests: 100% Extended-A, counters intact, "
     "a tight 25.0 normalized. What it does not have is a drawn italic — the "
     "slope is close enough to a sloped roman that emphasis reads as a tilt "
     "rather than a change of voice."),
    ("Archivo", "Archivo", "disqualified",
     "Omnibus-Type",
     "DISQUALIFIED on the panel, not on the drawing board. At a 12px x-height "
     "the apertures of its 's' seal shut — 5 enclosed pixels where an open "
     "letter must have zero — so the letter renders as a blob. Its raw 273px "
     "set width is the smallest number in the bench and it is a mirage: "
     "Archivo lands an 11px x-height at slot 0, not 12, so it is simply set "
     "smaller. Normalised it is 24.8, wider than the installed sans."),
    ("SchibstedGrotesk", "Schibsted Grotesk", "disqualified",
     "Schibsted, for Norwegian news brands",
     "DISQUALIFIED for the same reason as Archivo and worse: 6 enclosed pixels "
     "in the 's' at a 12px x-height, and its slot 0 also lands at 11px rather "
     "than 12, which flatters its raw width. Normalised it is the second-widest "
     "thing in the bench at 26.0."),
]

CONTROLS = [
    ("LexicaUltralegible", "Lexica Ultralegible", "The installed sans, S tier since 3 August 2026. Everything here has to beat it."),
    ("Inter", "Inter", "Neo-grotesque, already measured by the sans bench. Carried, not rebuilt."),
    ("IBMPlexSans", "IBM Plex Sans", "Neo-grotesque, already measured by the sans bench. Carried, not rebuilt."),
    ("PublicSans", "Public Sans", "Libre Franklin's descendant, and the widest face in the sans bench."),
    ("Edgar", "Edgar", "The installed serif, for scale."),
]

PAID = [
    ("Neue Haas Grotesk Text", "Monotype / Christian Schwartz",
     "The canonical answer to this whole question: Helvetica restored from "
     "Max Miedinger's drawings WITH a genuine Text cut — looser fitting, "
     "opened apertures, larger counters, all the things the Display cut gives "
     "up. If a grotesque is ever going to work on a 1-bit panel it is this one.",
     "Monotype EULA; embedding is a separate tier."),
    ("Founders Grotesk Text", "Klim Type Foundry",
     "Kris Sowersby's grotesque with a real optical Text size rather than a "
     "lighter weight pretending to be one. Wider apertures and sturdier joins "
     "than the regular cut.",
     "Desktop license; Klim sells app/device embedding separately."),
    ("Söhne / Söhne Buch", "Klim Type Foundry",
     "Akzidenz-Grotesk as remembered rather than as drawn. Buch is the reading "
     "weight and the relevant one here.",
     "Desktop license; embedding priced separately."),
    ("National 2", "Klim Type Foundry",
     "A grotesque built for editorial text families, with the widest range of "
     "text-usable weights of anything on this list.",
     "Desktop license."),
    ("GT America", "Grilli Type",
     "The American-gothic-meets-Swiss grotesque, in six widths. Worth noting "
     "this fork already licenses GT Alpina, so the foundry relationship exists.",
     "Desktop license; Grilli sells embedding tiers."),
    ("Aktiv Grotesk", "Dalton Maag",
     "Drawn explicitly as a Helvetica/Univers replacement without their small-"
     "size failures — closed apertures opened, tight fitting loosened.",
     "Dalton Maag license; embedding negotiated."),
    ("Basis Grotesque", "Colophon Foundry",
     "A British grotesque with unusually large counters for the genre, which "
     "is exactly the variable this bench measures.",
     "Desktop license."),
    ("Untitled Sans", "Klim Type Foundry",
     "Sowersby's deliberately ordinary grotesque, drawn from the 'default' "
     "faces of the 1970s. Neutral in the way body text wants.",
     "Desktop license."),
    ("Suisse Int'l", "Swiss Typefaces",
     "The purest Akzidenz descendant on this list, and the tightest — likely "
     "the worst fit for a hard threshold, listed for completeness.",
     "Desktop license."),
    ("ABC Diatype", "Dinamo",
     "A contemporary grotesque with a genuine text cut and unusually generous "
     "sidebearings for the genre.",
     "Dinamo license; embedding is a named tier."),
    ("Trade Gothic Next", "Monotype",
     "Jackson Burke's Trade Gothic redrawn by Akira Kobayashi with the text "
     "sizes actually fixed.",
     "Monotype EULA."),
    ("ATF Franklin Gothic", "ATF Collection",
     "The authoritative Franklin Gothic revival — the paid answer to what "
     "Libre Franklin approximates, and Libre Franklin was this bench's winner.",
     "Desktop license."),
]

SOURCES = [
    ("Bigelow &amp; Holmes — How and Why We Designed Lucida (on coarse rasterisation)",
     "https://lucidafonts.com/blogs/bigelow-holmes-blog/how-and-why-we-designed-lucida"),
    ("Typeface features and legibility research — Vision Research (2019)",
     "https://www.sciencedirect.com/science/article/pii/S0042698919301087"),
    ("Klim Type Foundry — Founders Grotesk", "https://klim.co.nz/retail-fonts/founders-grotesk/"),
    ("Grilli Type — GT America", "https://www.grillitype.com/typeface/gt-america"),
    ("Google Fonts — the nine free candidates", "https://fonts.google.com/"),
    ("CrossPoint sans-serif bench (the prior round)",
     "https://claude.ai/code/artifact/a4d8b143-4012-4514-9224-ee860d7fb44d"),
]


def uri(fam, slot):
    p = os.path.join(PNG, f"reading_{fam}_{slot}.png")
    return "data:image/png;base64," + base64.b64encode(open(p, "rb").read()).decode()


def norm(fam, slot=0):
    """Set width per x-height pixel — the only width number worth comparing."""
    xh = (slots[fam]["xheights"][slot] if fam in slots
          else [12, 14, 16, 18][slot])
    return measure[fam][slot]["alpha"] / xh


def build():
    spec = {k: [uri(k, s) for s in range(4)] for k, *_ in FAMS}
    spec.update({k: [uri(k, s) for s in range(4)] for k, *_ in CONTROLS})

    meta = {}
    for key, name, group, origin, note in FAMS:
        e = slots[key]
        meta[key] = {"name": name, "origin": origin, "note": note, "group": group,
                     "sizes": e["sizes"], "xh": e["xheights"], "adv": e["advY"],
                     "x": e["src"]["x_height_em"], "n": e["src"]["n_width_em"],
                     "cov": e["cov"], "cnt": e["counters"]["0"] if "0" in e["counters"] else e["counters"][0],
                     "m": measure[key], "norm": round(norm(key), 2), "ctrl": False}
    for key, name, note in CONTROLS:
        c = ctrl.get(key, {})
        cnt = c.get("counters", {}).get("0") or c.get("counters", {}).get(0) or {}
        meta[key] = {"name": name, "origin": "control — measured by the sans bench",
                     "note": note, "group": "control",
                     "sizes": c.get("sizes"), "xh": [12, 14, 16, 18],
                     "adv": None,
                     "x": c.get("src", {}).get("x_height_em"),
                     "n": c.get("src", {}).get("n_width_em"),
                     "cov": c.get("cov"), "cnt": cnt,
                     "m": measure[key], "norm": round(norm(key), 2), "ctrl": True}

    rail, groups = [], [("grotesque", "Text grotesques"),
                        ("disqualified", "Disqualified on the panel"),
                        ("control", "Controls")]
    order = sorted([k for k, *_ in FAMS if meta[k]["group"] == "grotesque"],
                   key=lambda k: meta[k]["norm"])
    for gid, glabel in groups:
        rail.append(f'<li class="rail-head">{glabel}</li>')
        keys = (order if gid == "grotesque"
                else [k for k, *_ in FAMS if meta[k]["group"] == gid] if gid == "disqualified"
                else [k for k, *_ in CONTROLS])
        for key in keys:
            d = meta[key]
            cls = " ref" if d["ctrl"] else (" bad" if d["group"] == "disqualified" else "")
            rail.append(
                f'<li><button class="fam{cls}" data-fam="{key}" role="tab" aria-selected="false">'
                f'<span class="fam-name" style="font-family:\'S{key}\',serif">{d["name"]}</span>'
                f'<span class="fam-num">{d["norm"]:.1f}</span></button></li>')

    rows = []
    for key in order + [k for k, *_ in FAMS if meta[k]["group"] == "disqualified"] + [k for k, *_ in CONTROLS]:
        d = meta[key]
        c = d["cnt"] or {}
        sealed = c.get("s", 0) > 0
        cls = ' class="is-ref"' if d["ctrl"] else (' class="is-bad"' if d["group"] == "disqualified" else "")
        xh0 = d["xh"][0]
        rows.append(
            f'<tr{cls}><th scope="row">{d["name"]}</th>'
            f'<td class="num">{d["x"]:.3f}</td>'
            f'<td class="num{"" if xh0 == 12 else " low"}">{xh0}</td>'
            f'<td class="num">{d["m"][0]["alpha"]}</td>'
            f'<td class="num strong">{d["norm"]:.1f}</td>'
            f'<td class="num">{c.get("a","—")}</td><td class="num">{c.get("e","—")}</td>'
            f'<td class="num">{c.get("o","—")}</td>'
            f'<td class="num{" low" if sealed else ""}">{c.get("s","—")}</td>'
            f'<td class="num{"" if (d["cov"] or {}).get("latinA",100) >= 95 else " low"}">'
            f'{(d["cov"] or {}).get("latinA","—")}</td>'
            f'<td class="num">{"/".join(str(s) for s in d["sizes"]) if d["sizes"] else "—"}</td></tr>')

    paid = "".join(
        f'<tr><th scope="row">{n}<span class="foundry">{f}</span></th>'
        f'<td>{why}</td><td class="risk">{risk}</td></tr>' for n, f, why, risk in PAID)
    src = "".join(f'<li><a href="{u}">{t}</a></li>' for t, u in SOURCES)

    open(OUT, "w").write(TEMPLATE.format(
        fontcss=fontcss, rail="".join(rail), rows="".join(rows),
        paid_rows=paid, sources=src,
        specimens=json.dumps(spec), meta=json.dumps(meta)))
    print(f"wrote {OUT} ({os.path.getsize(OUT)/1024:.0f} KB)")


TEMPLATE = r"""<meta charset="utf-8">
<title>Text-grotesque bench — CrossPoint Reader</title>
<style>
{fontcss}

:root {{
  color-scheme: light dark;
  --paper: #FBFAF6; --paper-2: #F2F1EA; --rule: #DCDACE;
  --ink: #1B1C19; --ink-2: #55574D; --ink-3: #85877A;
  --panel: #E9E9E3;
  --accent: #1F5E86;        /* proofing blue: this round is a measurement, not a verdict */
  --bad: #A8321C;
  --shadow: 0 1px 0 rgba(27,28,25,.05), 0 12px 32px -18px rgba(27,28,25,.45);
}}
@media (prefers-color-scheme: dark) {{
  :root {{ --paper:#12140F; --paper-2:#1A1D16; --rule:#32362B; --ink:#E8E7DA;
    --ink-2:#A9AB9B; --ink-3:#74776A; --accent:#6FB4DE; --bad:#E8735A;
    --shadow: 0 1px 0 rgba(0,0,0,.4), 0 16px 40px -20px rgba(0,0,0,.8); }}
}}
:root[data-theme="dark"] {{ --paper:#12140F; --paper-2:#1A1D16; --rule:#32362B;
  --ink:#E8E7DA; --ink-2:#A9AB9B; --ink-3:#74776A; --accent:#6FB4DE; --bad:#E8735A;
  --shadow: 0 1px 0 rgba(0,0,0,.4), 0 16px 40px -20px rgba(0,0,0,.8); }}
:root[data-theme="light"] {{ --paper:#FBFAF6; --paper-2:#F2F1EA; --rule:#DCDACE;
  --ink:#1B1C19; --ink-2:#55574D; --ink-3:#85877A; --accent:#1F5E86; --bad:#A8321C;
  --shadow: 0 1px 0 rgba(27,28,25,.05), 0 12px 32px -18px rgba(27,28,25,.45); }}

* {{ box-sizing: border-box; }}
body {{ margin:0; background:var(--paper); color:var(--ink);
  font-family:'ChromeText', Georgia, serif; font-size:17px; line-height:1.62;
  -webkit-text-size-adjust:100%; }}
.wrap {{ max-width:1180px; margin:0 auto; padding:0 24px 96px; }}
h1,h2,h3 {{ font-weight:600; text-wrap:balance; line-height:1.2; margin:0; }}
p {{ margin:0; }}
a {{ color:var(--accent); text-underline-offset:2px; }}
.mono {{ font-family:'ChromeMono', ui-monospace, monospace; }}
.eyebrow {{ font-family:'ChromeMono',monospace; font-size:11px; letter-spacing:.14em;
  text-transform:uppercase; color:var(--ink-3); }}

header.top {{ border-bottom:2px solid var(--ink); padding:56px 0 22px;
  display:flex; flex-direction:column; gap:18px; }}
header.top h1 {{ font-size:clamp(32px,5.4vw,54px); letter-spacing:-.015em; }}
header.top h1 em {{ font-style:normal; color:var(--accent); }}
.standfirst {{ font-size:clamp(18px,2.2vw,21px); color:var(--ink-2); max-width:62ch; }}
.byline {{ display:flex; flex-wrap:wrap; gap:8px 22px; }}

.finding {{ margin-top:40px; display:grid;
  grid-template-columns:repeat(auto-fit,minmax(250px,1fr)); gap:0;
  border:1px solid var(--rule); border-left:3px solid var(--accent); }}
.finding div {{ padding:22px 24px; border-right:1px solid var(--rule); }}
.finding div:last-child {{ border-right:0; }}
.finding h2 {{ font-size:16px; margin-bottom:8px; }}
.finding p {{ font-size:15px; color:var(--ink-2); }}
.finding .big {{ font-family:'ChromeMono',monospace; font-size:28px; color:var(--ink);
  display:block; margin-bottom:4px; font-variant-numeric:tabular-nums; }}

section {{ margin-top:72px; }}
section > h2 {{ font-size:clamp(24px,3.4vw,32px); letter-spacing:-.01em;
  padding-bottom:12px; border-bottom:1px solid var(--ink); }}
.lede {{ margin-top:18px; max-width:68ch; color:var(--ink-2); }}

.bench {{ margin-top:28px; display:grid; grid-template-columns:270px minmax(0,1fr);
  gap:32px; align-items:start; }}
.rail {{ position:sticky; top:16px; }}
.rail ul {{ list-style:none; margin:0; padding:0; }}
.rail-head {{ font-family:'ChromeMono',monospace; font-size:10.5px; letter-spacing:.13em;
  text-transform:uppercase; color:var(--ink-3); padding:18px 0 7px;
  border-bottom:1px solid var(--rule); margin-bottom:4px; }}
.rail-head:first-child {{ padding-top:0; }}
button.fam {{ width:100%; display:flex; align-items:baseline; justify-content:space-between;
  gap:10px; padding:7px 9px; background:none; border:0; border-radius:2px;
  color:var(--ink); cursor:pointer; text-align:left; font:inherit; }}
button.fam:hover {{ background:var(--paper-2); }}
button.fam:focus-visible {{ outline:2px solid var(--accent); outline-offset:1px; }}
button.fam[aria-selected="true"] {{ background:var(--ink); color:var(--paper); }}
button.fam[aria-selected="true"] .fam-num {{ color:var(--paper); opacity:.75; }}
button.fam.bad .fam-name {{ text-decoration:line-through; text-decoration-color:var(--bad);
  text-decoration-thickness:1px; }}
button.fam.ref .fam-name {{ font-family:'ChromeText',serif !important; font-style:italic;
  color:var(--ink-2); }}
button.fam.ref[aria-selected="true"] .fam-name {{ color:var(--paper); }}
.fam-name {{ font-size:17px; line-height:1.25; }}
.fam-num {{ font-family:'ChromeMono',monospace; font-size:11px;
  font-variant-numeric:tabular-nums; color:var(--ink-3); flex:none; }}

.stage {{ display:grid; gap:20px; }}
.stage-head {{ display:flex; flex-wrap:wrap; gap:16px 24px; align-items:flex-end;
  justify-content:space-between; }}
.stage-title {{ font-size:26px; letter-spacing:-.01em; }}
.slotbar {{ display:flex; border:1px solid var(--rule); }}
.slotbar button {{ font-family:'ChromeMono',monospace; font-size:11px; letter-spacing:.06em;
  padding:8px 13px; background:var(--paper); border:0; border-right:1px solid var(--rule);
  color:var(--ink-2); cursor:pointer; }}
.slotbar button:last-child {{ border-right:0; }}
.slotbar button[aria-pressed="true"] {{ background:var(--ink); color:var(--paper); }}
.slotbar button:focus-visible {{ outline:2px solid var(--accent); outline-offset:-2px; }}

.viewer {{ display:grid; grid-template-columns:minmax(0,auto) minmax(0,1fr); gap:30px;
  align-items:start; }}
.device {{ background:var(--panel); padding:10px; border:1px solid var(--rule);
  box-shadow:var(--shadow); max-width:100%; }}
.device img {{ display:block; width:528px; max-width:100%; height:auto;
  image-rendering:pixelated; }}
.device-note {{ font-family:'ChromeMono',monospace; font-size:10px; letter-spacing:.1em;
  text-transform:uppercase; color:var(--ink-3); margin-top:9px; }}
.readout dl {{ margin:0; display:grid; grid-template-columns:auto 1fr; gap:5px 16px; }}
.readout dt {{ font-family:'ChromeMono',monospace; font-size:10.5px; letter-spacing:.08em;
  text-transform:uppercase; color:var(--ink-3); padding-top:3px; }}
.readout dd {{ margin:0; font-family:'ChromeMono',monospace; font-size:13px;
  font-variant-numeric:tabular-nums; }}
.readout .note {{ margin-top:20px; color:var(--ink-2); font-size:15.5px; }}
.origin {{ font-size:13px; color:var(--ink-3); margin-top:4px; }}
.live {{ margin-top:22px; padding-top:16px; border-top:1px solid var(--rule); }}
.live-line {{ font-size:27px; line-height:1.35; margin-top:6px; }}

.scroller {{ overflow-x:auto; margin-top:24px; }}
table {{ border-collapse:collapse; width:100%; font-size:14px; }}
caption {{ text-align:left; color:var(--ink-3); font-size:13px; padding-bottom:10px; }}
th,td {{ padding:8px 11px; text-align:left; vertical-align:top;
  border-bottom:1px solid var(--rule); }}
thead th {{ font-family:'ChromeMono',monospace; font-size:10px; letter-spacing:.09em;
  text-transform:uppercase; color:var(--ink-3); font-weight:400;
  border-bottom:1px solid var(--ink); white-space:nowrap; }}
tbody th {{ font-weight:600; white-space:nowrap; }}
td.num {{ font-family:'ChromeMono',monospace; font-variant-numeric:tabular-nums;
  text-align:right; white-space:nowrap; }}
td.strong {{ color:var(--ink); font-weight:600; }}
td.low {{ color:var(--bad); }}
tr.is-ref th, tr.is-ref td {{ color:var(--ink-3); font-style:italic; }}
tr.is-bad th {{ text-decoration:line-through; text-decoration-color:var(--bad); }}
.foundry {{ display:block; font-weight:400; font-size:12px; color:var(--ink-3);
  font-style:normal; }}
td.risk {{ color:var(--ink-2); font-size:13px; }}
table.paid th[scope="row"] {{ width:22%; }}

.verdict {{ margin-top:26px; border:1px solid var(--rule); border-top:3px solid var(--accent);
  padding:28px; background:var(--paper-2); }}
.verdict h3 {{ font-size:22px; margin-bottom:12px; }}
.verdict p + p {{ margin-top:12px; }}
.runners {{ display:grid; grid-template-columns:repeat(auto-fit,minmax(250px,1fr));
  gap:20px; margin-top:20px; }}
.runner {{ border:1px solid var(--rule); padding:20px; }}
.runner .rank {{ font-family:'ChromeMono',monospace; font-size:10.5px; letter-spacing:.12em;
  text-transform:uppercase; color:var(--ink-3); }}
.runner h4 {{ font-size:18px; margin:6px 0 8px; font-weight:600; }}
.runner p {{ font-size:14.5px; color:var(--ink-2); }}

ol.steps {{ margin:20px 0 0; padding-left:22px; }}
ol.steps li {{ margin-bottom:10px; }}
code {{ font-family:'ChromeMono',monospace; font-size:12.5px; background:var(--paper-2);
  padding:1px 5px; border:1px solid var(--rule); }}
ul.sources {{ margin:18px 0 0; padding-left:20px; font-size:14.5px; color:var(--ink-2); }}
ul.sources li {{ margin-bottom:7px; }}
footer {{ margin-top:72px; padding-top:22px; border-top:1px solid var(--rule);
  color:var(--ink-3); font-size:13px; }}

@media (max-width:1180px) {{
  .viewer {{ grid-template-columns:1fr; }}
  .device {{ justify-self:start; }}
  .readout .note {{ max-width:62ch; }}
}}
@media (max-width:900px) {{
  .bench {{ grid-template-columns:1fr; }}
  .rail {{ position:static; }}
  .rail ul {{ display:grid; grid-template-columns:repeat(auto-fill,minmax(160px,1fr)); gap:2px 10px; }}
  .rail-head {{ grid-column:1/-1; }}
  .finding div {{ border-right:0; border-bottom:1px solid var(--rule); }}
}}
@media (prefers-reduced-motion:reduce) {{ * {{ transition:none !important; }} }}
</style>

<div class="wrap">

<header class="top">
  <span class="eyebrow">CrossPoint Reader &middot; typeface evaluation, round two &middot; 3 August 2026</span>
  <h1>Nine text grotesques against the installed sans. <em>None of them wins.</em></h1>
  <p class="standfirst">Same bench as the sans round: every face built to a measured
    x-height of 12/14/16/18&thinsp;px, rendered by the real <span class="mono">GfxRenderer</span>
    into the device's 528&times;792 one-bit framebuffer. This round adds the measurement the
    genre actually needs — whether a counter still exists as white pixels once the panel has
    thresholded it.</p>
  <div class="byline eyebrow">
    <span>9 candidates + 5 controls</span>
    <span>set width normalized per x-height pixel</span>
    <span>bench: lib/EpdFont/scripts/grotesque-candidates.yaml</span>
  </div>
</header>

<div class="finding">
  <div>
    <h2>The prediction was wrong</h2>
    <p><span class="big">7 of 9</span>Grotesques were supposed to lose on closed apertures.
      Seven of nine hold their counters at a 12&thinsp;px x-height as well as the installed
      sans or better. The genre's reputation does not survive contact with the measurement.</p>
  </div>
  <div>
    <h2>Two really do seal shut</h2>
    <p><span class="big">2</span>Archivo and Schibsted Grotesk close the apertures of their
      <span class="mono">s</span> at reading size — 5 and 6 enclosed pixels where an open
      letter must have zero. On the panel the letter becomes a blob.</p>
  </div>
  <div>
    <h2>And the width edge is a mirage</h2>
    <p><span class="big">0.4%</span>The best grotesque beats the installed sans by four parts
      in a thousand on width. Two candidates looked much narrower until their x-height was
      normalized; they were simply set smaller.</p>
  </div>
</div>

<section>
  <h2>Why raw set width lies here</h2>
  <p class="lede">Archivo's lowercase alphabet is 273&thinsp;px against Lexica Ultralegible's
    287 — a 5% win, and meaningless. Archivo's slot 0 lands an <strong>11&thinsp;px</strong>
    x-height, not 12: the hinter skips the pixel value that would put it on target, so the
    face is set smaller and of course measures narrower. Schibsted Grotesk does the same.
    Every width number on this page is therefore divided by the x-height it was actually
    rendered at, and that normalized figure — pixels of alphabet per pixel of x-height — is
    what the rail and the table sort by. Under it Archivo is 24.8 against Lexica's 23.9:
    not narrower, <em>wider</em>.</p>
</section>

<section>
  <h2>The bench</h2>
  <p class="lede">Pick a face and a size slot. The number beside each name is the normalized
    set width; lower is more text per line. Struck-through names failed the counter test.</p>
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
            <dt>x-height</dt><dd id="rXh">—</dd>
            <dt>Line height</dt><dd id="rLine">—</dd>
            <dt>Set width</dt><dd id="rSet">—</dd>
            <dt>Normalised</dt><dd id="rNorm">—</dd>
            <dt>Counters a/e/o/s</dt><dd id="rCnt">—</dd>
            <dt>Latin Ext-A</dt><dd id="rCov">—</dd>
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
  <p class="lede">Sorted by normalized set width. The counter columns are enclosed white
    pixels surviving the panel's threshold at a 12&thinsp;px x-height, measured on the same
    FreeType render the converter stores, with a flood fill from outside separating an
    enclosed counter from an open aperture. For <span class="mono">a</span>,
    <span class="mono">e</span> and <span class="mono">o</span> a bigger number is a counter
    that stayed open. For <span class="mono">s</span> the only acceptable value is
    <strong>zero</strong> — anything else means the aperture sealed and the letter has
    become a blob.</p>
  <div class="scroller">
    <table>
      <caption>Red x-height means the slot missed its 12&thinsp;px target. Red Ext-A means
        under 95% coverage, where the rest is silently filled from Noto Sans at build time.
        Last five rows are controls, already measured by the sans bench.</caption>
      <thead><tr>
        <th scope="col">Typeface</th><th scope="col">x/em</th><th scope="col">x-ht px</th>
        <th scope="col">Set px</th><th scope="col">Normalised</th>
        <th scope="col">a</th><th scope="col">e</th><th scope="col">o</th><th scope="col">s</th>
        <th scope="col">Ext-A %</th><th scope="col">Point sizes</th>
      </tr></thead>
      <tbody>{rows}</tbody>
    </table>
  </div>
</section>

<section>
  <h2>The ruling</h2>
  <div class="verdict">
    <h3>Keep Lexica Ultralegible. Install nothing from this round.</h3>
    <p>The best grotesque here, Libre Franklin, ties the installed sans on width to within
      four parts in a thousand, matches it on counters, and matches it on coverage. That is
      a tie, and a tie does not justify a sixth family on five surfaces plus two SD cards.</p>
    <p>What none of them has is the thing Lexica was chosen for. Look at the
      <span class="mono">Illinois 1lI0O</span> line in any specimen above: in every grotesque
      in this bench the lowercase <span class="mono">l</span> and the capital
      <span class="mono">I</span> are the same vertical bar. Lexica draws them apart on
      purpose — that is the entire brief it inherited from Atkinson Hyperlegible. On a
      one-bit panel at a 12&thinsp;px x-height, that is worth more than a rounding error of
      set width.</p>
    <p>The genre prediction was still wrong, and worth recording: grotesques were expected
      to fail on closed apertures, and seven of nine did not. If a grotesque is ever wanted
      here, the objection is letterform ambiguity, not ink.</p>
  </div>
  <div class="runners">
    <div class="runner">
      <span class="rank">If a sixth family is ever wanted</span>
      <h4>Libre Franklin</h4>
      <p>99% of Latin Extended-A, counters intact, a real drawn italic, and the Franklin
        Gothic skeleton — the most conventional text grotesque available under the OFL.
        Recipe is already in the bench file; it builds in one command.</p>
    </div>
    <div class="runner">
      <span class="rank">If the brief ever changes to voice</span>
      <h4>Familjen Grotesk</h4>
      <p>The narrowest normalized set in the bench and the most open <span class="mono">a</span>
        of anything measured, Edgar included. It reads as a designed choice rather than a
        default, which is a reason to pick it and a reason not to.</p>
    </div>
    <div class="runner">
      <span class="rank">Do not install</span>
      <h4>Archivo &middot; Schibsted Grotesk</h4>
      <p>Both seal the <span class="mono">s</span> at reading size, and both miss the
        12&thinsp;px x-height slot, which is what made their raw widths look competitive.
        Two independent disqualifications.</p>
    </div>
  </div>
  <ol class="steps">
    <li>Rebuild the bench: <code>python3 lib/EpdFont/scripts/build-sd-fonts.py --config lib/EpdFont/scripts/grotesque-candidates.yaml --output-dir out/</code></li>
    <li>Re-render a specimen: <code>CPFONT_DIR=/fonts ./render_harness reading LibreFranklin</code> from <code>tools/calendar_preview</code></li>
    <li><code>installed_families:</code> is unchanged at five — Edgar, Coelacanth, Rosarivo, TeX&nbsp;Gyre&nbsp;Schola, LexicaUltralegible.</li>
  </ol>
</section>

<section>
  <h2>Paid grotesques worth the license</h2>
  <p class="lede">Same posture as the sans round: converting a licensed font to
    <code>.cpfont</code> bitmaps for your own card is normally within a desktop license;
    redistributing those bitmaps is not. This fork already works that way for Edgar, GT Alpina,
    Venetian&nbsp;301 and Caledonia&nbsp;CC — recipe committed, font files never. Every entry
    below would ship the same way. Prices are not listed because they were not verified.</p>
  <div class="scroller">
    <table class="paid">
      <thead><tr><th scope="col">Typeface</th><th scope="col">Why it is on this list</th>
        <th scope="col">Licensing reality</th></tr></thead>
      <tbody>{paid_rows}</tbody>
    </table>
  </div>
</section>

<section>
  <h2>Method, and one bug worth knowing about</h2>
  <ol class="steps">
    <li>Each family's hinted x-height was measured at every point size from 7 to 29 at
      150&nbsp;DPI — the converter's own resolution — and the four closest to 12/14/16/18&nbsp;px
      chosen. Where the hinter skips a pixel value the slot misses; that is flagged in red
      rather than hidden.</li>
    <li>One ascent/descent span per family, solved against the leading the tier actually
      <em>ships</em> (34/39/45/51, measured off Edgar and TeX Gyre Schola) rather than the
      34/40/46/51 stated in <code>sd-fonts.yaml</code>'s header.</li>
    <li><strong>The metrics probe was wrong on the first run.</strong>
      <code>build-sd-fonts.py</code> scales <code>metrics:</code> by
      <code>head.unitsPerEm/1000</code> before writing hhea, so a probe that writes the raw
      number is off by 2.048&times; on every 2048-upem face. It silently produced 0.68&nbsp;em
      line heights for Roboto and Schibsted Grotesk — plausible-looking numbers, badly wrong.
      Fixed, re-swept, and worth carrying: the same bug is latent in the sans bench's
      predicted values for its 2048-upem families, though that page reports measured line
      heights from the harness rather than predicted ones.</li>
    <li><code>render_harness reading FAMILY</code> wrapped the same passage against the
      firmware's <span class="mono">getTextWidth()</span> with kerning prewarmed, drew it with
      <span class="mono">GfxRenderer</span>, and dumped the framebuffer.</li>
  </ol>
  <h3 style="margin-top:34px;font-size:19px">Sources</h3>
  <ul class="sources">{sources}</ul>
</section>

<footer>
  Bench: <span class="mono">lib/EpdFont/scripts/grotesque-candidates.yaml</span> &middot;
  specimen mode: <span class="mono">tools/calendar_preview/render_harness.cpp</span> &middot;
  branch <span class="mono">claude/readable-sans-serif-test-d58603</span>.
  Nothing from this round is installed on any surface.
</footer>
</div>

<script>
const SPEC = {specimens};
const META = {meta};
let fam = 'LibreFranklin', slot = 0;
const $ = (id) => document.getElementById(id);

function render() {{
  const d = META[fam];
  $('specImg').src = SPEC[fam][slot];
  $('specImg').alt = d.name + ' rendered on the device at size slot ' + slot;
  $('specName').textContent = d.name;
  $('specName').style.fontFamily = d.ctrl ? '' : "'S" + fam + "', serif";
  $('specOrigin').textContent = d.origin;
  $('rPt').textContent = d.sizes ? d.sizes[slot] + ' pt' : '—';
  $('rXh').textContent = d.xh ? d.xh[slot] + ' px' : '—';
  $('rLine').textContent = d.m[slot].line + ' px';
  $('rSet').textContent = d.m[slot].alpha + ' px';
  $('rNorm').textContent = d.norm.toFixed(1);
  const c = d.cnt || {{}};
  $('rCnt').textContent = [c.a, c.e, c.o, c.s].map(v => v === undefined ? '—' : v).join(' / ')
    + (c.s > 0 ? '   SEALED' : '');
  $('rCov').textContent = d.cov ? d.cov.latinA.toFixed(0) + '%' : '—';
  $('rNote').textContent = d.note;
  $('rLive').style.fontFamily = d.ctrl ? "'ChromeText', serif" : "'S" + fam + "', serif";
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
