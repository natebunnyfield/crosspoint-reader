#!/usr/bin/env python3
"""Render many dates through the real renderer and assert nothing clips.

Catches layout regressions that only appear for particular date shapes:
cross-year headers, 6-week windows spanning three months, Semana Santa
(Easter-relative holidays), and month-rollover subscripts colliding with
highlight boxes.
"""
import datetime, os, shutil, subprocess, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, "render_harness")

def render(d):
    fs = os.path.join(HERE, "fs_")
    if os.path.exists(fs):
        shutil.rmtree(fs)
    os.makedirs(fs)
    subprocess.run([BIN, str(d.year), str(d.month), str(d.day)],
                   capture_output=True, cwd=HERE, check=True)
    return np.array(Image.open(os.path.join(fs, "sleep.bmp")).convert("L"))

def main():
    bad, n = [], 0
    d, end = datetime.date(2025, 1, 1), datetime.date(2029, 12, 31)
    while d <= end:
        a = render(d)
        H, W = a.shape
        ys, xs = np.where(a < 128)
        if ys.min() < 2 or ys.max() > H - 3 or xs.min() < 2 or xs.max() > W - 3:
            bad.append(d.isoformat())
        n += 1
        d += datetime.timedelta(days=9)   # strides through all 7 weekdays
    print(f"sampled {n} dates")
    if bad:
        print("CLIPPING at:", ", ".join(bad[:12]))
        return 1
    print("no clipping ✓")
    return 0

if __name__ == "__main__":
    sys.exit(main())
