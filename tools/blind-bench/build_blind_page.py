import json

with open('candidates_data.json') as f:
    data = json.load(f)
with open('chrome_fonts.json') as f:
    chrome = json.load(f)
with open('blind_slots.json') as f:
    slots = json.load(f)

LETTERS = sorted(data.keys())
XH_TARGETS = [12, 14, 16, 18]

chrome_css = f"""
@font-face{{font-family:'Chrome Mono';src:url(data:font/woff2;base64,{chrome['regular']}) format('woff2');font-weight:400;font-style:normal;font-display:swap;}}
@font-face{{font-family:'Chrome Mono';src:url(data:font/woff2;base64,{chrome['bold']}) format('woff2');font-weight:700;font-style:normal;font-display:swap;}}
"""

# JS data: letter -> {images: {0:b64,1:b64,2:b64,3:b64}, family, displayName, sizes}
js_data = json.dumps(data)

html = f"""<title>Blind E-Ink Read &mdash; Serif Bench</title>
<style>
{chrome_css}

:root {{
  --paper: #F7F5F0;
  --paper-dim: #EEEBE3;
  --paper-card: #FFFFFE;
  --ink: #1C1B18;
  --ink-soft: #6B6558;
  --ink-faint: #9A9384;
  --accent: #2F6F5E;
  --accent-soft: rgba(47,111,94,0.12);
  --accent-ink: #FFFFFF;
  --rule: #DDD8CB;
  --rule-strong: #C7C1B1;
  --panel-edge: #B9B2A0;
  --radius: 3px;
  --shadow: 0 1px 2px rgba(28,27,24,0.06), 0 1px 1px rgba(28,27,24,0.04);
  color-scheme: light;
}}
@media (prefers-color-scheme: dark) {{
  :root {{
    --paper: #16150F; --paper-dim: #1E1C15; --paper-card: #201E17;
    --ink: #ECE7D9; --ink-soft: #A39C89; --ink-faint: #756E5D;
    --accent: #74C2A8; --accent-soft: rgba(116,194,168,0.16);
    --rule: #35321F; --rule-strong: #47432B; --panel-edge: #4A4633;
    --shadow: 0 1px 2px rgba(0,0,0,0.3);
    color-scheme: dark;
  }}
}}
:root[data-theme="dark"] {{
  --paper: #16150F; --paper-dim: #1E1C15; --paper-card: #201E17;
  --ink: #ECE7D9; --ink-soft: #A39C89; --ink-faint: #756E5D;
  --accent: #74C2A8; --accent-soft: rgba(116,194,168,0.16);
  --rule: #35321F; --rule-strong: #47432B; --panel-edge: #4A4633;
  --shadow: 0 1px 2px rgba(0,0,0,0.3);
  color-scheme: dark;
}}
:root[data-theme="light"] {{
  --paper: #F7F5F0; --paper-dim: #EEEBE3; --paper-card: #FFFFFE;
  --ink: #1C1B18; --ink-soft: #6B6558; --ink-faint: #9A9384;
  --accent: #2F6F5E; --accent-soft: rgba(47,111,94,0.12);
  --rule: #DDD8CB; --rule-strong: #C7C1B1; --panel-edge: #B9B2A0;
  --shadow: 0 1px 2px rgba(28,27,24,0.06), 0 1px 1px rgba(28,27,24,0.04);
  color-scheme: light;
}}

* {{ box-sizing: border-box; }}
html, body {{ margin: 0; padding: 0; }}
body {{
  background: var(--paper); color: var(--ink);
  font: 15px/1.5 ui-sans-serif, system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
  -webkit-font-smoothing: antialiased;
}}
@media (prefers-reduced-motion: no-preference) {{
  .card {{ transition: border-color .15s ease; }}
  button.action {{ transition: background .12s ease, border-color .12s ease; }}
}}

.page {{ max-width: 1180px; margin: 0 auto; padding: 40px 24px 96px; }}

.eyebrow {{
  font-family: 'Chrome Mono', ui-monospace, monospace;
  font-size: 11px; font-weight: 700; letter-spacing: 0.14em; text-transform: uppercase;
  color: var(--accent); margin: 0 0 10px;
}}
h1 {{
  font-family: ui-serif, Georgia, "Times New Roman", serif;
  font-size: clamp(28px, 4vw, 40px); font-weight: 600; letter-spacing: -0.01em;
  margin: 0 0 14px; text-wrap: balance;
}}
.lede {{ max-width: 68ch; color: var(--ink-soft); font-size: 15px; margin: 0 0 10px; }}
.lede strong {{ color: var(--ink); font-weight: 600; }}
.lede code {{ font-family: 'Chrome Mono', monospace; background: var(--paper-dim); padding: 1px 4px; border-radius: 2px; font-size: 0.9em; }}

.warn {{
  display: flex; gap: 8px; align-items: flex-start;
  background: var(--paper-dim); border: 1px solid var(--rule-strong);
  border-radius: var(--radius); padding: 10px 12px; margin: 16px 0 24px;
  font-size: 12.5px; color: var(--ink-soft); max-width: 68ch;
}}

.toolbar {{
  display: flex; flex-wrap: wrap; align-items: center; gap: 14px;
  margin-bottom: 22px; padding-bottom: 18px; border-bottom: 1px solid var(--rule);
  position: sticky; top: 0; background: var(--paper); z-index: 10; padding-top: 4px;
}}
.slot-tabs {{ display: inline-flex; border: 1px solid var(--rule-strong); border-radius: var(--radius); overflow: hidden; }}
.slot-tabs button {{
  font: 700 12px 'Chrome Mono', ui-monospace, monospace;
  padding: 8px 13px; border: none; background: var(--paper-card); color: var(--ink-soft);
  cursor: pointer; border-right: 1px solid var(--rule-strong);
}}
.slot-tabs button:last-child {{ border-right: none; }}
.slot-tabs button.active {{ background: var(--accent); color: var(--accent-ink); }}
.slot-tabs button:hover:not(.active) {{ background: var(--paper-dim); }}
.spacer {{ flex: 1 1 auto; }}
button.action {{
  font: 700 12.5px 'Chrome Mono', ui-monospace, monospace; letter-spacing: 0.03em;
  padding: 9px 14px; border-radius: var(--radius); border: 1px solid var(--rule-strong);
  background: var(--paper-card); color: var(--ink); cursor: pointer;
}}
button.action:hover {{ border-color: var(--ink-faint); }}
button.action.primary {{ background: var(--accent); border-color: var(--accent); color: var(--accent-ink); }}
button.action.primary:hover {{ opacity: 0.92; }}
button.action:disabled {{ opacity: 0.4; cursor: not-allowed; }}

.pickcount {{
  font-family: 'Chrome Mono', monospace; font-size: 12px; color: var(--ink-faint);
}}
.pickcount b {{ color: var(--accent); font-variant-numeric: tabular-nums; }}

.grid {{
  display: grid; grid-template-columns: repeat(auto-fill, minmax(300px, 1fr)); gap: 14px;
}}

.card {{
  border: 1px solid var(--rule); border-radius: var(--radius); background: var(--paper-card);
  box-shadow: var(--shadow); overflow: hidden; display: flex; flex-direction: column;
}}
.card.picked {{ border-color: var(--accent); box-shadow: 0 0 0 1px var(--accent), var(--shadow); }}

.card-head {{
  display: flex; align-items: center; justify-content: space-between; gap: 8px;
  padding: 10px 12px; border-bottom: 1px solid var(--rule); background: var(--paper-dim);
}}
.card-letter {{
  font-family: ui-serif, Georgia, serif; font-size: 20px; font-weight: 600;
  min-width: 1.6em;
}}
.card-name {{ font-family: 'Chrome Mono', monospace; font-size: 11.5px; color: var(--ink-soft); text-align: right; }}
.card-name .real {{ color: var(--ink); font-weight: 700; }}

.pick-btn {{
  display: inline-flex; align-items: center; gap: 5px; cursor: pointer; user-select: none;
  font: 700 11px 'Chrome Mono', monospace; padding: 5px 9px; border-radius: 999px;
  border: 1px solid var(--rule-strong); background: var(--paper-card); color: var(--ink-soft);
}}
.pick-btn.on {{ background: var(--accent); border-color: var(--accent); color: var(--accent-ink); }}
.pick-btn input {{ display: none; }}

.specimen-wrap {{
  background: var(--paper); border: 1px solid var(--panel-edge); border-radius: 2px;
  margin: 12px; overflow: hidden;
}}
.specimen-wrap img {{ display: block; width: 100%; height: auto; }}

.card-foot {{ padding: 0 12px 12px; }}
.card-foot textarea {{
  width: 100%; min-height: 44px; resize: vertical; font: 12px 'Chrome Mono', monospace;
  color: var(--ink); background: var(--paper-dim); border: 1px solid var(--rule);
  border-radius: var(--radius); padding: 7px 8px;
}}
.card-foot textarea::placeholder {{ color: var(--ink-faint); }}

.export-panel {{
  margin-top: 24px; border: 1px solid var(--rule-strong); border-radius: var(--radius);
  background: var(--paper-card); padding: 14px;
}}
.export-panel.hidden {{ display: none; }}
.export-panel h2 {{
  font-family: 'Chrome Mono', monospace; font-size: 11px; text-transform: uppercase;
  letter-spacing: 0.08em; margin: 0 0 10px; color: var(--ink-soft);
}}
.export-panel textarea {{
  width: 100%; min-height: 130px; resize: vertical; font: 12.5px 'Chrome Mono', monospace;
  color: var(--ink); background: var(--paper-dim); border: 1px solid var(--rule);
  border-radius: var(--radius); padding: 10px; margin-bottom: 10px;
}}

footer.notes {{
  margin-top: 40px; padding-top: 18px; border-top: 1px solid var(--rule);
  color: var(--ink-faint); font-size: 12px; line-height: 1.7; max-width: 72ch;
}}
footer.notes code {{ font-family: 'Chrome Mono', monospace; background: var(--paper-dim); padding: 1px 4px; border-radius: 2px; }}
</style>

<div class="page">
  <header>
    <p class="eyebrow">CrossPoint Reader &middot; SD-card font bake-off</p>
    <h1>Blind e-ink read</h1>
    <p class="lede">
      Round 4: down to the final 4 &mdash; Spectral, Antykwa Poltawskiego and Merriweather Light
      didn't make round 3's cut &mdash; rendered through the
      <strong>real firmware</strong> &mdash; actual <code>GfxRenderer</code> /
      <code>EpdFont</code> / <code>SdCardFont</code>, via
      <code>tools/calendar_preview/render_harness reading</code> (same rig as the sans-serif and
      grotesque benches). Sizes are swept per family to the tier's own uniform slots
      &mdash; a {XH_TARGETS[0]}/{XH_TARGETS[1]}/{XH_TARGETS[2]}/{XH_TARGETS[3]}px x-height ladder,
      not a shared point size, so what differs between two specimens is the letterforms, not the size.
      Letters reshuffled again &mdash; A&ndash;G no longer match round 2.
    </p>
    <p class="lede">
      Letters, not names. Pick without knowing which is which; reveal when you're done.
    </p>
    <div class="warn">
      Client-side blind, not cryptographic: the name mapping ships in this page's own
      JavaScript, so it's an honor-system blind test (don't open dev tools). Good enough
      for judging alone; not for a study with stakes.
    </div>
  </header>

  <div class="toolbar">
    <div class="slot-tabs" id="slotTabs">
      <button data-slot="0" class="active">{XH_TARGETS[0]}px x-height</button>
      <button data-slot="1">{XH_TARGETS[1]}px x-height</button>
      <button data-slot="2">{XH_TARGETS[2]}px x-height</button>
      <button data-slot="3">{XH_TARGETS[3]}px x-height</button>
    </div>
    <span class="pickcount"><b id="pickCount">0</b> picked</span>
    <div class="spacer"></div>
    <button class="action" id="revealBtn">Reveal names</button>
    <button class="action primary" id="exportBtn">Export</button>
  </div>

  <div class="grid" id="grid"></div>

  <div class="export-panel hidden" id="exportPanel">
    <h2 id="exportHeading">Picks (letters only &mdash; not revealed yet)</h2>
    <textarea id="exportText" readonly></textarea>
    <button class="action" id="copyBtn">Copy to clipboard</button>
    <span id="copyStatus" style="font-family:'Chrome Mono',monospace;font-size:11.5px;color:var(--ink-soft);margin-left:8px;"></span>
  </div>

  <footer class="notes">
    Pipeline: <code>tools/blind-bench/sweep_blind.py</code> measures each family's hinted
    x-height ramp and solves leading exactly as <code>tools/sans-bench/sweep_sans.py</code>
    does &mdash; see that script for the full derivation. Recipes in
    <code>tools/blind-bench/blind-candidates.yaml</code>, built via
    <code>lib/EpdFont/scripts/build-sd-fonts.py</code>, rendered via
    <code>render_harness reading &lt;Family&gt;</code> against <code>fs_/.fonts/</code>. Each
    specimen's own debug header (which named the family) is cropped off before display &mdash;
    the crop line is fixed by pixel measurement (row 50), the same for every candidate, since
    the header itself is drawn in the constant UI font regardless of family.
    Merriweather Light is measured on its own weight file; its bold/bold-italic borrow the base
    family's cut (no separate light-weight bold exists), which barely matters here since this
    reading specimen never draws bold-italic text. Edgar is the one exception to the rebuild
    pipeline: it's a commercial Frere-Jones/St&ouml;ssinger cut with no free source, so this round
    reuses the already-shipping <code>Edgar_{{12,14,16,18}}.cpfont</code> straight from the device
    font set rather than rebuilding from <code>lib/EpdFont/local_fonts/</code> &mdash; same uniform
    12/14/16/18px x-height slots either way, so it sits fairly next to the rest. This is a bench,
    not a shipping list &mdash; nothing here touches <code>sd-fonts.yaml</code>'s
    <code>installed_families:</code>.
  </footer>
</div>

<script>
const DATA = {js_data};
const LETTERS = {json.dumps(LETTERS)};

const STORAGE_KEY = 'crosspoint-blind-bench-v1';
function loadState() {{
  try {{ return JSON.parse(localStorage.getItem(STORAGE_KEY) || '{{}}'); }}
  catch (e) {{ return {{}}; }}
}}
function saveState(s) {{ try {{ localStorage.setItem(STORAGE_KEY, JSON.stringify(s)); }} catch (e) {{}} }}
let state = loadState();
if (!state.picks) state.picks = {{}};
if (!state.notes) state.notes = {{}};
if (!state.revealed) state.revealed = false;

let currentSlot = 0;
const grid = document.getElementById('grid');

function escapeHtml(s) {{
  return String(s).replace(/[&<>"']/g, c => ({{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}})[c]);
}}

function makeCard(letter) {{
  const c = DATA[letter];
  const card = document.createElement('article');
  card.className = 'card';
  card.dataset.letter = letter;

  card.innerHTML = `
    <div class="card-head">
      <div class="card-letter">${{letter}}</div>
      <label class="pick-btn">
        <input type="checkbox" ${{state.picks[letter] ? 'checked' : ''}}>
        <span class="pick-label">Keep</span>
      </label>
      <div class="card-name" data-name-slot></div>
    </div>
    <div class="specimen-wrap"><img data-img alt="Candidate ${{letter}} specimen"></div>
    <div class="card-foot">
      <textarea data-note placeholder="Notes (optional)&hellip;">${{escapeHtml(state.notes[letter] || '')}}</textarea>
    </div>
  `;

  const cb = card.querySelector('.pick-btn input');
  const pickBtn = card.querySelector('.pick-btn');
  if (state.picks[letter]) {{ card.classList.add('picked'); pickBtn.classList.add('on'); }}
  cb.addEventListener('change', () => {{
    if (cb.checked) {{ state.picks[letter] = true; card.classList.add('picked'); pickBtn.classList.add('on'); }}
    else {{ delete state.picks[letter]; card.classList.remove('picked'); pickBtn.classList.remove('on'); }}
    saveState(state);
    updatePickCount();
  }});

  const note = card.querySelector('[data-note]');
  note.addEventListener('input', () => {{
    state.notes[letter] = note.value;
    saveState(state);
  }});

  return card;
}}

LETTERS.forEach(l => grid.appendChild(makeCard(l)));

function renderSlot() {{
  LETTERS.forEach(letter => {{
    const card = grid.querySelector(`[data-letter="${{letter}}"]`);
    const img = card.querySelector('[data-img]');
    img.src = 'data:image/png;base64,' + DATA[letter].images[currentSlot];
  }});
}}
renderSlot();

function renderNames() {{
  LETTERS.forEach(letter => {{
    const card = grid.querySelector(`[data-letter="${{letter}}"]`);
    const slot = card.querySelector('[data-name-slot]');
    if (state.revealed) {{
      slot.innerHTML = `<span class="real">${{escapeHtml(DATA[letter].displayName)}}</span>`;
    }} else {{
      slot.textContent = '';
    }}
  }});
}}
renderNames();

document.getElementById('slotTabs').addEventListener('click', e => {{
  const btn = e.target.closest('button[data-slot]');
  if (!btn) return;
  currentSlot = parseInt(btn.dataset.slot, 10);
  document.querySelectorAll('#slotTabs button').forEach(b => b.classList.toggle('active', b === btn));
  renderSlot();
}});

function updatePickCount() {{
  document.getElementById('pickCount').textContent = Object.keys(state.picks).length;
}}
updatePickCount();

document.getElementById('revealBtn').addEventListener('click', () => {{
  if (state.revealed) return;
  if (!confirm('Reveal which candidate is which? This ends the blind part.')) return;
  state.revealed = true;
  saveState(state);
  renderNames();
  document.getElementById('revealBtn').textContent = 'Revealed';
  document.getElementById('revealBtn').disabled = true;
}});
if (state.revealed) {{
  document.getElementById('revealBtn').textContent = 'Revealed';
  document.getElementById('revealBtn').disabled = true;
}}

const exportPanel = document.getElementById('exportPanel');
document.getElementById('exportBtn').addEventListener('click', () => {{
  const lines = [];
  LETTERS.forEach(letter => {{
    if (!state.picks[letter]) return;
    const label = state.revealed ? DATA[letter].displayName : `Candidate ${{letter}}`;
    const note = state.notes[letter] ? `  -- ${{state.notes[letter]}}` : '';
    lines.push(`${{label}}${{note}}`);
  }});
  document.getElementById('exportHeading').textContent =
    state.revealed ? 'Picks (revealed)' : 'Picks (letters only \\u2014 not revealed yet)';
  document.getElementById('exportText').value = lines.length ? lines.join('\\n') : '(nothing picked yet)';
  exportPanel.classList.remove('hidden');
  exportPanel.scrollIntoView({{ behavior: 'smooth', block: 'nearest' }});
}});
document.getElementById('copyBtn').addEventListener('click', async () => {{
  const ta = document.getElementById('exportText');
  ta.select();
  const status = document.getElementById('copyStatus');
  try {{ await navigator.clipboard.writeText(ta.value); status.textContent = 'Copied.'; }}
  catch (e) {{ document.execCommand('copy'); status.textContent = 'Copied.'; }}
  setTimeout(() => status.textContent = '', 2000);
}});
</script>
"""

with open('out_blind.html', 'w') as f:
    f.write(html)

print(f"written out_blind.html ({len(html)/1024/1024:.2f} MB)")
