#!/usr/bin/env python3
"""Per-press latency to the GLASS, from the simulator's [lat] trace
(CROSSPOINT_SIM_LOG_LATENCY=1; the written / glass / swept / sweep-over lines
exist since crosspoint-simulator 8ede3c54). summarize.py stops at the panel
call; on a host the press is on screen only after the present that is held,
composed and swept after it. docs/menu-nav-lag-2026-11-27.md has the method.

usage: tools/latency/glass.py <log> [start_ms]   (presses before start_ms,
       e.g. the walk to the screen under test, are left out)
  written  the press's frame left the firmware (render done)
  glass    first flip carrying that frame
  done     the frame is wholly on the glass: the flip that finished its beam
           sweep, or its first flip when it landed whole (no sweep)
A frame written but never flipped was coalesced away (the selection skipped
it). A sweep 'over a STALE picture' painted the new frame down over a capture
older than the frame the glass was showing: the old selection came back."""
import re, sys, statistics
LAT = re.compile(r'\[lat\] (\d+) (\S+)(?: fb=\w+)? (-?\d+) ')
ev = []
for line in open(sys.argv[1], errors='replace'):
    m = LAT.match(line)
    if m: ev.append((int(m[1]) / 1000.0, m[2], int(m[3])))
start = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0
written, glass, swept, swept_frames = {}, {}, {}, set()
pending_over, prev_glass, stale, sweeps, whole = None, None, [], 0, 0
for t, w, a in ev:
    if w == 'written': written[a] = t
    elif w == 'swept': swept.setdefault(a, t)
    elif w == 'sweep-over': pending_over = a
    elif w == 'glass':
        glass.setdefault(a, t)
        if t >= start:
            if pending_over is not None:
                sweeps += 1; swept_frames.add(a)
                if prev_glass is not None and pending_over < prev_glass:
                    stale.append((a, pending_over, prev_glass))
            else:
                whole += 1
        pending_over, prev_glass = None, a
presses = [(t, a) for t, w, a in ev if w == 'btn-down' and t >= start]
rows = []
for t, b in presses:
    seqs = sorted(s for s, wt in written.items() if wt >= t)
    s = seqs[0] if seqs else None
    shown = next((x for x in seqs if x in glass), None)
    g = glass.get(shown) if shown else None
    done = (swept.get(shown) if shown in swept_frames else g) if shown else None
    rows.append((t, s, written.get(s), shown, g, done))
f = lambda x, t: f"{x - t:7.1f}" if x is not None else "      -"
for t, s, wt, shown, g, done in rows:
    kind = 'swept' if shown in swept_frames else 'whole'
    print(f"{t:10.1f} written#{s} {f(wt, t)}  glass#{shown} {f(g, t)}  done {f(done, t)} ({kind})")
ws = [s for s, wt in written.items() if wt >= start]
lost = sorted(s for s in ws if s not in glass)
print(f"frames written {len(ws)}, never on glass {len(lost)} {lost[:20]}")
print(f"frames swept {sweeps}, landed whole {whole}, swept over a STALE picture {len(stale)}"
      + (": " + ", ".join(f"#{a} over #{u} (glass showed #{p})" for a, u, p in stale[:10]) if stale else ""))
lat = [g - t for t, s, wt, shown, g, done in rows if g is not None]
dn = [done - t for t, s, wt, shown, g, done in rows if done is not None]
if lat: print(f"press->glass ms: median {statistics.median(lat):.1f} max {max(lat):.1f} n={len(lat)}")
if dn: print(f"press->done  ms: median {statistics.median(dn):.1f} max {max(dn):.1f} n={len(dn)} (of {len(rows)} presses)")
