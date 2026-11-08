#!/usr/bin/env python3
"""One line per press: when the action fired (relative to the press), every
panel call it caused, the storage traffic, and SAME-FRAME on a refresh whose
framebuffer hash equals the previous refresh's (a waveform that changes
nothing). Reads the [lat] lines from docs/data/sim-latency-trace-2026-11-07.patch."""
import re, sys
NAMES = {0: 'BACK', 1: 'CONFIRM', 2: 'LEFT', 3: 'RIGHT', 4: 'UP', 5: 'DOWN'}
MODES = ['FULL', 'HALF', 'FAST']
LAT = re.compile(r'\[lat\] (\d+) (.+?) (-?\d+) opens=(\d+) reads=(\d+) rbytes=(\d+) seeks=(\d+) writes=(\d+)')
base, prev_fb, strips = 0.0, None, 0
for line in open(sys.argv[1], errors='replace'):
    m = LAT.match(line)
    if not m:
        if 'Entering activity' in line:
            print(' [' + line.split('Entering activity: ')[1].strip() + ']', end='')
        continue
    t, what, arg = int(m[1]) / 1000, m[2], int(m[3])
    io = f"io={m[4]}o/{m[5]}r/{m[6]}B/{m[7]}s/{m[8]}w"
    if what == 'btn-down':
        print(f"\n{t:9.1f} {NAMES.get(arg, arg)}", end=''); base = t; continue
    if what == 'btn-up':
        print(f" | up+{t - base:.0f}", end=''); continue
    if what.startswith('gray-strip'):
        continue
    if what.startswith('refresh'):
        fb = what.split('fb=')[1]
        same = ' SAME-FRAME' if fb == prev_fb else ''
        prev_fb = fb
        print(f" | {MODES[arg]}+{t - base:.1f}{same} {io}", end='')
    else:
        print(f" | {what}+{t - base:.1f}", end='')
print()
