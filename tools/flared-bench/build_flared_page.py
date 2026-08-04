#!/usr/bin/env python3
"""Build docs/flared-semiserif.html — the triage page for the flared/semi-serif cell.

docs/type-coverage.html classifies all 37 curated families into 22 structural
classes and names two as empty. Typewriter was answered by IBM Plex. This is the
other one: flared / semi-serif, the glyphic-incised class whose canonical text
face is Optima.

WHAT THIS PAGE IS. A shortlist, rendered in a browser at matched x-height, to
decide which faces are worth building into .cpfont and putting through the real
bench. It is NOT the bench. sans-candidates.yaml and grotesque-candidates.yaml
each drove a real one — built at swept sizes, measured on the hinted render,
compared through GfxRenderer at 1 bit. This page cannot see any of that, and the
things that killed candidates in those benches (an `s` whose apertures seal shut
at 11 px, stems that thin to nothing under thresholding) are invisible here.

Treat the output as "which four of these get built", not "which one ships".

Usage:
    python3 tools/flared-bench/build_flared_page.py

Downloads each face from google/fonts, subsets it to the specimen's codepoints,
converts to woff2 and base64-embeds it, so the page is self-contained and works
offline with no CDN. Re-running regenerates it byte-for-byte.
"""

import base64
import io
import json
import pathlib
import subprocess
import sys
import tempfile
import urllib.request

REPO = pathlib.Path(__file__).resolve().parents[2]
OUT = REPO / "docs" / "flared-semiserif.html"
GF = "https://raw.githubusercontent.com/google/fonts/main/ofl"

# The subset. Latin-1 plus the punctuation the specimen actually uses; keeping
# it this tight is what holds the embedded page under ~200 KB.
UNICODES = "U+0020-007E,U+00A0-00FF,U+2018-201D,U+2013,U+2014"

# (key, display name, google-fonts path, role, note)
#
# ROLE is what the row is for, and only "candidate" rows are being judged:
#   candidate — a flared/glyphic face that could fill the cell
#   control   — not flared; present so the eye has a baseline to reject against
#   installed — ships today, S tier. A candidate has to earn a slot next to it.
FACES = [
    ("Philosopher", "Philosopher", "philosopher/Philosopher-Regular.ttf", "candidate",
     "The closest thing here to Optima's brief: a sans whose stems swell at the ends rather than "
     "terminating flat, drawn for text rather than for titling. Also the only candidate with Cyrillic, "
     "which matters if the interval preset ever widens."),
    ("Federo", "Federo", "federo/Federo-Regular.ttf", "candidate",
     "Flared, and unusually even in colour for this class — the flare is restrained enough that it does not "
     "read as decoration at a paragraph's worth of text. Cyreal drew it for screen."),
    ("TenorSans", "Tenor Sans", "tenorsans/TenorSans-Regular.ttf", "candidate",
     "The subtlest flare of the set, and a 0.50 x-height that already sits where the reader's slots want it. "
     "Risk is the opposite of the usual one: the flare may be too slight to survive 1-bit rendering at all, "
     "at which point it is just another humanist sans and the cell stays empty."),
    ("Marcellus", "Marcellus", "marcellus/Marcellus-Regular.ttf", "candidate",
     "Roman inscriptional — the flare comes straight off chisel-cut capitals. Handsome, but inscriptional "
     "faces earn their keep in headings; the question this bench has to answer is whether its lowercase "
     "holds up over a full page."),
    ("Forum", "Forum", "forum/Forum-Regular.ttf", "candidate",
     "The most overtly glyphic of the set and the strongest claim to the class. Its 0.42 x-height is the "
     "problem: matched to the reader's slots it has to be set larger than everything else, which costs "
     "characters per line — the same trade that has sunk small-x-height faces before."),
    ("JuliusSansOne", "Julius Sans One", "juliussansone/JuliusSansOne-Regular.ttf", "candidate",
     "Included to be ruled out rather than because it can win. The 0.649 x-height is not a text x-height — "
     "the lowercase is nearly cap height — and the strokes are thin enough that 1-bit thresholding at "
     "12 px x-height will break them. Display, not reading."),
    ("QuattrocentoSans", "Quattrocento Sans", "quattrocentosans/QuattrocentoSans-Regular.ttf", "control",
     "Humanist sans with almost no flare. The control for 'is that flare actually doing anything' — if a "
     "candidate cannot be told from this at reading size, it is not filling the cell."),
    ("SortsMillGoudy", "Sorts Mill Goudy", "sortsmillgoudy/SortsMillGoudy-Regular.ttf", "control",
     "A true bracketed serif, for the other edge. Anything that looks like this is a serif and belongs in a "
     "cell that is already full."),
]

