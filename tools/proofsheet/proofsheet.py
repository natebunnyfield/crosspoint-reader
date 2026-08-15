#!/usr/bin/env python3
"""Parametric font proof sheet — sweep .cpfont build parameters, render each
variant through the REAL renderer, and emit one self-contained HTML page with
in-place blink comparison.

Why this exists: the Inknut italic was settled over ~40 chat turns of
one-parameter-at-a-time bracketing ("test five steps between 545 and 575"),
each round hand-building variant families (IJ15W545, IJ15W590, ...) and
hand-assembling comparison pages. This tool is that loop, run once:

    tools/proofsheet/proofsheet.py \
        --family InknutJunicode \
        --vary styles.italic.variable.wght=545,565,590 \
        --vary styles.italic.scale=1.15,1.2

builds the cartesian product (6 variants here), renders each with
`render_harness inline <variant>`, and writes proofsheet.html where the
variants sit STACKED in one viewport — arrow keys / number keys flip between
them in place, which is the only comparison that shows a 0.1 px stem move
(side-by-side never does; the yaml's round-16 notes explain why: adjacent
steps flicker rather than accumulate).

Vary paths are dotted keys into the family's sd-fonts.yaml entry:
    styles.italic.variable.wght     styles.italic.scale
    styles.italic.word_space_em     styles.bolditalic.synthetic.embolden_em
    metrics.ascent                  sizes            (use ; to separate lists)

Prereqs: tools/calendar_preview/render_harness built (./build.sh there),
PyYAML, macOS `sips` for BMP->PNG. Variant families are built under unique
names (<Family>V1..Vn) in tools/calendar_preview/fs_/.fonts/ and deleted
afterwards unless --keep is given. Downloads hit build-sd-fonts.py's normal
source cache, so repeat sweeps do not re-fetch fonts.
"""

import argparse
import base64
import copy
import html as html_mod
import itertools
import json
import os
import shutil
import subprocess
import sys
import tempfile

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
YAML_PATH = os.path.join(REPO, "lib", "EpdFont", "scripts", "sd-fonts.yaml")
BUILD_SCRIPT = os.path.join(REPO, "lib", "EpdFont", "scripts", "build-sd-fonts.py")
HARNESS_DIR = os.path.join(REPO, "tools", "calendar_preview")
HARNESS = os.path.join(HARNESS_DIR, "render_harness")
FONT_ROOT = os.path.join(HARNESS_DIR, "fs_", ".fonts")


def parse_value(tok):
    tok = tok.strip()
    for cast in (int, float):
        try:
            return cast(tok)
        except ValueError:
            pass
    return tok


