#!/usr/bin/env python3
"""Score the blind side-by-side answers against the hidden key.

    python3 tools/knuth_plass_blind_score.py answers.json \
        [docs/data/knuth-plass-2026-09-25/blind-key.json]

answers.json is what the page's "Copy answers" button produces:
{"answers": {"p01": "A" | "B" | "none", ...}, ...}

Reports how often Knuth-Plass was preferred among DECISIVE answers (A or B),
with an exact (Clopper-Pearson) 95% interval and a two-sided exact binomial
test against 50%, overall and per stratum. "No difference" answers are counted
and reported, never folded into either side. Pure Python, no dependencies.
"""

import json
import sys
from math import comb


def binom_cdf(k, n, p):
    return sum(comb(n, i) * p**i * (1 - p) ** (n - i) for i in range(0, k + 1))


def clopper_pearson(k, n, alpha=0.05):
    if n == 0:
        return (0.0, 1.0)

    def solve(f, target):  # f increasing in p on [0,1]; bisection
        lo, hi = 0.0, 1.0
        for _ in range(100):
            mid = (lo + hi) / 2
            if f(mid) < target:
                lo = mid
            else:
                hi = mid
        return (lo + hi) / 2

    lower = 0.0 if k == 0 else solve(lambda p: 1 - binom_cdf(k - 1, n, p), alpha / 2)
    upper = 1.0 if k == n else solve(lambda p: 1 - binom_cdf(k, n, p), 1 - alpha / 2)
    return lower, upper


def two_sided_p(k, n):
    if n == 0:
        return 1.0
    pk = comb(n, k) * 0.5**n
    return min(1.0, sum(comb(n, i) * 0.5**n for i in range(n + 1) if comb(n, i) * 0.5**n <= pk + 1e-12))


def line(label, kp, greedy, none):
    n = kp + greedy
    lo, hi = clopper_pearson(kp, n)
    rate = f"{100 * kp / n:5.1f}%" if n else "   --"
    print(f"  {label:<22} KP {kp:3d}  greedy {greedy:3d}  no-diff {none:3d}  "
          f"KP preferred {rate}  95% CI [{100 * lo:5.1f}%, {100 * hi:5.1f}%]  p={two_sided_p(kp, n):.3f}")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    answers = json.load(open(sys.argv[1]))["answers"]
    key_path = sys.argv[2] if len(sys.argv) > 2 else "docs/data/knuth-plass-2026-09-25/blind-key.json"
    key = json.load(open(key_path))["pairs"]
    groups = {}
    missing = [p for p in key if p not in answers]
    for pid, k in key.items():
        a = answers.get(pid)
        if a is None:
            continue
        outcome = "none" if a == "none" else k[a]  # "kp" or "greedy"
        for g in ("ALL", f"{k['face']} {k['pt']}", k["stratum"]):
            groups.setdefault(g, {"kp": 0, "greedy": 0, "none": 0})[outcome] += 1
    print(f"{len(key) - len(missing)} of {len(key)} pairs answered" + (f"; missing {missing}" if missing else ""))
    order = ["ALL", "Albo 14", "LibreFranklin 14", "kp_worse", "near_tie", "kp_better", "kp_much_better"]
    for g in order + sorted(set(groups) - set(order)):
        if g in groups:
            c = groups[g]
            line(g, c["kp"], c["greedy"], c["none"])


if __name__ == "__main__":
    main()
