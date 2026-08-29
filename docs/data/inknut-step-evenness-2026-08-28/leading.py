#!/usr/bin/env python3
"""Leading floor, measured off built .cpfont files.

floor = worst-style plain-text ink span + 0.13 em   (sd-fonts.yaml:146-151,
tools/sans-bench/sweep_sans.py:34 -- INK_PAD_PER_MILLE = 65 each side).

The em is reported two ways because `scale:` moves them apart:
  em_ppem = pt * 150/72          the point size's em, as sweep_sans used it
  em_drawn = em_ppem * k         the em of the type as actually drawn
"""
import sys, os, re, struct

PLAIN = ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
         "0123456789.,;:!?'\"()-–—‘’“”")
ACC = "ÁÉÍÓÚÄÖÜÀÂÊÑÅ"

def styles(path):
    f = open(path,'rb'); hdr=f.read(32); assert hdr[:6]==b'CPFONT'
    sc = hdr[12]; out=[]
    for _ in range(sc):
        b=f.read(32)
        sid=b[0]; ivc,gc=struct.unpack_from('<II',b,4); advY=b[12]
        asc,desc=struct.unpack_from('<hh',b,13); off=struct.unpack_from('<I',b,24)[0]
        out.append((sid,ivc,gc,advY,asc,desc,off))
    return f,out

def span(f, st, chars):
    sid,ivc,gc,advY,asc,desc,off = st
    f.seek(off); iv=f.read(ivc*12)
    ivs=[struct.unpack_from('<III',iv,i*12) for i in range(ivc)]
    gb=off+ivc*12
    top_max=-9999; bot_min=9999
    for ch in chars:
        cp=ord(ch)
        for a,b_,o in ivs:
            if a<=cp<=b_:
                gi=o+(cp-a); f.seek(gb+gi*16); r=f.read(16)
                w,h,ax,left,top,dl = struct.unpack_from('<BBHhhH', r, 0)
                if h==0: break
                top_max=max(top_max, top); bot_min=min(bot_min, top-h)
                break
    return top_max, bot_min, top_max-bot_min

def report(tree, fam, k):
    d=os.path.join(tree,fam)
    files=sorted([p for p in os.listdir(d) if p.endswith('.cpfont')],
                 key=lambda p:int(re.search(r'_(\d+)\.',p).group(1)))
    print(f"{'pt':>4}{'advY':>6}{'inkspan':>9}{'clear':>7}{'pad/em_ppem':>13}{'pad/em_drawn':>14}{'accTop':>8}{'ascHdr':>8}")
    ok=True
    for p in files:
        pt=int(re.search(r'_(\d+)\.',p).group(1))
        f,sts=styles(os.path.join(d,p))
        worst=0; wtop=0
        for st in sts:
            t,b,s = span(f,st,PLAIN)
            if s>worst: worst=s; wtop=t
        advY=sts[0][3]; ascHdr=sts[0][4]
        at,ab,_=span(f,sts[0],ACC)
        f.close()
        em_ppem = pt*150/72.0
        em_drawn = em_ppem*k
        clear = advY-worst
        r1 = clear/em_ppem; r2 = clear/em_drawn
        flag = '' if r2>=0.13 else '  <-- BELOW 0.13 em_drawn'
        if r2<0.13: ok=False
        print(f"{pt:>4}{advY:>6}{worst:>9}{clear:>7}{r1:>13.3f}{r2:>14.3f}{at:>8}{ascHdr:>8}{flag}")
    return ok

if __name__=='__main__':
    print("### SHIPPED  k=0.917  sizes 7 9 11 12 14 16")
    report('trees/OLD','InknutJunicode',0.917)
    print("\n### CANDIDATE k=0.805  sizes 8 10 12 14 16 18")
    report(sys.argv[1] if len(sys.argv)>1 else 'trees/I','InknutJunicode',0.805)
    print("\n### ANCHOR Almendra (no k)")
    report('/Users/natebunnyfield/src/crosspoint-simulator/build/seedfonts','Almendra',1.0)
