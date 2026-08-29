#!/usr/bin/env python3
"""Read x-height, cap height, ascender, descender and advanceY out of .cpfont headers."""
import struct, sys, os, re

def read_style(f, toc):
    styleId, ivCount, glyphCount, advY, asc, desc, kl, kr, klc, krc, ligs, dataOff = toc
    # intervals
    f.seek(dataOff)
    iv = f.read(ivCount*12)
    intervals = [struct.unpack_from('<III', iv, i*12) for i in range(ivCount)]
    gbase = dataOff + ivCount*12
    def glyph_for(cp):
        for first, last, off in intervals:
            if first <= cp <= last:
                gi = off + (cp - first)
                f.seek(gbase + gi*16)
                b = f.read(16)
                w, h, adv, left, top, dl = struct.unpack_from('<BBHhhH', b, 0)
                return dict(w=w, h=h, adv=adv/16.0, left=left, top=top)
        return None
    return dict(styleId=styleId, advanceY=advY, ascender=asc, descender=desc,
                glyph=glyph_for)

def read_file(path):
    with open(path, 'rb') as f:
        hdr = f.read(32)
        if hdr[:6] != b'CPFONT':
            raise SystemExit('not a cpfont (compressed?): %s' % path)
        ver, flags, sc = struct.unpack_from('<HHB', hdr, 8)
        tocs = []
        for i in range(sc):
            b = f.read(32)
            styleId = b[0]
            ivCount, glyphCount = struct.unpack_from('<II', b, 4)
            advY = b[12]
            asc, desc, kl, kr = struct.unpack_from('<hhHH', b, 13)
            klc, krc, ligs = b[21], b[22], b[23]
            dataOff = struct.unpack_from('<I', b, 24)[0]
            tocs.append((styleId, ivCount, glyphCount, advY, asc, desc, kl, kr, klc, krc, ligs, dataOff))
        out = {}
        for toc in tocs:
            st = read_style(f, toc)
            g = st['glyph']
            x = g(ord('x')); H = g(ord('H')); n = g(ord('n')); o = g(ord('o'))
            out[st['styleId']] = dict(advanceY=st['advanceY'], ascender=st['ascender'],
                                      descender=st['descender'],
                                      xh=x['top'] if x else None, cap=H['top'] if H else None,
                                      xhb=x['h'] if x else None, capb=H['h'] if H else None,
                                      advx=round(x['adv'],2) if x else None)
        return out

if __name__ == '__main__':
    root = sys.argv[1]; fam = sys.argv[2]
    tier = sys.argv[3] if len(sys.argv) > 3 else ''
    d = os.path.join(root, fam, tier) if tier else os.path.join(root, fam)
    files = sorted([p for p in os.listdir(d) if p.endswith('.cpfont')],
                   key=lambda p: int(re.search(r'_(\d+)\.cpfont$', p).group(1)))
    print(f"{'pt':>4} {'advY':>5} {'xh':>4} {'cap':>4} {'xhb':>4} {'capb':>5} {'asc':>4} {'desc':>5}")
    prev = None
    for p in files:
        pt = int(re.search(r'_(\d+)\.cpfont$', p).group(1))
        r = read_file(os.path.join(d, p))[0]
        step = f"  x{r['advanceY']/prev:.3f}" if prev else ""
        print(f"{pt:>4} {r['advanceY']:>5} {r['xh']:>4} {r['cap']:>4} {r['xhb']:>4} {r['capb']:>5} {r['ascender']:>4} {r['descender']:>5}{step}")
        prev = r['advanceY']
