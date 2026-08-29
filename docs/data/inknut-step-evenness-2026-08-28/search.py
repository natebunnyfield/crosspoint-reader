import math, itertools, re, numpy as np

sweep = {}
for line in open('/Users/natebunnyfield/src/crosspoint-reader/docs/data/almendra-anchored-2026-08-27/sweep.txt'):
    if not line.startswith('SINKN'): continue
    m = re.search(r'pt\s+(\d+)\s+lineH\s+(\d+).*?wordsPerPage\s+([\d.]+)', line)
    sweep[int(m.group(1))] = (int(m.group(2)), float(m.group(3)))
pts = sorted(sweep)
xs = np.array([math.log(p) for p in pts])
ys = np.array([math.log(sweep[p][1]) for p in pts])
sl0 = (ys[1]-ys[0])/(xs[1]-xs[0]); sl1 = (ys[-1]-ys[-2])/(xs[-1]-xs[-2])

def g(x):                      # ln wpp at ln(effective size)
    x = np.asarray(x, float)
    out = np.interp(x, xs, ys)
    out = np.where(x < xs[0],  ys[0]  + sl0*(x-xs[0]),  out)
    out = np.where(x > xs[-1], ys[-1] + sl1*(x-xs[-1]), out)
    return out

alm = np.array([349.52, 227.54, 162.81, 122.58, 96.30, 76.12])
la  = np.log(alm)

SIZES = np.arange(5, 22)
LS    = np.log(SIZES)
ramps = np.array(list(itertools.combinations(range(len(SIZES)), 6)))   # index into SIZES
ks    = np.arange(0.700, 1.1005, 0.001)

def evaluate(k):
    G = g(LS + math.log(k))                     # per point size
    R = G[ramps] - la                           # (nramp, 6)
    D = np.diff(R, axis=1)
    return R, D

results = []
for k in ks:
    R, D = evaluate(k)
    rms   = np.sqrt((R**2).mean(axis=1))
    drms  = np.sqrt((D**2).mean(axis=1))
    dmax  = np.abs(D).max(axis=1)
    results.append((k, R, rms, drms, dmax))

def report(lam, n=8):
    print(f"=== lambda = {lam}  (J = mean r^2 + lam * mean dstep^2) ===")
    rows = []
    for k, R, rms, drms, dmax in results:
        J = rms**2 + lam*drms**2
        i = int(np.argmin(J))
        rows.append((J[i], k, ramps[i], rms[i], drms[i], dmax[i], R[i]))
    rows.sort(key=lambda t: t[0])
    seen = set(); shown = 0
    for J, k, ri, rms, drms, dmax, R in rows:
        ramp = tuple(SIZES[ri])
        if ramp in seen: continue
        seen.add(ramp); shown += 1
        print(f"  k={k:.3f} ramp={list(ramp)}  rms={rms:.2f}%  dstep_rms={drms:.2f}%  dstep_max={dmax:.2f}%  r=[{' '.join(f'{v*100:+.1f}' for v in R)}]")
        if shown >= n: break
    print()

# sanity check against the shipped build
G = g(np.log(np.array([7,9,11,12,14,16], float)) + math.log(0.917))
print("shipped k=0.917 [7 9 11 12 14 16] modeled r%:", " ".join(f"{v*100:+.1f}" for v in (G-la)))
print("                             measured r%:  +0.8 -2.7 -4.5 +6.4 +3.6 -1.9")
print()
for lam in (0.0, 1.0, 3.0, 10.0, 30.0):
    report(lam)
