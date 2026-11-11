#!/usr/bin/env python3
"""Assertions for tools/latency/gate.sh over a [lat] trace.

Each check names the finding it guards in docs/lag-reduction-plan-2026-11-07.md.
Exit 1 with one line per failure; exit 0 with a one-line summary.
"""
import re
import sys

WINDOW = re.compile(r'Present windowed to \d+ changed tiles: (\d+),(\d+) (\d+)x(\d+)')
LAT = re.compile(r'\[lat\] (\d+) (.+?) (-?\d+) opens=(\d+) reads=(\d+) rbytes=(\d+) seeks=(\d+) writes=(\d+)')
PANEL_AREA = 480 * 800
BTN = {0: 'BACK', 1: 'CONFIRM', 2: 'LEFT', 3: 'RIGHT', 4: 'UP', 5: 'DOWN'}

presses = []  # one dict per btn-down
activity = None
prev_fb = None
failures = []
for line in open(sys.argv[1], errors='replace'):
    # Not a lag finding, but this run is the one place every boot is read:
    # the settings list's reserve count has drifted five times unnoticed.
    if 'FIXED_ENTRY_COUNT is' in line:
        failures.append('settings: ' + line.strip().split('] ', 3)[-1])
    if 'Entering activity: ' in line:
        activity = line.split('Entering activity: ')[1].strip()
        if presses:
            presses[-1]['entered'] = activity
        continue
    if 'Cache not found, building' in line and presses:
        presses[-1]['building'] = True
    w = WINDOW.search(line)
    if w and presses:
        presses[-1]['windows'].append(int(w[3]) * int(w[4]))
    m = LAT.match(line)
    if not m:
        continue
    t, what, arg = int(m[1]) / 1000.0, m[2], int(m[3])
    reads = int(m[5])
    if what == 'btn-down':
        presses.append({'button': BTN.get(arg, arg), 'down': t, 'up': None, 'calls': [], 'reads': 0,
                        'activity': activity, 'entered': None, 'building': False, 'windows': []})
        continue
    if not presses:
        continue
    p = presses[-1]
    if what == 'btn-up':
        p['up'] = t
        continue
    if what.startswith('gray-strip'):
        continue
    p['reads'] += reads
    if what.startswith('refresh'):
        fb = what.split('fb=')[1]
        if fb == prev_fb:
            failures.append(f"F2: SAME-FRAME refresh at {t:.0f} ms after {p['button']} ({p['activity']})")
        prev_fb = fb
        p['calls'].append(('refresh', t))
    else:
        p['calls'].append((what, t))

turns = [p for p in presses if p['button'] == 'RIGHT' and p['activity'] == 'EpubReader']
if len(turns) < 10:
    failures.append(f"gate: only {len(turns)} page turns reached the reader; the script did not run as written")
for i, p in enumerate(turns):
    first = next((c for c in p['calls'] if c[0] == 'refresh'), None)
    if first is None:
        failures.append(f"turn {i + 1}: no panel refresh")
        continue
    # F1: the turn fires on the press, not the release.
    if p['up'] is not None and first[1] >= p['up']:
        failures.append(f"F1: turn {i + 1} refreshed {first[1] - p['down']:.0f} ms after the press, after the release")
    # F5: a page load is a handful of buffered reads, not one per field.
    if not p['building'] and i > 0 and p['reads'] > 16:
        failures.append(f"F5: turn {i + 1} made {p['reads']} storage reads (limit 16)")
fast = turns[6:]
for i, p in enumerate(fast[:-1]):
    if any(c[0] == 'gray-display' for c in p['calls']):
        failures.append(f"2.2: rapid turn {i + 7} ran a gray pass before the next press")
slow = turns[1:6]
if slow and not any(any(c[0] == 'gray-display' for c in p['calls']) for p in slow):
    failures.append("2.2: no rested page got its deferred gray pass")
home = [p for p in presses if p['entered'] == 'Home']
for p in home:
    n = sum(1 for c in p['calls'] if c[0] == 'refresh')
    if n > 2:
        failures.append(f"F2/F3: entering Home cost {n} panel refreshes (limit 2)")
settings_moves = [p for p in presses if p['activity'] == 'Settings' and p['button'] == 'RIGHT']
for p in settings_moves:
    first = next((c for c in p['calls'] if c[0] == 'refresh'), None)
    if first and p['up'] is not None and first[1] >= p['up']:
        failures.append("3.3: a Settings row move fired on the release")
    # 2.1: a row move inside a page changes two rows, which the tile diff
    # presents as a narrow window (160x480 in this run). A list that repaints
    # every row on a step -- a scrolling list at its edge, a re-laid-out list,
    # a changed font or band -- presents the whole panel instead. Guards the
    # FUI list base (upstream 9e3cea2e5), whose rows Lyra Six paints.
    if first and not any(area <= PANEL_AREA // 2 for area in p['windows']):
        failures.append(f"2.1: a Settings row move was not a windowed present (windows {p['windows']})")

if failures:
    print("\n".join(failures))
    sys.exit(1)
print(f"lag gate OK: {len(turns)} turns, {len(home)} Home entries, {len(settings_moves)} Settings moves")
