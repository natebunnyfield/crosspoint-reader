import numpy as np
from PIL import Image, ImageDraw, ImageFont
E = "eink"; SLOTS = [(0, 9), (1, 11), (2, 12), (3, 14)]
PAPER = (0xF9, 0xF3, 0xE9); LABEL = (0x7A, 0x6E, 0x62); GAP = 24; M = 40
ui = ImageFont.truetype("/Users/natebunnyfield/src/crosspoint-reader/lib/EpdFont/scripts/downloaded_fonts/QHERO/texgyreheros-regular.otf", 15)
def page(fam, slot, aa=True):
    return Image.open(f"{E}/{'aa_' if aa else 'reading_'}{fam}_{slot}.png").convert("L")
def label(im, text):
    out = Image.new("RGB", (im.width, 28 + im.height), PAPER)
    ImageDraw.Draw(out).text((M, 7), text, font=ui, fill=LABEL); out.paste(im, (0, 28)); return out
def pair(a, b, gap=GAP):
    out = Image.new("RGB", (M + a.width + gap + b.width + M, max(a.height, b.height)), PAPER)
    out.paste(a.convert("RGB"), (M, 0)); out.paste(b.convert("RGB"), (M + a.width + gap, 0)); return out
def stats(im):
    a = np.asarray(im); ink = a < 255
    n = ink.sum(); lv = {v: int((a == v).sum()) for v in (0, 96, 200)}
    return n, lv, 1.0 - a[ink].mean() / 255.0 if n else 0.0
blocks = []; report = []
for slot, pt in SLOTS:
    A = page("HerosRef", slot).crop((0, 0, 528, 470)); B = page("HerosTextCut", slot).crop((0, 0, 528, 470))
    nA, lA, dA = stats(page("HerosRef", slot)); nB, lB, dB = stats(page("HerosTextCut", slot))
    report.append((pt, nA, lA, dA, nB, lB, dB))
    blocks.append(label(pair(A, B), f"{pt} pt slot — as shipped (left) · Text Cut (right). Firmware renderer, four gray levels, 528 px panel, native pixels. Solid-black share {lA[0]/nA:.0%} → {lB[0]/nB:.0%}."))
# zooms: a body line at 9 pt and 12 pt, 3x NEAREST, A above B
def band_at(fam, slot, y_target):
    """The text row band of the Ref page containing y_target: (top, bottom) with a little air."""
    a = np.asarray(page(fam, slot)); rows = np.nonzero((a < 255).any(axis=1))[0]
    bands = []; s0 = rows[0]; prev = rows[0]
    for r in rows[1:]:
        if r > prev + 2: bands.append((s0, prev)); s0 = r
        prev = r
    bands.append((s0, prev))
    top, bot = min(bands, key=lambda b: abs((b[0] + b[1]) / 2 - y_target))
    return int(top) - 4, int(bot) + 5
def zoom(fam, slot, box, k=3):
    im = page(fam, slot).crop(box); return im.resize((im.width * k, im.height * k), Image.NEAREST)
for slot, pt, y in [(0, 9, 185), (2, 12, 300)]:
    t, b = band_at("HerosRef", slot, y); box = (14, t, 514, b)
    za, zb = zoom("HerosRef", slot, box), zoom("HerosTextCut", slot, box)
    col = Image.new("RGB", (M + za.width + M, za.height + 12 + zb.height), PAPER)
    col.paste(za.convert("RGB"), (M, 0)); col.paste(zb.convert("RGB"), (M, za.height + 12))
    blocks.append(label(col, f"one body line at {pt} pt, 3× NEAREST — as shipped above, Text Cut below. Four levels only: paper, 200, 96, black."))
# 1-bit base pass at 9 pt: what the panel shows before the gray planes land
A1 = page("HerosRef", 0, aa=False).crop((0, 150, 528, 330)); B1 = page("HerosTextCut", 0, aa=False).crop((0, 150, 528, 330))
blocks.append(label(pair(A1, B1), "9 pt, the 1-bit BASE PASS — what the panel shows before the two gray planes land. Left as shipped, right Text Cut."))
W = max(b.width for b in blocks); H = sum(b.height for b in blocks) + GAP * (len(blocks) - 1) + 2 * M
fig = Image.new("RGB", (W, H), PAPER); y = M
for b in blocks: fig.paste(b, (0, y)); y += b.height + GAP
fig.save("heros-eink.png", optimize=True); print("figure", fig.size)
print("pt   ink px A->B   black share A->B   dark(96) share A->B   light(200) share A->B   mean darkness A->B")
for pt, nA, lA, dA, nB, lB, dB in report:
    print(f"{pt:>2}   {nA:>6}->{nB:<6}  {lA[0]/nA:.3f}->{lB[0]/nB:.3f}        {lA[96]/nA:.3f}->{lB[96]/nB:.3f}          {lA[200]/nA:.3f}->{lB[200]/nB:.3f}           {dA:.3f}->{dB:.3f}")
