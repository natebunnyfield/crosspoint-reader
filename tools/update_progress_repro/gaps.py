import re, sys
log = open(sys.argv[1], errors='replace').read().splitlines()
start = end = None; pres = []
for l in log:
    m = re.search(r'^\[(\d+)\] .*Entering activity: (FontUpdate|LibraryUpdate)', l)
    if m and start is None: start = int(m.group(1))
    m = re.search(r'^\[(\d+)\] .*(font sync done|library sync done|stopped by the reader)', l)
    if m and end is None: end = int(m.group(1))
    m = re.search(r'\[present\] #\d+ at (\d+) ms', l)
    if m: pres.append(int(m.group(1)))
print('activity window (firmware ms):', start, '->', end)
w = [p for p in pres if start is not None and p >= start and (end is None or p <= end + 2000)]
print('presents in window:', len(w))
if len(w) > 1:
    pts = [start] + w
    gaps = [(b - a, a, b) for a, b in zip(pts, pts[1:])]
    g = max(gaps); print('max gap %d ms (%d -> %d)' % g)
    print('gaps >1500 ms:', [x[0] for x in gaps if x[0] > 1500])
# NEW pictures only: a present whose preceding [accum] line says changed=1 (the
# phosphor trail's decay frames re-present the same picture and are excluded).
newp = []; changed = False
for l in log:
    if '[accum] trail' in l: changed = 'changed=1' in l
    m = re.search(r'\[present\] #\d+ at (\d+) ms', l)
    if m and changed: newp.append(int(m.group(1)))
w = [p for p in newp if start is not None and p >= start and (end is None or p <= end + 2000)]
print('NEW-picture presents in window:', len(w))
if w:
    pts = [start] + w
    gaps = [(b - a, a, b) for a, b in zip(pts, pts[1:])]
    print('max NEW-picture gap %d ms (%d -> %d)' % max(gaps))