def deep_set(entry, dotted, value):
    keys = dotted.split(".")
    node = entry
    for k in keys[:-1]:
        if k not in node or not isinstance(node[k], dict):
            node[k] = {}
        node = node[k]
    node[keys[-1]] = value


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--family", required=True, help="family name in sd-fonts.yaml")
    ap.add_argument("--vary", action="append", default=[], metavar="PATH=V1,V2,...",
                    help="dotted yaml path + comma list of values; repeatable, "
                         "combos are the cartesian product")
    ap.add_argument("--mode", default="inline", choices=["inline", "reading"],
                    help="render_harness mode (default: inline)")
    ap.add_argument("--slots", default="0,1,2,3",
                    help="ordinal size slots to include in the page")
    ap.add_argument("--intervals", default=None,
                    help="override the family's intervals preset (faster sweeps)")
    ap.add_argument("--out", default=os.path.join(HERE, "proofsheet.html"))
    ap.add_argument("--keep", action="store_true",
                    help="keep the variant font dirs in fs_/.fonts")
    ap.add_argument("--max-variants", type=int, default=12)
    args = ap.parse_args()

    if not os.path.exists(HARNESS):
        sys.exit(f"render_harness not built — run {HARNESS_DIR}/build.sh first")

    with open(YAML_PATH) as f:
        cfg = yaml.safe_load(f)
    base_entry = next((fam for fam in cfg["families"] if fam["name"] == args.family), None)
    if base_entry is None:
        sys.exit(f"family {args.family!r} not in sd-fonts.yaml")

    axes = []  # (dotted_path, [values])
    for spec in args.vary:
        path, _, vals = spec.partition("=")
        if not vals:
            sys.exit(f"--vary needs PATH=V1,V2,... got {spec!r}")
        axes.append((path, [parse_value(v) for v in vals.split(",")]))
    if not axes:
        sys.exit("give at least one --vary")

    combos = list(itertools.product(*[vals for _, vals in axes]))
    if len(combos) > args.max_variants:
        sys.exit(f"{len(combos)} variants > --max-variants {args.max_variants} — "
                 "narrow the sweep; a proof sheet you can't flip through is a poster")

    variants = []  # {name, params: {path: value}}
    for i, combo in enumerate(combos, 1):
        entry = copy.deepcopy(base_entry)
        name = f"{args.family}V{i}"
        entry["name"] = name
        params = {}
        for (path, _), value in zip(axes, combo):
            deep_set(entry, path, value)
            params[path] = value
        if args.intervals:
            entry["intervals"] = args.intervals
        variants.append({"name": name, "entry": entry, "params": params})

    sweep_cfg = dict(cfg)
    sweep_cfg["families"] = [v["entry"] for v in variants]
    sweep_cfg.pop("installed_families", None)

    tmp_yaml = tempfile.NamedTemporaryFile(
        "w", suffix=".yaml", prefix="proofsheet-", delete=False)
    yaml.safe_dump(sweep_cfg, tmp_yaml, sort_keys=False)
    tmp_yaml.close()

    built_dirs = []
    try:
        only = ",".join(v["name"] for v in variants)
        print(f"building {len(variants)} variant(s): {only}")
        r = subprocess.run(
            [sys.executable, BUILD_SCRIPT, "--config", tmp_yaml.name,
             "--only", only, "--output-dir", FONT_ROOT],
            capture_output=True, text=True)
        sys.stdout.write(r.stdout[-2000:])
        if r.returncode != 0:
            sys.stderr.write(r.stderr[-2000:])
            sys.exit("build-sd-fonts.py failed")
        for v in variants:
            d = os.path.join(FONT_ROOT, v["name"])
            if not os.path.isdir(d):
                sys.exit(f"expected build output missing: {d}")
            built_dirs.append(d)

        slots = [s.strip() for s in args.slots.split(",") if s.strip()]
        shots = {}  # (variant, slot) -> png path
        with tempfile.TemporaryDirectory(prefix="proofsheet-png-") as png_dir:
            for v in variants:
                print(f"rendering {v['name']} ({args.mode})")
                r = subprocess.run([HARNESS, args.mode, v["name"]],
                                   cwd=HARNESS_DIR, capture_output=True, text=True)
                if r.returncode != 0:
                    sys.stderr.write(r.stdout[-800:] + r.stderr[-800:])
                    sys.exit(f"render_harness failed for {v['name']}")
                for slot in slots:
                    bmp = os.path.join(HARNESS_DIR, "fs_",
                                       f"{args.mode}_{v['name']}_{slot}.bmp")
                    if not os.path.exists(bmp):
                        sys.exit(f"no output {bmp} — does mode {args.mode!r} "
                                 f"emit per-slot files?")
                    png = os.path.join(png_dir, f"{v['name']}_{slot}.png")
                    subprocess.run(["sips", "-s", "format", "png", bmp,
                                    "--out", png], check=True,
                                   capture_output=True)
                    os.remove(bmp)
                    shots[(v["name"], slot)] = png

            write_page(args, axes, variants, slots, shots)
        print(f"wrote {args.out}")
    finally:
        os.unlink(tmp_yaml.name)
        if not args.keep:
            for d in built_dirs:
                shutil.rmtree(d, ignore_errors=True)


