#!/usr/bin/env python3
"""For every highlight box in the render, compare the box centre to the
centre of the digit ink inside it. Catches vertical/horizontal drift that is
hard to judge by eye."""
import sys, subprocess, shutil, os, datetime
import numpy as np
from PIL import Image

def render(y,m,d):
    if os.path.exists('fs_'): shutil.rmtree('fs_')
    os.makedirs('fs_/.crosspoint')
    subprocess.run(['./render_harness',str(y),str(m),str(d)],capture_output=True,check=True)
    return np.array(Image.open('fs_/sleep.bmp').convert('L'))

def blobs(mask, minpx):
    """Connected components (4-neighbour), iterative flood fill."""
    seen = np.zeros_like(mask, dtype=bool); out=[]
    H,W = mask.shape
    for y in range(H):
        xs = np.where(mask[y] & ~seen[y])[0]
        for x in xs:
            if seen[y,x]: continue
            st=[(y,x)]; seen[y,x]=True
            ys_=[];xs_=[]
            while st:
                cy,cx=st.pop(); ys_.append(cy); xs_.append(cx)
                for dy,dx in ((1,0),(-1,0),(0,1),(0,-1)):
                    ny,nx=cy+dy,cx+dx
                    if 0<=ny<H and 0<=nx<W and mask[ny,nx] and not seen[ny,nx]:
                        seen[ny,nx]=True; st.append((ny,nx))
            if len(ys_)>=minpx:
                out.append((min(xs_),min(ys_),max(xs_),max(ys_)))
    return out

def check(datestr, y,m,d, tol=2):
    a = render(y,m,d)
    # The dithered grey fill is a checkerboard of black/white; the *border* is
    # solid black. Find filled cells by locating the solid rounded-rect borders.
    black = a < 128
    # A holiday/today box border encloses a region ~40-70px square in the grid area.
    cand = [b for b in blobs(black, 150)
            if 30 <= (b[2]-b[0]) <= 80 and 30 <= (b[3]-b[1]) <= 80]
    problems=[]
    for (x0,y0,x1,y1) in cand:
        bw, bh = x1-x0+1, y1-y0+1
        # Interior, excluding the 3px border ring so we measure only the digit.
        pad=4
        ix0,iy0,ix1,iy1 = x0+pad, y0+pad, x1-pad, y1-pad
        if ix1<=ix0 or iy1<=iy0: continue
        interior = black[iy0:iy1+1, ix0:ix1+1]
        # Today's cell is inverted (black fill, white digit); holiday cells are
        # grey dither with a black digit. Detect polarity from the fill density
        # and measure whichever colour the digit actually is.
        inverted = interior.mean() > 0.75
        ink = ~interior if inverted else interior
        # The grey dither is a regular 50% checkerboard; digit strokes are solid
        # runs. Keep pixels whose 4-neighbours are also ink (erosion) to drop dither.
        er = (ink[1:-1,1:-1] & ink[:-2,1:-1] & ink[2:,1:-1]
              & ink[1:-1,:-2] & ink[1:-1,2:])
        ys,xs = np.where(er)
        if len(ys)==0:
            problems.append(f"  box at ({x0},{y0}) {bw}x{bh}: no digit ink found")
            continue
        ink_cx = ix0+1+ (xs.min()+xs.max())/2
        ink_cy = iy0+1+ (ys.min()+ys.max())/2
        box_cx = (x0+x1)/2
        box_cy = (y0+y1)/2
        dx, dy = ink_cx-box_cx, ink_cy-box_cy
        status = "ok" if (abs(dx)<=tol and abs(dy)<=tol) else "OFF"
        if status=="OFF":
            problems.append(f"  box ({x0},{y0}) {bw}x{bh}: digit offset dx={dx:+.1f} dy={dy:+.1f}")
        kind = "INVERTED(today)" if inverted else "grey(holiday)"
        print(f"    box@({x0:3d},{y0:3d}) {bw}x{bh} {kind:15s} digit dx={dx:+5.1f} dy={dy:+5.1f}  {status}")
    return problems

if __name__=='__main__':
    allprob=[]
    for label,(y,m,d) in [("today 2026-07-27",(2026,7,27)),
                          ("Semana Santa 2026-03-30",(2026,3,30)),
                          ("Thanksgiving 2026-11-22",(2026,11,22)),
                          ("year boundary 2026-12-28",(2026,12,28))]:
        print(f"\n{label}:")
        allprob += check(label,y,m,d)
    print()
    if allprob:
        print("PROBLEMS:"); [print(p) for p in allprob]; sys.exit(1)
    print("all highlight boxes: digit centred within 2px ✓")