INSTALLED = ("LibreFranklin", "Libre Franklin", "librefranklin/LibreFranklin%5Bwght%5D.ttf", "installed",
             "The installed sans, S tier. It is not flared and does not pretend to be — it is here for scale "
             "and colour. A flared face that merely matches it has not earned a slot, because shipping it "
             "costs a card slot and a download.")

SPECIMEN = ("Whereof one cannot speak, thereof one must be silent. The quick brown fox "
            "jumps over the lazy dog; 0123456789 &amp; “quoted” — café, naïve, Zürich.")
WORD = "Hamburgefonstiv"


def fetch(path: str) -> bytes:
    with urllib.request.urlopen(f"{GF}/{path}", timeout=60) as r:
        return r.read()


def subset_woff2(ttf: bytes) -> bytes:
    with tempfile.TemporaryDirectory() as d:
        src = pathlib.Path(d) / "in.ttf"
        dst = pathlib.Path(d) / "out.woff2"
        src.write_bytes(ttf)
        subprocess.run(
            [sys.executable, "-m", "fontTools.subset", str(src),
             f"--unicodes={UNICODES}", "--layout-features=", "--flavor=woff2",
             f"--output-file={dst}"],
            check=True, capture_output=True)
        return dst.read_bytes()


def metrics(ttf: bytes) -> dict:
    from fontTools.ttLib import TTFont
    f = TTFont(io.BytesIO(ttf))
    upem = f["head"].unitsPerEm
    os2 = f["OS/2"]
    xh = getattr(os2, "sxHeight", 0) or 0
    if not xh:
        from fontTools.pens.boundsPen import BoundsPen
        gs = f.getGlyphSet()
        bp = BoundsPen(gs)
        gs["x"].draw(bp)
        xh = bp.bounds[3] if bp.bounds else 0
    cap = getattr(os2, "sCapHeight", 0) or 0
    hhea = f["hhea"]
    return {
        "upem": upem,
        "xh_em": xh / upem,
        "cap_em": (cap / upem) if cap else None,
        # Default line height the face asks for, in ems. The builder overrides
        # this per slot, but a face demanding 1.4 em is telling you something.
        "lead_em": (hhea.ascent - hhea.descent) / upem,
        # Designer straight from name ID 9, never from memory — same rule
        # docs/font-dates.md follows.
        "designer": (f["name"].getDebugName(9) or "—").strip(),
    }


def build():
    rows = []
    for key, label, path, role, note in FACES + [INSTALLED]:
        sys.stderr.write(f"  {label} ... ")
        sys.stderr.flush()
        ttf = fetch(path)
        m = metrics(ttf)
        b64 = base64.b64encode(subset_woff2(ttf)).decode("ascii")
        rows.append({"key": key, "label": label, "role": role, "note": note,
                     "b64": b64, **m})
        sys.stderr.write(f"x-height/em {m['xh_em']:.3f}\n")

    faces_css = "\n".join(
        f"@font-face{{font-family:'{r['key']}';font-display:block;"
        f"src:url(data:font/woff2;base64,{r['b64']}) format('woff2')}}"
        for r in rows)

    data = json.dumps([{k: r[k] for k in
                        ("key", "label", "role", "note", "xh_em", "cap_em", "lead_em", "upem", "designer")}
                       for r in rows], indent=1)

    OUT.write_text(TEMPLATE
                   .replace("/*FACES*/", faces_css)
                   .replace("/*DATA*/", data)
                   .replace("__SPECIMEN__", SPECIMEN)
                   .replace("__WORD__", WORD), encoding="utf-8")
    sys.stderr.write(f"\nwrote {OUT.relative_to(REPO)}  ({OUT.stat().st_size/1024:.0f} KB)\n")


