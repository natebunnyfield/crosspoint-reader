#!/usr/bin/env python3
"""Build the blind side-by-side: shipped greedy vs candidate Knuth-Plass.

Owner ruling 2026-09-25, "Blind side-by-side first". Account and results:
docs/knuth-plass-line-breaking-2026-09-25.md section 12.

Inputs (both produced by test/line_break_quality/LineBreakKnuthPlassTest):
  * stats CSVs from KnuthPlass.DISABLED_BlindStats (one row per paragraph)
  * the corpus file from tools/linebreak_corpus.py
Outputs:
  * docs/data/knuth-plass-2026-09-25/blind/index.html + pNN_A.png / pNN_B.png
  * docs/data/knuth-plass-2026-09-25/blind-key.json -- OUTSIDE blind/, and
    never referenced by the page, so publishing the blind/ folder cannot carry
    the answer with it.

Everything random is drawn from one random.Random(SEED), so the same inputs
give the same pairs, the same order and the same sides.

    python3 tools/knuth_plass_blind.py --stats-dir DIR --corpus corpus.txt \
        --sd SNAPSHOT_SD --binary build/line_break_quality/LineBreakKnuthPlassTest
"""

import argparse
import csv
import json
import os
import random
import subprocess
import sys
import tempfile

from PIL import Image

SEED = 20260925
OUT = "docs/data/knuth-plass-2026-09-25/blind"
KEY = "docs/data/knuth-plass-2026-09-25/blind-key.json"

# How many pairs per face, and how they are spread over the worst-line
# difference d = greedy_worst - kp_worst (word spaces). Stratified on purpose so
# the page is not only easy wins: a quarter of Albo's pairs are ones where
# Knuth-Plass's worst line is WORSE, and a quarter are near ties.
STRATA = [
    ("kp_worse", lambda d: d < -0.1),
    ("near_tie", lambda d: -0.1 <= d <= 0.1),
    ("kp_better", lambda d: 0.1 < d <= 1.0),
    ("kp_much_better", lambda d: d > 1.0),
]
PER_FACE = {("Albo", 14): [9, 8, 9, 8], ("LibreFranklin", 14): [2, 1, 2, 1]}
MIN_LINES, MAX_LINES = 3, 12  # at least a real paragraph; at most what fits a phone screen at 1:1


def load_stats(path):
    with open(path) as f:
        return [
            {k: (float(v) if "." in v else int(v)) for k, v in row.items()}
            for row in csv.DictReader(f)
        ]


def choose(rng, rows, counts):
    eligible = [
        r for r in rows
        if r["differ"] == 1
        and MIN_LINES <= r["greedy_lines"] <= MAX_LINES
        and MIN_LINES <= r["kp_lines"] <= MAX_LINES
    ]
    picked = []
    for (name, pred), n in zip(STRATA, counts):
        pool = sorted((r for r in eligible if pred(r["greedy_worst"] - r["kp_worst"])), key=lambda r: r["idx"])
        if len(pool) < n:
            sys.exit(f"stratum {name}: only {len(pool)} eligible, need {n}")
        for r in rng.sample(pool, n):
            picked.append((r, name))
    return picked, len(eligible)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stats-dir", required=True)
    ap.add_argument("--corpus", required=True)
    ap.add_argument("--sd", required=True, help="SD root holding the Albo .cpfont snapshot")
    ap.add_argument("--binary", default="build/line_break_quality/LineBreakKnuthPlassTest")
    args = ap.parse_args()

    rng = random.Random(SEED)
    chosen = []
    for (fam, pt), counts in PER_FACE.items():
        rows = load_stats(os.path.join(args.stats_dir, f"stats_{fam}_{pt}.csv"))
        picked, n_eligible = choose(rng, rows, counts)
        differ = sum(r["differ"] for r in rows)
        print(f"{fam} {pt}: {len(rows)} paragraphs, {differ} differ, {n_eligible} eligible, {len(picked)} picked")
        chosen += [(fam, pt, r, stratum) for r, stratum in picked]

    tmp = tempfile.mkdtemp(prefix="kpblind")
    spec = ",".join(f"{fam}:{pt}:{r['idx']}" for fam, pt, r, _ in chosen)
    env = dict(os.environ, CROSSPOINT_TEST_SD=args.sd, CROSSPOINT_LINEBREAK_CORPUS=args.corpus,
               CROSSPOINT_KP_OUT=tmp, CROSSPOINT_KP_BLIND=spec)
    subprocess.run([args.binary, "--gtest_also_run_disabled_tests", "--gtest_filter=*BlindRender*"],
                   env=env, check=True, stdout=subprocess.DEVNULL)

    rng.shuffle(chosen)  # page order
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".png"):
            os.remove(os.path.join(OUT, f))
    key = {"seed": SEED, "note": "A/B assignment per pair; NOT referenced by index.html", "pairs": {}}
    for n, (fam, pt, r, stratum) in enumerate(chosen, 1):
        pid = f"p{n:02d}"
        greedy_left = rng.random() < 0.5
        sides = {"A": "greedy" if greedy_left else "kp", "B": "kp" if greedy_left else "greedy"}
        sizes = set()
        for side, arm in sides.items():
            im = Image.open(os.path.join(tmp, f"{fam}_{pt}_{r['idx']}_{arm}.pgm"))
            sizes.add(im.size)
            # Strip every text chunk: nothing in the file may name the arm.
            im.save(os.path.join(OUT, f"{pid}_{side}.png"), optimize=True, pnginfo=None)
        if len(sizes) != 1:
            sys.exit(f"{pid}: arms differ in size {sizes} -- the size would give the answer away")
        key["pairs"][pid] = {
            "A": sides["A"], "B": sides["B"], "face": fam, "pt": pt, "corpus_index": r["idx"],
            "stratum": stratum, "greedy_worst": r["greedy_worst"], "kp_worst": r["kp_worst"],
            "greedy_hyph": r["greedy_hyph"], "kp_hyph": r["kp_hyph"],
            "greedy_lines": r["greedy_lines"], "kp_lines": r["kp_lines"],
        }
    with open(KEY, "w") as f:
        json.dump(key, f, indent=1)
    n_greedy_a = sum(1 for p in key["pairs"].values() if p["A"] == "greedy")
    print(f"{len(chosen)} pairs -> {OUT}; greedy on A in {n_greedy_a}; key -> {KEY}")


if __name__ == "__main__":
    main()