def write_page(args, axes, variants, slots, shots):
    data = {}
    for v in variants:
        imgs = {}
        for slot in slots:
            with open(shots[(v["name"], slot)], "rb") as f:
                imgs[slot] = base64.b64encode(f.read()).decode()
        label = "  ".join(f"{p.split('.')[-2]}.{p.split('.')[-1]}={val}"
                          if p.count(".") else f"{p}={val}"
                          for p, val in v["params"].items())
        data[v["name"]] = {"label": label, "imgs": imgs}

    page = f"""<!doctype html><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Proof sheet — {html_mod.escape(args.family)}</title>
<style>
 body{{margin:0;font:13px/1.4 -apple-system,sans-serif;background:#111;color:#ddd;
      padding-bottom:env(safe-area-inset-bottom)}}
 header{{position:sticky;top:0;background:#111;z-index:2;
        padding:8px 12px 6px;border-bottom:1px solid #2a2a2a}}
 header b{{color:#fff;font-size:15px;margin-right:10px}}
 #label{{color:#8fd;font-family:ui-monospace,monospace;font-size:12px;
        display:block;margin-top:2px;word-break:break-all}}
 .chips{{display:flex;gap:6px;flex-wrap:wrap;margin-top:6px}}
 .chip{{background:#2a2a2a;color:#bbb;border:0;border-radius:14px;
       padding:5px 12px;font:inherit;font-size:12px;touch-action:manipulation}}
 .chip.on{{background:#8fd;color:#111;font-weight:600}}
 #stage{{position:relative;background:#fff;margin:0 auto;width:fit-content;
        max-width:100%;touch-action:pan-y pinch-zoom;
        -webkit-user-select:none;user-select:none;-webkit-touch-callout:none}}
 #stage img{{display:block;image-rendering:pixelated;max-width:100vw;height:auto}}
 #stage img.hidden{{display:none}}
 nav{{padding:6px 12px;color:#888;font-size:11px}}
 nav kbd{{background:#333;border-radius:3px;padding:1px 4px;margin:0 1px}}
 .grid #stage{{display:flex;flex-wrap:wrap;gap:8px;background:#111}}
 .grid #stage img{{display:block!important;max-width:46vw;height:auto}}
 @media (pointer:coarse){{ .kbd-only{{display:none}} }}
</style>
<header><b>{html_mod.escape(args.family)}</b>
 <span id="which"></span> <span id="slotlbl"></span>
 <span id="label"></span>
 <div class="chips" id="vchips"></div>
 <div class="chips" id="schips"></div>
</header>
<nav>tap right/left of specimen: next/prev &middot; press-and-hold: blink
 against previous variant &middot; chips select directly
 <span class="kbd-only">&middot; <kbd>&larr;</kbd><kbd>&rarr;</kbd>/<kbd>1</kbd>-<kbd>9</kbd>
 variant &middot; <kbd>&uarr;</kbd><kbd>&darr;</kbd> slot &middot; <kbd>g</kbd> grid</span></nav>
<div id="stage"></div>
<script>
const DATA={json.dumps(data)};
const NAMES=Object.keys(DATA), SLOTS={json.dumps(slots)};
let vi=0, lastVi=null, si=0, grid=false;
const stage=document.getElementById('stage');
const vchips=document.getElementById('vchips'), schips=document.getElementById('schips');

function setVi(n){{ if(n===vi) return; lastVi=vi; vi=n; show(); }}

function build(){{
  stage.innerHTML='';
  for(const n of NAMES){{
    const img=document.createElement('img');
    img.src='data:image/png;base64,'+DATA[n].imgs[SLOTS[si]];
    img.id='img-'+n;
    stage.appendChild(img);
  }}
  vchips.innerHTML='';
  NAMES.forEach((n,i)=>{{
    const b=document.createElement('button');
    b.className='chip'; b.textContent=(i+1);
    b.onclick=()=>setVi(i);
    vchips.appendChild(b);
  }});
  const g=document.createElement('button');
  g.className='chip'; g.textContent='grid';
  g.onclick=()=>{{grid=!grid; show();}};
  vchips.appendChild(g);
  schips.innerHTML='';
  SLOTS.forEach((s,i)=>{{
    const b=document.createElement('button');
    b.className='chip'; b.textContent='slot '+s;
    b.onclick=()=>{{si=i; build();}};
    schips.appendChild(b);
  }});
  show();
}}
function show(showVi){{
  const cur = showVi===undefined ? vi : showVi;
  NAMES.forEach((n,i)=>{{
    document.getElementById('img-'+n).classList.toggle('hidden', !grid && i!==cur);
  }});
  document.getElementById('which').textContent=(cur+1)+'/'+NAMES.length+' '+NAMES[cur];
  document.getElementById('label').textContent=DATA[NAMES[cur]].label;
  document.getElementById('slotlbl').textContent='slot '+SLOTS[si];
  document.body.classList.toggle('grid', grid);
  [...vchips.children].forEach((c,i)=>c.classList.toggle('on', i===cur));
  [...schips.children].forEach((c,i)=>c.classList.toggle('on', i===si));
}}

// Touch: tap right 60% = next, left 40% = prev; press-and-hold >=300ms blinks
// against the previously viewed variant; vertical movement = scroll, cancels.
let pDown=null, holdTimer=null, holding=false;
stage.addEventListener('pointerdown',e=>{{
  if(grid) return;
  pDown={{x:e.clientX,y:e.clientY,t:Date.now()}};
  holding=false;
  holdTimer=setTimeout(()=>{{
    if(pDown && lastVi!==null){{ holding=true; show(lastVi); }}
  }},300);
}});
stage.addEventListener('pointermove',e=>{{
  if(!pDown) return;
  if(Math.abs(e.clientX-pDown.x)>12 || Math.abs(e.clientY-pDown.y)>12){{
    clearTimeout(holdTimer);
    if(holding){{holding=false; show();}}
    pDown=null;
  }}
}});
function pointerEnd(e){{
  clearTimeout(holdTimer);
  if(!pDown){{ if(holding){{holding=false; show();}} return; }}
  const wasHold=holding; holding=false;
  const r=stage.getBoundingClientRect();
  pDown=null;
  if(wasHold){{ show(); return; }}
  if(grid) return;
  if(e.type==='pointercancel') return;
  const frac=(e.clientX-r.left)/r.width;
  setVi(frac>=0.4 ? (vi+1)%NAMES.length : (vi+NAMES.length-1)%NAMES.length);
}}
stage.addEventListener('pointerup',pointerEnd);
stage.addEventListener('pointercancel',pointerEnd);

addEventListener('keydown',e=>{{
  if(e.key==='ArrowRight') setVi((vi+1)%NAMES.length);
  else if(e.key==='ArrowLeft') setVi((vi+NAMES.length-1)%NAMES.length);
  else if(e.key==='ArrowDown'){{si=(si+1)%SLOTS.length; build(); return;}}
  else if(e.key==='ArrowUp'){{si=(si+SLOTS.length-1)%SLOTS.length; build(); return;}}
  else if(e.key==='g'){{grid=!grid; show();}}
  else if(/^[1-9]$/.test(e.key) && +e.key<=NAMES.length) setVi(+e.key-1);
  else return;
  e.preventDefault();
}});
build();
</script>"""
    with open(args.out, "w") as f:
        f.write(page)


if __name__ == "__main__":
    main()