TEMPLATE = r"""<!doctype html>
<meta charset="utf-8">
<title>Flared / semi-serif — the empty cell</title>
<style>
/*FACES*/
:root{--ink:#111;--dim:#666;--rule:#e2e2e2;--bg:#fff;--flag:#8a5a00;--flagbg:#fff8ec}
@media (prefers-color-scheme:dark){:root{--ink:#e8e8e8;--dim:#999;--rule:#333;--bg:#141414;--flag:#e0b060;--flagbg:#241d10}}
*{box-sizing:border-box}
body{margin:0 auto;padding:2.5rem 1.25rem 6rem;max-width:56rem;background:var(--bg);color:var(--ink);
 font-family:system-ui,-apple-system,"Segoe UI",sans-serif;line-height:1.55;-webkit-text-size-adjust:100%}
h1{font-size:1.6rem;margin:0 0 .25rem;letter-spacing:-.01em}
h2{font-size:1.05rem;margin:2.5rem 0 .5rem;padding-top:1.25rem;border-top:1px solid var(--rule)}
.sub{color:var(--dim);margin:0 0 1.5rem}
p{margin:.6rem 0}
a{color:inherit}
.flag{background:var(--flagbg);border-left:3px solid var(--flag);padding:.75rem 1rem;margin:1.25rem 0;border-radius:0 4px 4px 0}
.flag b{color:var(--flag)}
.controls{position:sticky;top:0;z-index:5;background:var(--bg);border-bottom:1px solid var(--rule);
 padding:.75rem 0;margin:1.5rem 0 0;display:flex;gap:1.25rem;flex-wrap:wrap;align-items:center}
.controls label{font-size:.82rem;color:var(--dim);display:flex;gap:.4rem;align-items:center}
button{font:inherit;font-size:.82rem;padding:.25rem .6rem;border:1px solid var(--rule);background:transparent;
 color:var(--ink);border-radius:4px;cursor:pointer}
button[aria-pressed=true]{background:var(--ink);color:var(--bg);border-color:var(--ink)}
.face{padding:1.5rem 0;border-bottom:1px solid var(--rule)}
.face header{display:flex;gap:.6rem;align-items:baseline;flex-wrap:wrap;margin-bottom:.15rem}
.face h3{margin:0;font-size:1rem}
.role{font-size:.68rem;text-transform:uppercase;letter-spacing:.07em;padding:.1rem .4rem;border-radius:3px;
 border:1px solid var(--rule);color:var(--dim)}
.role.candidate{border-color:var(--flag);color:var(--flag)}
.role.installed{background:var(--ink);color:var(--bg);border-color:var(--ink)}
.meta{font-size:.78rem;color:var(--dim);margin:0 0 .8rem}
.note{font-size:.86rem;color:var(--dim);max-width:44rem;margin:.1rem 0 1rem}
.word{margin:.2rem 0 .4rem;line-height:1.1}
.para{max-width:34rem;margin:0}
table{border-collapse:collapse;width:100%;font-size:.82rem;margin-top:.5rem}
th,td{text-align:right;padding:.4rem .5rem;border-bottom:1px solid var(--rule);font-variant-numeric:tabular-nums}
th:first-child,td:first-child{text-align:left}
th{color:var(--dim);font-weight:600}
tr.installed td{font-weight:600}
code{font-family:ui-monospace,Menlo,monospace;font-size:.85em}
.small{font-size:.82rem;color:var(--dim)}
</style>

<h1>Flared / semi-serif — the empty cell</h1>
<p class="sub">Triage before a bench. Generated by <code>tools/flared-bench/build_flared_page.py</code>.</p>

<p><a href="type-coverage.html">type-coverage.html</a> sorts the 37 curated families into 22 structural
classes and finds two of them empty. Typewriter was answered by IBM Plex. This is the other: the
glyphic-incised class, where the stem widens into its terminal instead of ending in a bracketed serif —
serif-like weight distribution, no actual serifs. The canonical text face is Optima.</p>

<div class="flag">
<p><b>Optima is not on this page, and cannot be.</b> It is Monotype's, there is no libre cut, and nothing
here is a metric clone of it. Every face below is an OFL face that reaches for the same idea from its own
direction. If the answer to this cell turns out to be "buy Optima", that is a legitimate outcome of the
bench — three of the catalogue's families are already commercial or webfont cuts that build from
<code>local_fonts/</code>.</p>
</div>

<div class="flag">
<p><b>A browser at 2x cannot answer the question this cell actually poses.</b> Every specimen below is
anti-aliased on a high-density display. The device renders 1-bit at an x-height of 12–18&nbsp;px, and the
faces that died in the previous two benches died exactly there — Archivo's <i>s</i> sealed its apertures
shut at 11&nbsp;px until the auto-hinter was forced on. A flared stem is a stem that gets <i>thinner</i>
before it flares, which is the worst possible shape to threshold. Expect this page to flatter every
candidate.</p>
</div>

<h2>Specimens, matched x-height</h2>
<p>Set so every face shows the same x-height, which is how <code>sans-candidates.yaml</code> and
<code>grotesque-candidates.yaml</code> were judged — comparing at equal point size compares nothing, because
a 0.42 and a 0.65 x-height at 16&nbsp;pt are different sizes of type. The four targets are the reader's own
slots. Colour, set width and how far the flare survives shrinking are the things to look at.</p>

<div class="controls">
  <label>x-height
    <button data-xh="12">12px</button><button data-xh="14">14px</button>
    <button data-xh="16" aria-pressed="true">16px</button><button data-xh="18">18px</button>
  </label>
  <label><input type="checkbox" id="hideControls"> candidates only</label>
  <label><input type="checkbox" id="eink"> e-ink cast</label>
</div>

<div id="specimens"></div>

<h2>Metrics</h2>
<p class="small">Straight from each font: x-height and cap height per em from <code>OS/2</code>, default
leading from <code>hhea</code>, designer from name ID 9 — never from memory, the rule
<code>docs/font-dates.md</code> already follows. "Set at" is the point size each face needs to hit the
selected x-height.</p>
<table id="metrics"><thead><tr>
<th>Face</th><th>Role</th><th>x-height/em</th><th>cap/em</th><th>default leading</th><th>upem</th><th>set at</th>
</tr></thead><tbody></tbody></table>
<p class="small" id="designers"></p>

<h2>What the bench still has to decide</h2>
<p>Nothing on this page is evidence about the device. If a face survives triage it goes into a
<code>flared-candidates.yaml</code> sibling of the other two and gets built, and only then can these be
answered:</p>
<ul>
<li>Does the flare survive 1-bit rendering at 12&nbsp;px x-height, or does it threshold away into a plain
sans — leaving a face that costs a slot and fills no cell?</li>
<li>Do the slots land? Every curated family hits x-height 12/14/16/18&nbsp;px on the hinted render. A face
whose hinting skips a pixel value cannot hold a slot, which is what forced
<code>force_autohint: true</code> on Archivo.</li>
<li>Set width against Libre Franklin at matched x-height. A small-x-height face like Forum has to be set
larger to match, and pays for it in characters per line.</li>
<li>Latin Extended-A coverage, and whether a real italic exists or only a slanted roman.</li>
</ul>

<script>
const FACES = /*DATA*/;
const SPECIMEN = "__SPECIMEN__", WORD = "__WORD__";
let targetXh = 16;

const elSpec = document.getElementById('specimens');
const elRows = document.querySelector('#metrics tbody');

// Point size that puts this face's x-height on the target. The whole comparison
// rests on this one division.
const sizeFor = f => targetXh / f.xh_em;

function render(){
  const hideControls = document.getElementById('hideControls').checked;
  const shown = FACES.filter(f => !(hideControls && f.role === 'control'));

  elSpec.innerHTML = shown.map(f => `
    <div class="face">
      <header>
        <h3>${f.label}</h3>
        <span class="role ${f.role}">${f.role}</span>
        <span class="meta">${f.designer} · x-height ${f.xh_em.toFixed(3)} em · set at ${sizeFor(f).toFixed(1)}px</span>
      </header>
      <p class="note">${f.note}</p>
      <div class="word" style="font-family:'${f.key}';font-size:${sizeFor(f)*2.4}px">${WORD}</div>
      <p class="para" style="font-family:'${f.key}';font-size:${sizeFor(f)}px;line-height:${f.lead_em.toFixed(3)}">${SPECIMEN}</p>
    </div>`).join('');

  elRows.innerHTML = FACES.map(f => `
    <tr class="${f.role}">
      <td>${f.label}</td><td>${f.role}</td>
      <td>${f.xh_em.toFixed(3)}</td>
      <td>${f.cap_em ? f.cap_em.toFixed(3) : '—'}</td>
      <td>${f.lead_em.toFixed(3)}</td>
      <td>${f.upem}</td>
      <td>${sizeFor(f).toFixed(1)}px</td>
    </tr>`).join('');
}

document.querySelectorAll('[data-xh]').forEach(b => b.onclick = () => {
  targetXh = +b.dataset.xh;
  document.querySelectorAll('[data-xh]').forEach(o => o.setAttribute('aria-pressed', o === b));
  render();
});
document.getElementById('hideControls').onchange = render;
document.getElementById('eink').onchange = e => {
  // Not a simulation of the panel — just removes the browser's colour and
  // contrast flattery so the shapes carry themselves.
  document.body.style.filter = e.target.checked ? 'grayscale(1) contrast(1.35)' : '';
};
render();
</script>
"""

if __name__ == "__main__":
    build()
