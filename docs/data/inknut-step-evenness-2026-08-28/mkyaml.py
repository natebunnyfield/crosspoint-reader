#!/usr/bin/env python3
"""Write a variant sd-fonts.yaml with InknutJunicode re-fitted to (k, ramp).

Only the Inknut block moves: sizes:, metrics: (base 1203/-340 x k), the two
roman scale: values (k) and the two italic scale: values (1.2 x k).
"""
import re, sys, pathlib

SRC = pathlib.Path('/Users/natebunnyfield/src/crosspoint-reader/lib/EpdFont/scripts/sd-fonts.yaml')
BASE_ASC, BASE_DESC, ITAL_BASE = 1203, -340, 1.2

def variant(k, ramp, out):
    s = SRC.read_text()
    i = s.index('  - name: InknutJunicode')
    j = s.index('  - name: LibreCaslonText')
    blk = s[i:j]
    n = blk
    n, c = re.subn(r'^    sizes: \[[^\]]*\]$',
                   '    sizes: [%s]' % ', '.join(str(v) for v in ramp), n, flags=re.M)
    assert c == 1, c
    n, c = re.subn(r'^    metrics: \{ascent: \d+, descent: -\d+, linegap: 0\}$',
                   '    metrics: {ascent: %d, descent: %d, linegap: 0}'
                   % (round(BASE_ASC*k), -round(-BASE_DESC*k)), n, flags=re.M)
    assert c == 1, c
    n, c = re.subn(r'\{scale: 0\.917,', '{scale: %g,' % round(k, 4), n)
    assert c == 2, c
    n, c = re.subn(r'scale: 1\.101,', 'scale: %g,' % round(ITAL_BASE*k, 4), n)
    assert c == 2, c
    pathlib.Path(out).write_text(s[:i] + n + s[j:])
    print(f"wrote {out}: k={k} ramp={list(ramp)} metrics={{{round(BASE_ASC*k)}, {-round(-BASE_DESC*k)}}} italic scale={round(ITAL_BASE*k,4)}")

if __name__ == '__main__':
    k = float(sys.argv[1]); ramp = [int(v) for v in sys.argv[2].split(',')]
    variant(k, ramp, sys.argv[3])
